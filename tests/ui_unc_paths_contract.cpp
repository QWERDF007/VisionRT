#include "WorkbenchFixtures.h"
#include "ui/AppController.h"
#include "ui/OutputModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QTest>
#include <iostream>
#include <stdexcept>

using namespace visionrt;

namespace {
void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}
}

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    testing::WorkbenchInstallation installation;
    const auto                     root = installation.directory();
    const auto fixtureDirectory = QStringLiteral(VISIONRT_SOURCE_DIR "/build/unc-fixture/中文 空格");
    require(QDir().mkpath(fixtureDirectory), "cannot create the UNC fixture directory");
    const auto localImage = fixtureDirectory + "/图 像.png";
    QImage     image(2, 2, QImage::Format_RGB888);
    image.fill(QColor(70, 70, 70));
    require(image.save(localImage), "cannot write the UNC fixture image");
    const QString sourceRoot = QStringLiteral(VISIONRT_SOURCE_DIR);
    const auto    uncRoot = QStringLiteral("\\\\localhost\\") + sourceRoot.left(1) + QStringLiteral("$")
                       + sourceRoot.mid(2);
    const auto uncImage = uncRoot + "/build/unc-fixture/中文 空格/图 像.png";
    if (!QFileInfo::exists(uncImage))
    {
        std::cout << "UI_UNC_PATHS_SKIP local-share-unavailable\n";
        return 77;
    }
    AppController app(1, root);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "UNC catalog did not become ready");
    app.addNode("visionrt.image.read", 0, 0);
    require(QTest::qWaitFor([&] { return !app.busy() && app.nodes().size() == 1; }, 5000),
            "UNC reader node was not created");
    require(app.setParameter("path", false, uncImage), "UNC path was rejected");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "UNC path did not settle");
    const auto projectDirectory = QStringLiteral(VISIONRT_SOURCE_DIR "/build/unc-project");
    require(QDir().mkpath(projectDirectory), "cannot create the UNC project directory");
    const auto projectPath = projectDirectory + "/unc.vrt.json";
    require(app.save(projectPath), "UNC project could not be saved");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "UNC project did not save");
    app.requestOpen(projectPath);
    if (!app.busy() && app.nodes().isEmpty())
        app.resolveUnsaved("discard");
    require(QTest::qWaitFor([&] { return !app.busy() && app.nodes().size() == 1; }, 5000),
            "UNC project did not reopen");
    app.selectNode(app.nodes().front().toMap().value("id").toString());
    require(QTest::qWaitFor(
                [&]
                {
                    for (const auto &value : app.parameters())
                        if (value.toMap().value("id").toString() == "path")
                            return true;
                    return false;
                },
                2000),
            "UNC path parameter did not appear after reopen");
    QVariantMap pathParameter;
    for (const auto &value : app.parameters())
        if (value.toMap().value("id").toString() == "path")
            pathParameter = value.toMap();
    require(pathParameter.value("valueText").toString() == uncImage,
            "UNC path was not preserved exactly after save/reopen: "
                + pathParameter.value("valueText").toString().toStdString());
    app.runNode();
    require(QTest::qWaitFor([&] { return !app.busy(); }, 10000), "UNC image read did not finish");
    auto *outputs = qobject_cast<OutputModel *>(app.outputModel());
    require(outputs != nullptr, "UNC output model is unavailable");
    const auto images = outputs->capture(QStringLiteral("images"));
    require(images && std::holds_alternative<DataRef<Image>>(images->value), "UNC image output is missing");
    const auto &batch = std::get<DataRef<Image>>(images->value).get();
    require(batch.size() == 1 && batch[0].at<std::uint8_t>(0, 0) == 70,
            "UNC image content does not match the fixture");
    std::cout << "UI_UNC_PATHS_PASS saved=1 reopened=1 executed=1 utf8=1\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
