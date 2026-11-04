#include "core/CustomToolStore.h"
#include "ui/ToolEditorController.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <cassert>
#include <fstream>
#include <iostream>

using namespace visionrt;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tempDir;
    assert(tempDir.isValid());
    const std::filesystem::path root(tempDir.path().toStdWString());

    CustomToolStore store(root);
    ToolEditorController controller;
    controller.setCustomStore(&store);

    // 1. Verify 34 supported types
    const auto types = controller.supportedTypes();
    assert(types.size() == 34);
    for (const auto &item : types)
    {
        const auto map = item.toMap();
        assert(!map.value("token").toString().isEmpty());
        assert(!map.value("label").toString().isEmpty());
    }

    // 2. Create new tool
    assert(controller.newTool("测试阈值", "custom.test.threshold", "python_function", "自定义/图像"));
    assert(controller.isDirty());
    assert(controller.isEditable());
    assert(controller.currentToolId() == "custom.test.threshold");
    assert(controller.currentToolName() == "测试阈值");
    assert(controller.currentToolKind() == "python_function");

    // Starting another draft would silently drop this one, so the editor refuses until the draft is
    // applied or reverted.
    assert(!controller.newTool("另一个工具", "custom.test.other", "python_function", "自定义"));
    assert(controller.currentToolId() == "custom.test.threshold");

    // The id stays editable until the first commit, and the code package follows it.
    controller.setCurrentToolId("custom.test.threshold.v2");
    assert(controller.currentToolId() == "custom.test.threshold.v2");
    assert(controller.isToolIdEditable());
    assert(QDir::fromNativeSeparators(controller.currentToolScript()).contains("custom/test/threshold/v2"));
    controller.setCurrentToolId("custom.test.threshold");
    assert(controller.currentToolId() == "custom.test.threshold");

    // 3. Add ports to Inputs
    controller.setCurrentStage("inputs");
    auto *portModel = controller.portModel();
    assert(portModel != nullptr);
    assert(portModel->addPort("阈值", "int", "128"));
    assert(portModel->addPort("输入图像", "image"));

    // 4. Add port to Outputs
    controller.setCurrentStage("outputs");
    assert(portModel->addPort("输出图像", "image"));

    // 5. Preview JSON
    const auto json = controller.previewJson();
    assert(json.contains("\"schema_version\": 6"));
    assert(json.contains("custom.test.threshold"));
    assert(json.contains("next_port_id"));

    // 6. Apply
    assert(controller.apply());
    assert(!controller.isDirty());
    assert(controller.isEditable());
    assert(!controller.isToolIdEditable());
    assert(std::filesystem::exists(root / "tools" / "custom.test.threshold.json"));
    assert(std::filesystem::exists(root / "python" / "custom/test/threshold" / "tool.py"));

    // A published id names the declaration, the package and every project binding, so it locks.
    controller.setCurrentToolId("custom.test.renamed");
    assert(controller.currentToolId() == "custom.test.threshold");

    // An applied tool owns its id: a second tool may not publish over its declaration and code.
    assert(!controller.newTool("重复标识", "custom.test.threshold", "python_function", "自定义"));

    // The generated script reports the declared outputs as missing under a not-implemented status.
    {
        std::ifstream script(root / "python" / "custom/test/threshold" / "tool.py");
        const std::string content((std::istreambuf_iterator<char>(script)), std::istreambuf_iterator<char>());
        assert(content.find("2: None") != std::string::npos);
        assert(content.find("工具尚未实现") != std::string::npos);
    }

    // Tools list should include our new tool
    const auto toolList = controller.tools();
    assert(toolList.size() >= 1);
    bool found = false;
    for (const auto &t : toolList)
    {
        if (t.toMap().value("id").toString() == "custom.test.threshold")
        {
            found = true;
            assert(t.toMap().value("isEditable").toBool() == true);
        }
    }
    assert(found);

    // 7. Clone tool
    assert(controller.cloneTool("custom.test.threshold"));
    assert(controller.tools().size() >= 2);

    // 8. Delete tool
    assert(controller.deleteTool("custom.test.threshold_copy1"));

    // 9. Revert changes
    controller.selectTool("custom.test.threshold");
    controller.setCurrentStage("inputs");
    assert(portModel->addPort("临时参数", "int", "0"));
    assert(controller.isDirty());
    controller.revert();
    assert(!controller.isDirty());

    // 10. A loaded draft exposes host-reserved ports exactly once: the model projects the fixed
    // prefix itself, so the stored draft must not carry a second copy of port 0.
    controller.selectTool("custom.test.threshold");
    controller.setCurrentStage("inputs");
    assert(portModel->rowCount() == 3);
    assert(portModel->data(portModel->index(0, 0), ToolPortListModel::IdRole).toUInt() == 0);
    assert(portModel->data(portModel->index(0, 0), ToolPortListModel::IsFixedRole).toBool());
    assert(portModel->data(portModel->index(1, 0), ToolPortListModel::IdRole).toUInt() == 1);
    assert(!portModel->data(portModel->index(1, 0), ToolPortListModel::IsFixedRole).toBool());
    assert(portModel->data(portModel->index(2, 0), ToolPortListModel::IdRole).toUInt() == 2);

    // 11. Editing a constraint is a real draft change: the apply path must not report a clean draft
    // while the declaration on disk still holds the old bounds.
    assert(!controller.isDirty());
    assert(portModel->updatePort(1, {{"min", QStringLiteral("0")}, {"max", QStringLiteral("255")}}));
    assert(controller.isDirty());

    // 12. Cloning twice must never overwrite the first copy, and a clone never silently drops the
    // edits the active draft holds: the draft has to be applied or reverted first.
    assert(!controller.cloneTool("custom.test.threshold")); // step 11 left the draft dirty
    controller.revert();
    assert(!controller.isDirty());
    assert(controller.cloneTool("custom.test.threshold"));
    const auto firstCopy = controller.currentToolId();
    assert(firstCopy == "custom.test.threshold_copy1");
    assert(controller.cloneTool("custom.test.threshold"));
    assert(controller.currentToolId() == "custom.test.threshold_copy2");
    bool firstSurvived = false;
    for (const auto &item : controller.tools())
        if (item.toMap().value("id").toString() == firstCopy)
            firstSurvived = true;
    assert(firstSurvived);

    std::cout << "[PASS] tool_editor_controller_test" << std::endl;
    return 0;
}
