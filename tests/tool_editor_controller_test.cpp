#include "core/CustomToolStore.h"
#include "ui/ToolEditorController.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace visionrt;

namespace {

QString readFile(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return QString::fromStdString(content);
}

bool treeHasFolder(const QVariantList &nodes, const QString &path)
{
    for (const auto &item : nodes)
    {
        const auto map = item.toMap();
        if (map.value("kind").toString() != QStringLiteral("folder"))
            continue;
        if (map.value("path").toString() == path)
            return true;
        if (treeHasFolder(map.value("children").toList(), path))
            return true;
    }
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tempDir;
    assert(tempDir.isValid());
    const std::filesystem::path root(tempDir.path().toStdWString());

    CustomToolStore store(root);
    ToolEditorController controller;
    controller.setCustomStore(&store);

    // 1. The editor offers exactly the type system it validates against.
    const auto types = controller.supportedTypes();
    assert(types.size() == 34);
    for (const auto &item : types)
    {
        const auto map = item.toMap();
        assert(!map.value("token").toString().isEmpty());
        assert(!map.value("label").toString().isEmpty());
    }

    // 2. Adding a tool produces a card immediately: no dialog, no file.
    assert(controller.addTool("自定义/图像"));
    assert(controller.isDirty());
    assert(controller.isEditable());
    assert(controller.currentToolName() == "新工具 1");
    assert(controller.currentToolId() == "custom.tool.1");
    assert(controller.currentToolCategory() == "自定义/图像");
    assert(treeHasFolder(controller.tree(), "自定义"));
    assert(treeHasFolder(controller.tree(), "自定义/图像"));
    assert(!std::filesystem::exists(root / "tools"));

    // The id stays editable until the first save, and the code package follows it.
    controller.setCurrentToolId("custom.test.threshold");
    assert(controller.currentToolId() == "custom.test.threshold");
    assert(QDir::fromNativeSeparators(controller.currentToolScript()).contains("custom/test/threshold/tool.py"));
    assert(controller.currentToolFile() == "tool.py");
    assert(controller.currentToolSymbol() == "execute");

    // 3. Ports of both stages live in the same draft.
    auto *inputs  = controller.inputModel();
    auto *outputs = controller.outputModel();
    assert(inputs->addPort("阈值", "int", "128"));
    assert(inputs->addPort("输入图像", "image"));
    assert(inputs->updatePort(2, {{"showImage", true}}));
    assert(outputs->addPort("输出图像", "image"));
    assert(outputs->updatePort(2, {{"showImage", true}}));

    // 4. A declaration preview is the same normalised JSON the store writes.
    const auto json = controller.previewJson();
    assert(json.contains("\"schema_version\": 6"));
    assert(json.contains("custom.test.threshold"));
    assert(json.contains("\"visible\": true"));
    assert(json.contains("next_port_id"));

    // 5. Saving commits every draft and locks the published id.
    assert(controller.saveAll());
    assert(!controller.isDirty());
    assert(controller.isEditable());
    assert(!controller.isToolIdEditable());
    assert(std::filesystem::exists(root / "tools" / "custom.test.threshold.json"));
    assert(std::filesystem::exists(root / "python" / "custom/test/threshold" / "tool.py"));
    controller.setCurrentToolId("custom.test.renamed");
    assert(controller.currentToolId() == "custom.test.threshold");

    // The generated script reports the declared outputs as missing under a not-implemented status.
    {
        const QString script = readFile(root / "python" / "custom/test/threshold" / "tool.py");
        assert(script.contains("2: None"));
        assert(script.contains("工具尚未实现"));
    }

    // 6. A second draft can be opened without leaving the first: switching never forces a save.
    assert(controller.addTool("自定义/图像"));
    assert(controller.currentToolName() == "新工具 2");
    controller.setCurrentToolId("custom.test.second");
    assert(outputs->addPort("结果", "int"));
    assert(controller.isDirty());

    controller.selectKey("custom.test.threshold");
    assert(controller.currentToolId() == "custom.test.threshold");
    assert(inputs->addPort("临时参数", "int", "0"));
    assert(controller.isDirty());

    controller.selectKey("custom.test.second");
    assert(controller.currentToolId() == "custom.test.second");
    assert(outputs->rowCount() == 3); // status code, status detail, result

    // 7. Cancel discards the whole window: no file for the second tool, first tool untouched.
    controller.cancelAll();
    assert(!controller.isDirty());
    assert(!std::filesystem::exists(root / "tools" / "custom.test.second.json"));
    {
        const QString declaration = readFile(root / "tools" / "custom.test.threshold.json");
        assert(!declaration.contains("临时参数"));
    }

    // 8. Deletion is also a draft: nothing is removed before Save.
    controller.selectKey("custom.test.threshold");
    assert(controller.deleteTool("custom.test.threshold"));
    assert(controller.isDirty());
    assert(std::filesystem::exists(root / "tools" / "custom.test.threshold.json"));
    controller.cancelAll();
    assert(std::filesystem::exists(root / "tools" / "custom.test.threshold.json"));

    // 9. Bounds are real declaration fields: a dirty draft must not be reported clean.
    controller.selectKey("custom.test.threshold");
    assert(!controller.isDirty());
    assert(inputs->updatePort(1, {{"min", QStringLiteral("0")}, {"max", QStringLiteral("255")}}));
    assert(controller.isDirty());
    assert(controller.saveAll());
    {
        const QString declaration = readFile(root / "tools" / "custom.test.threshold.json");
        assert(declaration.contains("\"min\": 0"));
        assert(declaration.contains("\"max\": 255"));
    }

    // 10. A loaded draft exposes host-reserved ports exactly once: the model projects the fixed
    // prefix itself, so the stored draft must not carry a second copy of port 0.
    controller.cancelAll();
    controller.selectKey("custom.test.threshold");
    assert(inputs->rowCount() == 3);
    assert(inputs->data(inputs->index(0, 0), ToolPortListModel::IdRole).toUInt() == 0);
    assert(inputs->data(inputs->index(0, 0), ToolPortListModel::IsFixedRole).toBool());
    assert(inputs->data(inputs->index(1, 0), ToolPortListModel::IdRole).toUInt() == 1);
    assert(!inputs->data(inputs->index(1, 0), ToolPortListModel::IsFixedRole).toBool());
    assert(inputs->data(inputs->index(2, 0), ToolPortListModel::IdRole).toUInt() == 2);

    // 11. Cloning carries the script as a draft; the copy is only written by Save.
    assert(controller.cloneTool("custom.test.threshold"));
    assert(controller.currentToolId() == "custom.test.threshold_copy1");
    assert(controller.isDirty());
    assert(!std::filesystem::exists(root / "tools" / "custom.test.threshold_copy1.json"));
    assert(controller.saveAll());
    assert(std::filesystem::exists(root / "tools" / "custom.test.threshold_copy1.json"));

    // 12. Folder edits are drafts too: renaming moves the tools, deleting a non-empty folder is
    // refused, and an empty folder can be added and removed without touching the disk.
    controller.cancelAll();
    assert(controller.addFolder("自定义", "示例工具"));
    assert(treeHasFolder(controller.tree(), "自定义/示例工具"));
    assert(controller.removeFolder("自定义/示例工具"));
    assert(!treeHasFolder(controller.tree(), "自定义/示例工具"));
    assert(!controller.removeFolder("自定义/图像"));

    assert(controller.renameFolder("自定义", "用户扩展"));
    assert(controller.isDirty());
    assert(treeHasFolder(controller.tree(), "用户扩展/图像"));
    assert(controller.saveAll());
    {
        const QString declaration = readFile(root / "tools" / "custom.test.threshold.json");
        assert(declaration.contains("用户扩展"));
        assert(!declaration.contains("自定义"));
    }

    std::cout << "[PASS] tool_editor_controller_test" << std::endl;
    return 0;
}
