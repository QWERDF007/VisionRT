// R50 Python environment gate: VisionRT owns one answer to "can Python tools run", states the exact
// reason when it cannot, and never blocks the window while it checks. Every case below goes through
// the real controller: the same probe, the same settings file and the same tool registry a user's
// window uses.

#include "python/PythonEnvironment.h"
#include "ui/AppController.h"
#include "ui/UiPreferences.h"

#include "WorkbenchFixtures.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTest>

#include <iostream>
#include <stdexcept>

using namespace visionrt;

namespace
{

void require(bool condition, const QString &message)
{
    if (!condition)
        throw std::runtime_error(message.toStdString());
}

QVariantMap definition(const QVariantList &definitions, const QString &id)
{
    for (const auto &item : definitions)
    {
        const auto map = item.toMap();
        if (map.value(QStringLiteral("id")).toString() == id)
            return map;
    }
    return {};
}

QByteArray settingsBytes()
{
    QFile file(UiPreferences::path());
    require(file.open(QIODevice::ReadOnly), "the settings file was not written");
    return file.readAll();
}

// The preferences file lives in the application directory, so it is shared with every other test
// binary and with a developer's own installation. This contract owns it while it runs and gives the
// previous bytes back afterwards.
class SettingsFileGuard
{
public:
    SettingsFileGuard()
        : path_(UiPreferences::path())
    {
        QFile file(path_);
        if (file.open(QIODevice::ReadOnly))
            previous_ = file.readAll();
        existed_ = file.exists();
        // The read handle must be closed first: Windows refuses to delete a file that is still open,
        // and a developer's own preferences would then leak into every case below.
        file.close();
        QFile::remove(path_);
    }

