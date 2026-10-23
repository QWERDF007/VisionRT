#include "WorkbenchFixtures.h"
#include "ui/AppController.h"
#include "ui/ToolTreeModel.h"

#include <QAbstractItemModel>
#include <QGuiApplication>
#include <QTest>
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <functional>
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

QModelIndex findToolIndex(QAbstractItemModel *model, const QString &id, const QModelIndex &parent = {})
{
    for (int row = 0; row < model->rowCount(parent); ++row)
    {
        const auto index = model->index(row, 0, parent);
        if (model->data(index, ToolTreeModel::ToolIdRole).toString() == id)
            return index;
        if (const auto nested = findToolIndex(model, id, index); nested.isValid())
            return nested;
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

    AppController controller(1, installation.directory());
    require(QTest::qWaitFor([&] { return !controller.busy() && !controller.toolDefinitions().isEmpty(); }, 5000),
            "tool catalog did not become ready");
    controller.addNode(QStringLiteral("custom.python.roi"), 0, 0);
    require(QTest::qWaitFor([&] { return !controller.busy() && controller.nodes().size() == 1; }, 5000),
            "available Python tool was not added before removal");
    const auto nodeId = controller.nodes().first().toMap().value("id").toString();
    controller.copyNodes(QVariantList{nodeId});
    require(controller.canPaste(), "copied node was not retained");

    assert(std::filesystem::remove(script));
    controller.refreshPython();
    require(QTest::qWaitFor(
                [&]
                {
                    const auto current = findDefinition(controller.toolDefinitions(), QStringLiteral("custom.python.roi"));
                    return !controller.busy() && !current.isEmpty() && current.value("available").toBool() == false;
                },
                5000),
            "missing-script Python declaration was not retained as unavailable");
    const auto roi = findDefinition(controller.toolDefinitions(), QStringLiteral("custom.python.roi"));
    require(roi.value("available").toBool() == false, "missing-script Python tool was reported available");
    const auto index = findToolIndex(controller.toolModel(), QStringLiteral("custom.python.roi"));
    require(index.isValid(), "missing-script Python declaration disappeared from the tool tree");
    require(controller.toolModel()->data(index, ToolTreeModel::AvailableRole).toBool() == false,
            "tool tree did not mark the missing-script Python tool unavailable");
    const auto diagnostics = controller.diagnostics();
    require(std::any_of(diagnostics.begin(), diagnostics.end(),
                        [](const QString &message) { return message.contains(QStringLiteral("Python 脚本缺失")); }),
            "missing-script diagnosis was not retained");
    require(controller.nodes().size() == 1, "refresh removed the existing node");

    controller.pasteNodes(0, 0);
    QCoreApplication::processEvents();
    require(controller.nodes().size() == 1, "unavailable clipboard node was pasted into the flow");

    controller.addNode(QStringLiteral("custom.python.roi"), 0, 0);
    QCoreApplication::processEvents();
    require(controller.nodes().size() == 1, "unavailable tool was added to the flow");

    controller.addNode(QStringLiteral("visionrt.image.read"), 0, 0);
    require(QTest::qWaitFor([&] { return !controller.busy() && controller.nodes().size() == 2; }, 5000),
            "available tool was not added");
    std::cout << "Unavailable Python declarations stay diagnostic-only; available tools remain addable\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
