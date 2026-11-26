#include "ui/OutputModel.h"
#include "ui/ParameterListModel.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <iostream>
#include <stdexcept>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

ParameterListModel::Row makeParamRow(const QString &nodeId, int id, int tab, const QString &group,
                                     const QString &displayText, const QString &editText)
{
    const QString key = ParameterListModel::makeKey(nodeId, static_cast<int>(PortStage::Inputs), false, {}, {}, id);
    QVariantMap   map{
        {         "key",                                 key},
        {          "id",                                  id},
        {       "stage", static_cast<int>(PortStage::Inputs)},
        {"recordScoped",                               false},
        {   "datasetId",                          QVariant{}},
        {    "recordId",                           QString{}},
        {         "tab",                                 tab},
        {       "group",                               group},
        { "displayText",                         displayText},
        {    "editText",                            editText}
    };
    return {key, nodeId, group, tab, std::move(map)};
}
} // namespace

int main(int argc, char **argv)
try
{
    QCoreApplication app(argc, argv);

    // 1. ParameterListModel incremental sync & O(1) updateRowByKey
    ParameterListModel params;
    QSignalSpy         paramResets(&params, &QAbstractItemModel::modelReset);
    QSignalSpy         paramGroups(&params, &ParameterListModel::groupsChanged);
    QSignalSpy         paramChanged(&params, &QAbstractItemModel::dataChanged);

    auto row1 = makeParamRow("1", 1, 0, "输入数据", "1.123457", "1.123456789");
    auto row2 = makeParamRow("1", 2, 0, "检测区域", "[[[10,20,30,40,0]]]", "[[[10,20,30,40,0]]]");
    params.sync({row1, row2});
    require(paramResets.count() == 1 && paramGroups.count() == 1 && params.rowCount() == 2,
            "initial sync did not populate ParameterListModel");
    require(params.rowByKey(row1.key).value("displayText").toString() == "1.123457"
                && params.rowByKey(row1.key).value("editText").toString() == "1.123456789",
            "float parameter lost dual displayText/editText representation");

    // Same topology, only row1 value changes -> 0 resets, 0 groupsChanged, 1 dataChanged on row 0
    paramResets.clear();
    paramGroups.clear();
    paramChanged.clear();
    auto row1Updated = makeParamRow("1", 1, 0, "输入数据", "2.5", "2.5");
    params.sync({row1Updated, row2});
    require(paramResets.isEmpty() && paramGroups.isEmpty(),
            "same-topology parameter value edit triggered modelReset or groupsChanged");
    require(paramChanged.count() == 1 && paramChanged.first().at(0).toModelIndex().row() == 0
                && paramChanged.first().at(1).toModelIndex().row() == 0,
            "same-topology parameter edit did not target only the changed row");

    // O(1) single-row ROI preview update -> 0 resets, 0 groupsChanged, 1 dataChanged on row 1
    paramChanged.clear();
    auto row2Preview = row2.map;
    row2Preview.insert("displayText", "[[[15,25,30,40,0]]]");
    require(params.updateRowByKey(row2.key, row2Preview), "updateRowByKey rejected valid key");
    require(paramResets.isEmpty() && paramGroups.isEmpty() && paramChanged.count() == 1
                && paramChanged.first().at(0).toModelIndex().row() == 1,
            "updateRowByKey did not emit targeted dataChanged");
    require(!params.updateRowByKey(row2.key, row2Preview), "identical updateRowByKey emitted redundant notification");

    // Topology change (new node) -> triggers reset + groupsChanged
    auto rowOther = makeParamRow("2", 1, 0, "输入数据", "9", "9");
    params.sync({rowOther});
    require(paramResets.count() == 1 && paramGroups.count() == 1 && params.rowCount() == 1,
            "node switch did not reset ParameterListModel");

    // 2. OutputModel incremental dataChanged when port topology is unchanged
    ToolDefinition tool;
    tool.outputs = {
        {2, "数量", {Type::Int}},
        {3, "分数", {Type::Float}}
    };
    auto snap1 = std::make_shared<NodeSnapshot>();
    snap1->publication = 1;
    snap1->outputs     = {{2, std::int64_t{10}}, {3, 0.5}};
    OutputModel outputs;
    QSignalSpy  outResets(&outputs, &QAbstractItemModel::modelReset);
    QSignalSpy  outChanged(&outputs, &QAbstractItemModel::dataChanged);

    outputs.setResult(&tool, snap1, {});
    require(outResets.count() == 1 && outputs.rowCount() == 4, "initial output setResult did not reset");
    outResets.clear();
    outChanged.clear();

    // Loop frame 2: same port topology, only port 2 changes -> 0 resets, 1 dataChanged on port 2 row (index 2)
    auto snap2         = std::make_shared<NodeSnapshot>();
    snap2->publication = 2;
    snap2->outputs     = {{2, std::int64_t{11}}, {3, 0.5}};
    outputs.setResult(&tool, snap2, {});
    require(outResets.isEmpty(), "OutputModel reset on unchanged port topology");
    require(outChanged.count() == 1 && outChanged.first().at(0).toModelIndex().row() == 2,
            "OutputModel did not emit targeted dataChanged for changed output port");
    require(outputs.capture(2)->publication == 2 && std::get<std::int64_t>(outputs.capture(2)->value) == 11,
            "OutputModel did not update captured value and publication");

    // Switching to another tool with different output ports triggers modelReset
    ToolDefinition otherTool;
    otherTool.outputs = {{5, "标签", {Type::String}}};
    outputs.setResult(&otherTool, nullptr, {});
    require(outResets.count() == 1 && outputs.rowCount() == 3,
            "OutputModel did not reset when output port topology changed");

    std::cout << "PARAMETER_MODEL_CONTRACT_PASS\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
