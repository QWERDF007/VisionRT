#include "log/LogService.h"
#include "ui/AppController.h"
#include "WorkbenchFixtures.h"
#include "ui/ImageViewport.h"
#include "ui/OutputModel.h"
#include "ui/Theme.h"
#include "ImageFixtures.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QClipboard>
#include <QFile>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>


void startupProjectContract()
{
    const auto require = [](bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };
    qmlRegisterType<visionrt::ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
    visionrt::registerStyleModule();
    for (const bool projectReadyBeforeWindow : {false, true})
    {
        visionrt::testing::WorkbenchInstallation installation;
        visionrt::AppController                  app(1, installation.directory());
        require(QTest::qWaitFor([&] { return !app.busy(); }, 20000), "startup catalog did not become ready");
        app.requestOpen(QStringLiteral(VISIONRT_SOURCE_DIR "/examples/pipeline_cpu_python.vrt.json"));
        app.resolveUnsaved(QStringLiteral("discard"));
        if (projectReadyBeforeWindow)
            require(QTest::qWaitFor([&] { return !app.busy() && app.nodes().size() == 5; }, 20000),
                    "preloaded startup project did not become ready");

        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
        engine.rootContext()->setContextProperty("app", &app);
        engine.rootContext()->setContextProperty("demoMenu", false);
        engine.rootContext()->setContextProperty("demoSelect", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/Main.qml")));
        require(!engine.rootObjects().isEmpty(), "startup Main.qml did not load");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        require(window && QTest::qWaitForWindowExposed(window), "startup main window was not exposed");
        require(QTest::qWaitFor([&] { return !app.busy() && app.nodes().size() == 5; }, 20000),
                "startup main window did not receive the real project");

        const auto findItem = [](auto &&self, QQuickItem *parent, const QString &name) -> QQuickItem *
        {
            for (auto *child : parent->childItems())
            {
                if (child->objectName() == name)
                    return child;
                if (auto *found = self(self, child, name))
                    return found;
            }
            return nullptr;
        };
        auto *canvas = findItem(findItem, window->contentItem(), QStringLiteral("flowCanvas"));
        require(canvas != nullptr, "startup flow canvas is missing");
        auto *surface = findItem(findItem, canvas, QStringLiteral("flowSurface"));
        require(surface != nullptr, "startup flow surface is missing");
        const auto allVisible = [&]
        {
            for (const auto &value : app.nodes())
            {
                const auto id = value.toMap().value("id").toString();
                auto *item = findItem(findItem, canvas, QStringLiteral("flowNode_") + id);
                if (!item || !item->isVisible())
                    return false;
                const auto bounds = item->mapRectToItem(surface, item->boundingRect());
                if (bounds.width() < 70 || bounds.height() <= 0
                    || !surface->boundingRect().adjusted(1, 1, -1, -1).contains(bounds))
                    return false;
            }
            return true;
        };
        const bool visible = QTest::qWaitFor(allVisible, 1500);
        const auto mode = projectReadyBeforeWindow ? "preloaded" : "pending";
        require(window->grabWindow().save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/startup_%1.png")
                                             .arg(QString::fromLatin1(mode))),
                "cannot save startup main-window evidence");
        if (!visible)
            throw std::runtime_error(std::string("Startup ") + mode + " project is clipped: viewport="
                                     + std::to_string(surface->width()) + "x" + std::to_string(surface->height())
                                     + "; zoom=" + std::to_string(canvas->property("zoom").toDouble())
                                     + "; pan=" + std::to_string(canvas->property("panX").toDouble()) + ","
                                     + std::to_string(canvas->property("panY").toDouble()));
        std::cout << "[Startup Contract Passed] " << mode << " project has five visible, readable nodes.\n";
        if (projectReadyBeforeWindow)
        {
            QTemporaryDir directory;
            require(directory.isValid(), "project diagnostic directory is unavailable");
            const auto badPath = directory.filePath(QStringLiteral("错误 工程.vrt.json"));
            QFile      badProject(badPath);
            require(badProject.open(QIODevice::WriteOnly), "cannot write malformed project fixture");
            require(badProject.write(R"json({"format_version":4,"project":{},"flows":{}})json") > 0,
                    "cannot populate malformed project fixture");
            badProject.close();
            const auto oldNodes   = app.nodes();
            const auto oldFlows   = app.flows();
            const auto oldTitle   = app.title();
            const auto beforeOpen = app.diagnostics().size();
            app.requestOpen(badPath);
            app.resolveUnsaved(QStringLiteral("discard"));
            require(QTest::qWaitFor([&] { return !app.busy() && app.diagnostics().size() > beforeOpen; }, 3000),
                    "project contract failure did not reach the application");
            const auto failure = QJsonDocument::fromJson(app.diagnosticsJson().toUtf8()).array().at(0).toObject();
            require(failure.value("code").toInt() == -2
                        && failure.value("message").toString().contains(QStringLiteral("flows 必须是数组"))
                        && failure.value("message").toString().contains(badPath)
                        && failure.value("detail").toString().isEmpty(),
                    "project contract reason was hidden behind an unrelated summary or exception detail");
            require(app.nodes() == oldNodes && app.flows() == oldFlows && app.title() == oldTitle,
                    "malformed project replaced the active project");
            // The precise rejection stays in the framework diagnostic and now reaches the log
            // window instead of a dedicated prompt dialog.
            require(window->findChild<QObject *>("applicationDiagnosticDialog") == nullptr,
                    "the removed project diagnostic dialog is still instantiated");
            require(window->findChild<QObject *>("logWindow") != nullptr,
                    "the project failure has no log surface");
            require(window->grabWindow().save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/project_load_failure.png")),
                    "cannot capture project failure diagnostic");
            QFile example(QStringLiteral(VISIONRT_SOURCE_DIR "/examples/pipeline_cpu_python.vrt.json"));
            require(example.open(QIODevice::ReadOnly), "cannot read project warning fixture");
            auto document     = QJsonDocument::fromJson(example.readAll()).object();
            auto projectFlows = document.value("flows").toArray();
            auto projectFlow  = projectFlows.at(0).toObject();
            auto projectNodes = projectFlow.value("nodes").toArray();
            auto configured   = projectNodes.at(3).toObject();
            require(configured.value("node_seq").toString() == "4", "project warning node identity changed");
            auto inputs = configured.value("inputs").toObject();
            auto kernel = inputs.value("kernel").toObject();
            kernel.insert("literal", 0);
            inputs.insert("kernel", kernel);
            configured.insert("inputs", inputs);
            projectNodes[3] = configured;
            projectFlow.insert("nodes", projectNodes);
            projectFlows[0] = projectFlow;
            document.insert("flows", projectFlows);
            const auto warningPath = directory.filePath(QStringLiteral("越界 工程.vrt.json"));
            QFile      warningProject(warningPath);
            require(
                warningProject.open(QIODevice::WriteOnly) && warningProject.write(QJsonDocument(document).toJson()) > 0,
                "cannot write project warning fixture");
            warningProject.close();
            app.requestOpen(warningPath);
            app.resolveUnsaved(QStringLiteral("discard"));
            const auto warningDiagnostic = [&]
            {
                for (const auto &entry : QJsonDocument::fromJson(app.diagnosticsJson().toUtf8()).array())
                    if (entry.toObject().value("storage_code") == "range_warning")
                        return entry.toObject();
                return QJsonObject{};
            };
            require(QTest::qWaitFor(
                        [&] { return !app.busy() && app.nodes().size() == 5 && !warningDiagnostic().isEmpty(); }, 5000),
                    "project warning load did not complete");
            const auto rangeWarning = warningDiagnostic();
            require(rangeWarning.value("flow_id") == "flow_1" && rangeWarning.value("node") == "4"
                        && rangeWarning.value("field") == "kernel"
                        && rangeWarning.value("message").toString().contains(QStringLiteral("已按声明收敛"))
                        && !rangeWarning.contains("code"),
                    "project warning lost its typed context or invented an execution status");
            app.selectNode("4");
            // The stored value is written back in its legal form: the row shows the clamped value
            // instead of a number the declaration rejects.
            bool clamped = false;
            for (const auto &parameter : app.parameters())
                if (parameter.toMap().value("id") == "kernel")
                    clamped = parameter.toMap().value("valueText") == "1";
            require(clamped, "loading a range warning did not write back the clamped value");
            const auto previous = app.diagnostics().size();
            app.addNode("test.unregistered", 0, 0);
            app.addNode("test.unregistered", 0, 0);
            require(app.diagnostics().size() == previous + 2, "same-text application events were deduplicated");
            require(app.diagnostic() == QStringLiteral("工具定义缺失：test.unregistered"),
                    "application diagnostics lost the known Chinese reason or stable tool ID");
            // The old prompt/JSON dialogs are gone; diagnostics stay in the in-memory buffer and
            // the single log window is the only surface that displays them.
            require(window->findChild<QObject *>("applicationDiagnosticDialog") == nullptr,
                    "the removed application diagnostic dialog is still instantiated");
            require(window->findChild<QObject *>("applicationDiagnosticJsonDialog") == nullptr,
                    "the removed application diagnostic JSON dialog is still instantiated");
            auto *logWindow = window->findChild<QObject *>("logWindow");
            require(logWindow != nullptr, "the log window is missing");
            require(!logWindow->property("visible").toBool(), "the log window must start hidden");
            require((logWindow->property("flags").toInt() & Qt::Dialog) == Qt::Dialog,
                    "the log window is not a dialog window");
            require(logWindow->property("modality").toInt() == Qt::NonModal,
                    "the log window must not be modal");
            require(logWindow->property("transientParent").value<QObject *>() == window,
                    "the log window is not transient for the main window");
            const auto findText = [](auto &&self, QQuickItem *parent, const QString &text) -> QQuickItem *
            {
                if (parent->isVisible() && parent->property("text").toString() == text)
                    return parent;
                for (auto *child : parent->childItems())
                    if (auto *found = self(self, child, text))
                        return found;
                return nullptr;
            };
            require(findText(findText, window->contentItem(), QStringLiteral("日志")) != nullptr,
                    "the footer has no log entry");
            visionrt::LogConfig logConfig;
            logConfig.uiCapacity        = 100;
            logConfig.uiBatchIntervalMs = 20;
            logConfig.uiBatchMaximum    = 50;
            auto &logService             = visionrt::LogService::instance();
            struct LogStop
            {
                ~LogStop() { visionrt::LogService::instance().shutdown(); }
            } logStop;
            require(logService.start(logConfig, directory.filePath(QStringLiteral("logs/window.log"))),
                    "cannot start the log service for the window contract");
            require(QMetaObject::invokeMethod(logWindow, "showAndRaise"), "cannot show the log window");
            require(QTest::qWaitFor([&] { return logWindow->property("visible").toBool(); }, 1000),
                    "the log window did not become visible");
            require(app.logModel() != nullptr, "the log window has no model");
            visionrt::LogService::write(visionrt::LogLevel::Info, QStringLiteral("窗口契约"),
                                        QStringLiteral("日志窗口可见行"));
            require(QTest::qWaitFor([&] { return app.logModel()->rowCount() >= 1; }, 3000),
                    "the visible log window did not receive a bounded batch");
            QTest::qWait(100);
            auto *logQuick = qobject_cast<QQuickWindow *>(logWindow);
            require(logQuick != nullptr, "the log window is not a quick window");
            require(logQuick->grabWindow().save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/log_window.png")),
                    "cannot capture the log window");
            require(QMetaObject::invokeMethod(logWindow, "close"), "cannot close the log window");
            require(QTest::qWaitFor([&] { return !logWindow->property("visible").toBool(); }, 1000),
                    "closing the log window did not hide it");
            const auto oversized = visionrt::testing::oversizedBmp();
            const auto imagePath = directory.filePath(QStringLiteral("超尺寸图像.bmp"));
            QFile      oversizedFile(imagePath);
            require(oversizedFile.open(QIODevice::WriteOnly)
                        && oversizedFile.write(reinterpret_cast<const char *>(oversized.data()), oversized.size())
                               == static_cast<qint64>(oversized.size()),
                    "cannot write native decode failure fixture");
            oversizedFile.close();
            app.selectNode("1");
            require(app.setParameterPath("path", false, QUrl::fromLocalFile(imagePath)),
                    "cannot select native decode failure input");
            auto *outputModel = qobject_cast<visionrt::OutputModel *>(app.outputModel());
            require(outputModel != nullptr, "main-window output model is missing");
            app.runNode();
            require(QTest::qWaitFor(
                        [&]
                        {
                            const auto status = outputModel->capture(QStringLiteral("__status_message"));
                            return !app.busy() && status && status->diagnostic && status->diagnostic->status.code != 0;
                        },
                        3000),
                    "native decode failure did not reach the main window");
            const auto decodeJson
                = visionrt::OutputModel::diagnosticText(*outputModel->capture(QStringLiteral("__status_message")));
            const auto decode = QJsonDocument::fromJson(decodeJson.toUtf8()).object();
            require(decode.value("code").toInt() == 1 && decode.value("node") == "1"
                        && decode.value("tool_id") == "visionrt.image.read" && decode.value("phase") == "execute"
                        && QDir::fromNativeSeparators(decode.value("message").toString()).contains(imagePath)
                        && decode.value("message").toString().contains(QStringLiteral("批次索引 0"))
                        && !decode.value("message").toString().contains("OpenCV")
                        && decode.value("detail").toString().contains("validateInputImageSize"),
                    "native exception lost its raw detail, identity or actual execution phase: "
                        + decodeJson.toStdString());
            for (const auto &entry : app.nodes())
                if (entry.toMap().value("id") == "1")
                {
                    const auto summary = entry.toMap().value("statusMessage").toString();
                    require(summary.count(QStringLiteral("节点 1（visionrt.image.read）执行：")) == 1
                                && !summary.contains("OpenCV")
                                && entry.toMap().value("statusDetail") == decode.value("detail").toString(),
                            "native exception has duplicated context or leaked detail in the node summary");
                }
            require(QTest::qWaitFor(
                        [&] {
                            return findText(findText, window->contentItem(),
                                            outputModel->get(1).value("preview").toString())
                                != nullptr;
                        },
                        1000),
                    "main-window result table did not display the native failure before interaction");
            // The data column copies its value. The raw native detail stays in the framework
            // diagnostic that logs and tests consume, and never leaks into the node summary.
            auto *statusCell
                = findText(findText, window->contentItem(), outputModel->get(1).value("preview").toString());
            require(statusCell != nullptr, "main-window status message data cell is missing");
            QTest::mouseClick(
                window, Qt::RightButton, Qt::NoModifier,
                statusCell->mapToScene(QPointF(statusCell->width() / 2, statusCell->height() / 2)).toPoint());
            auto       *decodeMenu = window->findChild<QObject *>(QStringLiteral("outputMenu"));
            const bool  copyReady  = QTest::qWaitFor(
                [&]
                {
                    auto *copy = decodeMenu ? decodeMenu->findChild<QObject *>(QStringLiteral("outputCopyAction"))
                                            : nullptr;
                    return copy && copy->property("enabled").toBool();
                },
                1000);
            require(copyReady, "the data column does not offer copy for a real failed value");
            require(findText(findText, window->contentItem(), QStringLiteral("查看诊断 JSON")) == nullptr,
                    "the removed diagnostic entry is still offered");
            require(decodeMenu->property("count").toInt() == 1, "the data column still offers more than copy");
            QMetaObject::invokeMethod(decodeMenu, "close", Qt::DirectConnection);
        }
    }
}
