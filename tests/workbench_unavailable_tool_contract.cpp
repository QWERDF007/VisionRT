#include "WorkbenchFixtures.h"
#include "ui/AppController.h"

#include <QGuiApplication>
#include <QTest>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

QVariantMap findDefinition(const QVariantList &definitions, const QString &id)
{
    for (const auto &item : definitions)
    {
        const auto map = item.toMap();
        if (map.value("id").toString() == id)
            return map;
    }
    return {};
}
}

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    testing::WorkbenchInstallation installation;
    const auto package = std::filesystem::path(installation.directory().toStdWString()) / "tools-package";
    const auto script  = package / "python/custom/examples/roi_geometry/tool.py";
    assert(std::filesystem::is_regular_file(script));
    assert(std::filesystem::remove(script));

    AppController controller(1, installation.directory());
    require(QTest::qWaitFor([&] { return !controller.busy() && !controller.toolDefinitions().isEmpty(); }, 5000),
            "tool catalog did not become ready");
    const auto definitions = controller.toolDefinitions();
    const auto roi         = findDefinition(definitions, QStringLiteral("custom.python.roi"));
    require(!roi.isEmpty(), "missing-script Python declaration was not retained");
    require(roi.value("available").toBool() == false, "missing-script Python tool was reported available");
    require(controller.nodes().isEmpty(), "initial flow unexpectedly contains nodes");

    controller.addNode(QStringLiteral("custom.python.roi"), 0, 0);
    QCoreApplication::processEvents();
    require(controller.nodes().isEmpty(), "unavailable tool was added to the flow");

    controller.addNode(QStringLiteral("visionrt.image.read"), 0, 0);
    require(QTest::qWaitFor([&] { return !controller.busy() && controller.nodes().size() == 1; }, 5000),
            "available tool was not added");
    std::cout << "Unavailable Python declarations stay diagnostic-only; available tools remain addable\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