    ~SettingsFileGuard()
    {
        QFile::remove(path_);
        if (!existed_)
            return;
        QFile file(path_);
        if (file.open(QIODevice::WriteOnly))
            file.write(previous_);
    }

private:
    QString    path_;
    QByteArray previous_;
    bool       existed_ = false;
};

// The Python declaration shipped with the test package; a native tool is the control group that
// must keep working while every Python tool is unavailable.
const QString kPythonTool = QStringLiteral("RoiGeometry");
const QString kNativeTool = QStringLiteral("ReadImage");

} // namespace

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    SettingsFileGuard settingsFile;

    // 0. Identity of an environment path is not its spelling. CPython answers with backslashes for a
    //    venv that was configured with forward ones; treating that as a different environment made
    //    every standard venv unusable after the probe had accepted it.
    require(environmentPathEquals("F:\\tmp\\venv", std::filesystem::path("F:/tmp/venv")),
            "a backslash spelling was treated as a different environment");
    require(environmentPathEquals("f:/TMP/Venv/", std::filesystem::path("F:/tmp/venv")),
            "case and a trailing separator changed the environment identity");
    require(!environmentPathEquals("F:/tmp/venv2", std::filesystem::path("F:/tmp/venv")),
            "a different directory was accepted as the same environment");

    // 1. Not configured is a legal setting, not a failure: the native catalog works, every Python
    //    tool says so, and no check runs at all.
    {
        AppController::usePythonForTesting(QString());
        testing::WorkbenchInstallation installation;
        AppController                  controller(1, installation.directory());
        require(QTest::qWaitFor([&] { return !controller.busy() && !controller.toolDefinitions().isEmpty(); }, 30000),
                "tool catalog did not become ready");
        require(controller.pythonEnvironmentPhase() == QStringLiteral("unconfigured"),
                "an empty selection was not reported as unconfigured");
        require(!controller.settingsApplying(), "an unconfigured environment has nothing to check");
        const auto python = definition(controller.toolDefinitions(), kPythonTool);
        require(!python.isEmpty() && !python.value("available").toBool(),
                "a Python tool was available without an interpreter");
        require(python.value("message").toString() == QStringLiteral("未配置 Python 环境"),
                "unconfigured Python tools did not report the not-configured reason");
        const auto native = definition(controller.toolDefinitions(), kNativeTool);
        require(native.value("available").toBool(), "a native tool became unavailable without Python");

        // The report is the release evidence: an unconfigured process must show no interpreter in
        // the loader's table, not merely an empty setting.
        const auto report      = controller.pythonEnvironmentReport();
        const auto environment = report.value(QStringLiteral("environment")).toMap();
        const auto process     = report.value(QStringLiteral("process")).toMap();
        require(environment.value(QStringLiteral("phase")).toString() == QStringLiteral("unconfigured"),
                "the report lost the environment phase");
        require(environment.value(QStringLiteral("numpyFile")).toString().isEmpty(),
                "an unconfigured environment reported a NumPy file");
        require(process.value(QStringLiteral("runtime")).toString().isEmpty(),
                "an unconfigured process reported a loaded Python runtime");
        QStringList loadedModules;
        for (const auto &module : process.value(QStringLiteral("modules")).toList())
            loadedModules << module.toMap().value(QStringLiteral("name")).toString();
        require(loadedModules.isEmpty(),
                "an unconfigured process listed Python modules: " + loadedModules.join(QStringLiteral(", ")));
    }

    // 2. A configured interpreter that is gone keeps its own reason: the tool list must never fall
    //    back to "not configured", which would send the user looking for the wrong problem.
    {
        AppController::usePythonForTesting(QStringLiteral("Z:/visionrt/missing/python.exe"));
        testing::WorkbenchInstallation installation;
        AppController                  invalid(1, installation.directory());
        require(invalid.pythonEnvironmentPhase() == QStringLiteral("invalid"),
                "a missing interpreter was not reported as invalid");
        require(invalid.pythonEnvironmentText().contains(QStringLiteral("路径不存在")),
                "the environment status lost the missing-path reason");
        const auto python = definition(invalid.toolDefinitions(), kPythonTool);
        require(!python.value("available").toBool(), "a Python tool was available with a missing interpreter");
        const auto message = python.value("message").toString();
        require(message.contains(QStringLiteral("路径不存在")),
                "the tool status lost the missing-path reason: " + message);
        require(!message.contains(QStringLiteral("未配置")),
                "a configured but missing interpreter was reported as not configured");
        const auto report = invalid.pythonEnvironmentReport().value(QStringLiteral("environment")).toMap();
        require(report.value(QStringLiteral("phase")).toString() == QStringLiteral("invalid"),
                "the report did not carry the rejected environment phase");
        require(report.value(QStringLiteral("reason")).toString().contains(QStringLiteral("路径不存在")),
                "the report lost the missing-path reason: " + report.value(QStringLiteral("reason")).toString());

        // 3. Applying a real interpreter answers later: the call itself must not wait for the check.
        auto values = invalid.settingsSnapshot();
        values[QStringLiteral("pythonExecutable")] = QString::fromUtf8(VISIONRT_TEST_PYTHON_EXECUTABLE);
        QSignalSpy applied(&invalid, &AppController::settingsApplied);
        require(invalid.applySettings(values), "a valid interpreter draft was refused before checking");
        require(invalid.settingsApplying(), "applying an interpreter blocked until the check answered");
        require(!invalid.applySettings(values), "a second commit was accepted while one was checking");
        require(QTest::qWaitFor([&] { return applied.count() == 1; }, 120000), "the check never answered");
        require(applied.first().at(0).toBool(), "a valid interpreter was refused: " + invalid.settingsError());
        require(!invalid.settingsApplying(), "the settings commit stayed pending after its answer");
        require(invalid.settingsSnapshot().value(QStringLiteral("pythonExecutable")).toString()
                    == QDir::cleanPath(QString::fromUtf8(VISIONRT_TEST_PYTHON_EXECUTABLE)),
                "the accepted interpreter was not committed");
        // The check validates a draft; it never swaps the interpreter the process is running.
        require(invalid.pythonEnvironmentPhase() == QStringLiteral("invalid"),
                "applying an interpreter replaced the running environment");

        // 4. A refused check leaves the file byte for byte as it was.
        const auto before = settingsBytes();
        values[QStringLiteral("pythonExecutable")] = QStringLiteral("C:/Windows/System32/where.exe");
        require(invalid.applySettings(values), "a non-CPython draft was refused without being checked");
        require(QTest::qWaitFor([&] { return applied.count() == 2; }, 120000), "the second check never answered");
        require(!applied.at(1).at(0).toBool(), "a non-CPython interpreter was accepted");
        require(invalid.settingsError().contains(QStringLiteral("所选 Python 环境不可用")),
                "the refusal lost its reason: " + invalid.settingsError());
        require(invalid.settingsErrorField() == QStringLiteral("runtime"),
                "the refusal was attached to the wrong settings page");
        require(settingsBytes() == before, "a refused check rewrote the settings file");
        require(invalid.settingsSnapshot().value(QStringLiteral("pythonExecutable")).toString()
                    == QDir::cleanPath(QString::fromUtf8(VISIONRT_TEST_PYTHON_EXECUTABLE)),
                "a refused check changed the committed interpreter");
    }

    std::cout << "Python environment status, reasons and non-blocking check passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
