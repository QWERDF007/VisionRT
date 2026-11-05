#include "ui/ToolPortListModel.h"

#include <QCoreApplication>
#include <cassert>
#include <iostream>

using namespace visionrt;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    ToolDefinition def;
    def.schemaVersion    = 6;
    def.nameEn           = "custom.test.ports";
    def.nameZh           = "端口测试工具";
    def.kind             = ToolKind::PythonFunction;
    def.nextInputPortId  = 1;
    def.nextOutputPortId = 2;
    def.nextInitPortId   = 0;

    ToolPortListModel model;

    // 1. Stage: Inputs
    model.setDraft(&def, PortStage::Inputs, true);
    // Fixed row 0 must be present: "上层工具状态"
    assert(model.rowCount() == 1);
    assert(model.fixedRowCount() == 1);
    assert(model.isFixedRow(0));
    assert(model.data(model.index(0, 0), ToolPortListModel::IdRole).toUInt() == 0);
    assert(model.data(model.index(0, 0), ToolPortListModel::NameZhRole).toString() == "上层工具状态");
    assert(model.data(model.index(0, 0), ToolPortListModel::IsFixedRole).toBool() == true);

    // Rejects removing or moving fixed row
    assert(!model.removePort(0));
    assert(!model.movePort(0, 1));
    assert(!model.updatePort(0, {{"nameZh", "非法改名"}}));

    // 2. Add business ports to Inputs
    assert(model.addPort("阈值", "int", "128"));
    assert(model.rowCount() == 2);
    assert(!model.isFixedRow(1));
    assert(model.data(model.index(1, 0), ToolPortListModel::IdRole).toUInt() == 1);
    assert(model.data(model.index(1, 0), ToolPortListModel::NameZhRole).toString() == "阈值");
    assert(def.nextInputPortId == 2);

    assert(model.addPort("输入图像", "image"));
    assert(model.rowCount() == 3);
    assert(model.data(model.index(2, 0), ToolPortListModel::IdRole).toUInt() == 2);
    assert(def.nextInputPortId == 3);

    // The workbench image selector is an author choice stored in the declaration, only for Image.
    assert(!model.updatePort(1, {{"showImage", true}}));
    assert(model.updatePort(2, {{"showImage", true}}));
    assert(model.data(model.index(2, 0), ToolPortListModel::ShowImageRole).toBool());
    assert(model.portAt(2).value("showImage").toBool());
    // Retyping away from Image clears the display choice with the rest of the image fields.
    assert(model.changeType(2, "int"));
    assert(!def.inputs[1].imageVisible);

    // 3. Move row
    // Moving business row 2 to 1
    assert(model.movePort(2, 1));
    // Now row 1 is "输入图像" (id 2), row 2 is "阈值" (id 1)
    assert(model.data(model.index(1, 0), ToolPortListModel::IdRole).toUInt() == 2);
    assert(model.data(model.index(2, 0), ToolPortListModel::IdRole).toUInt() == 1);

    // Rejects moving into or before fixed prefix (row 0)
    assert(!model.movePort(1, 0));

    // 4. Delete port & verify non-recycling
    // Delete row 1 (which is id 2, the current max id)
    assert(model.removePort(1));
    assert(model.rowCount() == 2);
    // Next port ID must NOT be decremented! It stays at 3!
    assert(def.nextInputPortId == 3);

    // Add another port -> must get ID 3, NOT recycling ID 2!
    assert(model.addPort("掩模", "image"));
    assert(model.rowCount() == 3);
    assert(model.data(model.index(2, 0), ToolPortListModel::IdRole).toUInt() == 3);
    assert(def.nextInputPortId == 4);

    // 5. Stage: Outputs
    model.setDraft(&def, PortStage::Outputs, true);
    // Outputs has 2 fixed rows: 0 (状态码), 1 (状态信息)
    assert(model.rowCount() == 2);
    assert(model.fixedRowCount() == 2);
    assert(model.isFixedRow(0) && model.isFixedRow(1));
    assert(model.data(model.index(0, 0), ToolPortListModel::IdRole).toUInt() == 0);
    assert(model.data(model.index(1, 0), ToolPortListModel::IdRole).toUInt() == 1);

    // Add business output -> start at nextOutputPortId = 2
    assert(model.addPort("输出图像", "image"));
    assert(model.rowCount() == 3);
    assert(model.data(model.index(2, 0), ToolPortListModel::IdRole).toUInt() == 2);
    assert(def.nextOutputPortId == 3);

    // 6. Stage: Init — only a class tool has a lifecycle, so the init stage is exercised on one.
    ToolDefinition initDef;
    initDef.schemaVersion   = 6;
    initDef.nameEn          = "custom.test.init";
    initDef.nameZh          = "初始化测试";
    initDef.kind            = ToolKind::PythonClass;
    initDef.nextInitPortId  = 0;
    model.setDraft(&initDef, PortStage::Init, true);
    // Init has 0 fixed rows
    assert(model.rowCount() == 0);
    assert(model.fixedRowCount() == 0);
    assert(model.addPort("初始参数", "int", "0"));
    assert(model.rowCount() == 1);
    assert(model.data(model.index(0, 0), ToolPortListModel::IdRole).toUInt() == 0);
    assert(initDef.nextInitPortId == 1);

    // 7. A type change reconciles the draft: values and constraints the new type cannot hold are
    // cleared, and every reference to the retyped port that no longer type-checks goes with it.
    ToolDefinition retype;
    retype.schemaVersion    = 6;
    retype.nameEn           = "custom.test.retype";
    retype.nameZh           = "改型测试";
    retype.kind             = ToolKind::PythonFunction;
    retype.nextInputPortId  = 1;
    retype.nextOutputPortId = 2;

    ToolPortListModel retypeModel;
    retypeModel.setDraft(&retype, PortStage::Inputs, true);
    assert(retypeModel.addPort("阈值", "int", "128"));
    assert(retypeModel.updatePort(1, {{"min", QStringLiteral("0")}, {"max", QStringLiteral("255")}}));
    assert(retypeModel.addPort("检测区域", "roi"));
    assert(retypeModel.addPort("输入图像", "image"));
    assert(retypeModel.addPort("参考区域", "roi"));
    // Both ROI inputs reference the same image port, so retyping it must clear both references.
    retype.inputs[1].image = PortRef{PortStage::Inputs, 3};
    retype.inputs[3].image = PortRef{PortStage::Inputs, 3};

    const auto thresholdImpact = retypeModel.typeChangeImpact(1, QStringLiteral("string"));
    assert(thresholdImpact.contains(QStringLiteral("最小值")));
    assert(thresholdImpact.contains(QStringLiteral("最大值")));
    assert(retypeModel.changeType(1, QStringLiteral("string")));
    assert(retype.inputs[0].type == TypeSpec{Type::String});
    assert(!retype.inputs[0].minimum.has_value());
    assert(!retype.inputs[0].maximum.has_value());
    assert(equalValues(retype.inputs[0].defaultValue, PortValue{std::string{}}));

    const auto imageImpact = retypeModel.typeChangeImpact(3, QStringLiteral("string"));
    assert(imageImpact.contains(QStringLiteral("端口 2 的图像关联")));
    assert(imageImpact.contains(QStringLiteral("端口 4 的图像关联")));
    assert(retypeModel.changeType(3, QStringLiteral("string")));
    assert(!retype.inputs[1].image.has_value());
    assert(!retype.inputs[3].image.has_value());

    // 8. An installed tool is read-only: the list shows it but refuses every mutation, so the
    // editor can never overwrite a definition it does not own.
    retypeModel.setDraft(&retype, PortStage::Inputs, false);
    assert(!retypeModel.addPort("越权参数", "int", "0"));
    assert(!retypeModel.updatePort(1, {{"nameZh", QStringLiteral("越权改名")}}));
    assert(!retypeModel.removePort(1));
    assert(!retypeModel.changeType(1, QStringLiteral("int")));

    // 9. Relations are edited against real candidates: the editor can only pick a reference the
    // declaration accepts, so a dangling or mistyped relation never reaches the draft.
    ToolDefinition rel;
    rel.schemaVersion    = 6;
    rel.nameEn           = "custom.test.relations";
    rel.nameZh           = "关系测试";
    rel.kind             = ToolKind::PythonFunction;
    rel.nextInputPortId  = 1;
    rel.nextOutputPortId = 2;

    ToolPortListModel relModel;
    relModel.setDraft(&rel, PortStage::Inputs, true);
    assert(relModel.addPort("输入图像", "image"));                  // id 1
    assert(relModel.addPort("检测区域", "roi"));                    // id 2
    assert(relModel.addPort("对位数据", "vec_vec_vec_float"));    // id 3
    assert(relModel.addPort("根目录", "string"));                   // id 4
    assert(relModel.addPort("模式", "string"));                     // id 5

    // The editor list follows the row's type; "path" is what makes a path mode meaningful.
    assert(relModel.updatePort(4, {{"editor", QStringLiteral("path")}}));
    assert(rel.inputs[3].editor == "path");
    assert(!relModel.updatePort(5, {{"editor", QStringLiteral("rect")}}));

    const auto imageCandidates = relModel.relationCandidates(2, QStringLiteral("image"));
    assert(imageCandidates.size() == 1);
    assert(imageCandidates[0].toMap().value("ref").toString() == "inputs.1");

    assert(relModel.updatePort(2, {{"image", QStringLiteral("inputs.1")}}));
    assert(relModel.portAt(2).value("imageRef").toString() == "inputs.1");
    // A relation that the declaration would reject is refused, and the old target stays.
    assert(!relModel.updatePort(2, {{"image", QStringLiteral("inputs.2")}}));
    assert(!relModel.updatePort(2, {{"image", QStringLiteral("inputs.3")}}));
    assert(rel.inputs[1].image.has_value());
    assert((*rel.inputs[1].image == PortRef{PortStage::Inputs, 1}));

    assert(relModel.relationCandidates(2, QStringLiteral("alignment")).size() == 1);
    assert(relModel.updatePort(2, {{"alignment", QStringLiteral("inputs.3")}}));
    assert(rel.inputs[1].alignment.has_value());

    assert(relModel.relationCandidates(4, QStringLiteral("path_mode")).size() == 1);
    assert(relModel.updatePort(4, {{"path_mode", QStringLiteral("inputs.5")}}));
    assert(rel.inputs[3].pathModeFrom.has_value());
    assert(!relModel.updatePort(4, {{"path_mode", QStringLiteral("inputs.1")}}));

    // A String value is edited as its own text, and candidate values are declared one per line.
    assert(relModel.updatePort(5, {{"defaultValue", QStringLiteral("自动")}}));
    assert(equalValues(rel.inputs[4].defaultValue, PortValue{std::string{"自动"}}));
    assert(relModel.updatePort(5, {{"options", QStringLiteral("自动\n手动")}}));
    assert(rel.inputs[4].options.size() == 2);
    assert(equalValues(rel.inputs[4].options[0], PortValue{std::string{"自动"}}));
    assert(relModel.updatePort(5, {{"options", QString()}}));
    assert(rel.inputs[4].options.empty());

    // 10. Deleting a port reconciles what named it. The image input is the target of a relation and
    // of the alignment of another row; removing it must not leave a declaration that only fails at
    // apply time, and the loss has to be visible to the author.
    QString clearedMessage;
    QObject::connect(&relModel, &ToolPortListModel::cleared,
                     [&clearedMessage](const QString &message) { clearedMessage = message; });
    assert(relModel.removePort(1)); // 输入图像, id 1
    assert(rel.inputs.size() == 4);
    const auto findInput = [&rel](PortId id) -> const PortDefinition * {
        for (const auto &port : rel.inputs)
            if (port.id == id)
                return &port;
        return nullptr;
    };
    assert(!findInput(1));
    const auto *roi = findInput(2);
    assert(roi && !roi->image.has_value());
    assert(roi && roi->alignment.has_value());
    assert((*roi->alignment == PortRef{PortStage::Inputs, 3}));
    // The path mode pointed at a String that still exists, so it survives the deletion.
    const auto *root = findInput(4);
    assert(root && root->pathModeFrom.has_value());
    assert(clearedMessage.contains(QStringLiteral("端口 2 的图像关联")));

    // A host port cannot be erased, and a parameter a type_from output derives from is refused
    // instead of being removed from under a declaration that still needs it.
    bool reservedRefused = false;
    try
    {
        erasePort(rel, PortStage::Inputs, 0);
    }
    catch (const ContractError &)
    {
        reservedRefused = true;
    }
    assert(reservedRefused);

    ToolDefinition hostDef;
    hostDef.kind = ToolKind::Host;
    hostDef.nameEn = "custom.test.host";
    hostDef.initParams.push_back(PortDefinition{.id = 0, .nameZh = "流程", .type = TypeSpec{Type::String}});
    hostDef.outputs.push_back(PortDefinition{.id = 2, .nameZh = "结果", .type = TypeSpec{Type::Image}});
    hostDef.outputs.back().typeFrom = PortTypeReference{
        .flowParameter = {PortStage::Init, 0}, .nodeParameter = {PortStage::Init, 0}, .portParameter = {PortStage::Init, 0}};
    bool derivedRefused = false;
    try
    {
        erasePort(hostDef, PortStage::Init, 0);
    }
    catch (const ContractError &)
    {
        derivedRefused = true;
    }
    assert(derivedRefused);
    assert(hostDef.initParams.size() == 1);

    // 11. A function tool has no lifecycle: the init stage refuses new rows instead of letting the
    // author build a declaration the validator rejects at apply time.
    ToolPortListModel initModel;
    initModel.setDraft(&def, PortStage::Init, true);
    assert(!initModel.addPort("初始化参数", "int", "0"));
    assert(def.initParams.empty());

    ToolDefinition classDef;
    classDef.schemaVersion = 6;
    classDef.nameEn        = "custom.test.cls";
    classDef.nameZh        = "类工具";
    classDef.kind          = ToolKind::PythonClass;
    classDef.nextInitPortId = 0;
    ToolPortListModel classInitModel;
    classInitModel.setDraft(&classDef, PortStage::Init, true);
    assert(classInitModel.addPort("初始化参数", "int", "0"));
    assert(classDef.initParams.size() == 1);
    assert(classDef.initParams[0].id == 0);

    std::cout << "[PASS] tool_port_list_model_test" << std::endl;
    return 0;
}
