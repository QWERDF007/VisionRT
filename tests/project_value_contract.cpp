#include "core/ProjectEditor.h"
#include "core/ProjectStorage.h"
#include "core/ToolRegistry.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

const NodeDefinition &node(const ProjectEditor &editor, NodeId id)
{
    const auto &nodes = editor.project().flows.front().nodes;
    return *std::find_if(nodes.begin(), nodes.end(), [id](const auto &value) { return value.id == id; });
}

// Writes one element of the stored array. The handle is shared by every copy, so this is exactly
// how a later draft edit would reach the committed literal.
std::int64_t &numberAt(const PortValue &value, std::size_t index)
{
    return std::get<Array>(value).elements<std::int64_t>()[index];
}

ToolDefinition configuredTool()
{
    ToolDefinition tool;
    tool.nameEn          = "test.project_values";
    tool.nameZh = "配置值";
    tool.version     = "1";
    tool.kind        = ToolKind::Cpp;
    tool.inputs.push_back({1, "数值",
        {Type::Array, DType::Int64,   2},
        Array::fromNested(NestedVector<std::int64_t, 2>{     {2, 9},           {}, {7}}
        )
    });
    tool.initParams.push_back({0, "区域", {Type::ROI}, DataRef<ROI>(ROI{{{2., 1., 2., 3., 4., 0.}}})});
    return tool;
}

void defaultsBelongToEachNode()
{
    const auto    tool = configuredTool();
    ProjectEditor editor;
    const auto    first                                             = editor.addNode("flow_1", tool, {});
    const auto    second                                            = editor.addNode("flow_1", tool, {1, 0});
    numberAt(node(editor, first).inputs.at(1).literal, 0) = 83;
    require(numberAt(tool.inputs.front().defaultValue, 0) == 2, "node edit changed declaration default");
    require(numberAt(node(editor, second).inputs.at(1).literal, 0) == 2,
            "node defaults share Array storage");
    std::get<DataRef<ROI>>(node(editor, first).initParams.at(0)).get()[0][0][1] = 91.;
    require(std::get<DataRef<ROI>>(tool.initParams.front().defaultValue).get()[0][0][1] == 1.,
            "node edit changed ROI default");
    require(std::get<DataRef<ROI>>(node(editor, second).initParams.at(0)).get()[0][0][1] == 1.,
            "node defaults share ROI storage");
}

void copiedConfigurationIsIndependent()
{
    ProjectEditor editor;
    const auto    source = editor.addNode("flow_1", configuredTool(), {});
    const auto    copied = editor.copySelection("flow_1", std::array{source});
    numberAt(node(editor, source).inputs.at(1).literal, 0) = 83;
    require(numberAt(copied.nodes.front().inputs.at(1).literal, 0) == 2,
            "clipboard changed after editing source configuration");
    const auto pasted = editor.pasteSelection("flow_1", copied, {1, 0}).at(source);
    numberAt(copied.nodes.front().inputs.at(1).literal, 0) = 54;
    require(numberAt(node(editor, pasted).inputs.at(1).literal, 0) == 2,
            "pasted node shares clipboard configuration storage");
    const auto repeated = editor.pasteSelection("flow_1", copied, {2, 0}).at(source);
    numberAt(node(editor, repeated).inputs.at(1).literal, 0) = 76;
    require(numberAt(copied.nodes.front().inputs.at(1).literal, 0) == 54,
            "repeated paste changed clipboard configuration");
    const auto  flowId = editor.copyFlow("flow_1");
    const auto &flows  = editor.project().flows;
    const auto &flow = *std::find_if(flows.begin(), flows.end(), [&](const auto &value) { return value.id == flowId; });
    numberAt(flow.nodes.front().inputs.at(1).literal, 0) = 68;
    require(numberAt(node(editor, source).inputs.at(1).literal, 0) == 83,
            "flow copy shares source configuration storage");
    std::get<DataRef<ROI>>(flow.nodes.front().initParams.at(0)).get()[0][0][1] = 95.;
    require(std::get<DataRef<ROI>>(node(editor, source).initParams.at(0)).get()[0][0][1] == 1.,
            "flow copy shares source ROI configuration storage");
}

void submittedDraftsDoNotRemainLive()
{
    ProjectEditor editor;
    const auto    id    = editor.addNode("flow_1", configuredTool(), {});
    PortValue     draft = Array::fromNested(NestedVector<std::int64_t, 2>{{31}, {}});
    require(editor.setLiteral("flow_1", id, 1, draft), "changed draft not submitted");
    numberAt(draft, 0) = 84;
    require(numberAt(node(editor, id).inputs.at(1).literal, 0) == 31,
            "later draft edit changed committed literal");
    PortValue regions = DataRef<ROI>(ROI{{{3., 2., 4., 7.}}});
    require(editor.setInitParam("flow_1", id, 0, regions), "changed ROI draft not submitted");
    std::get<DataRef<ROI>>(regions).get()[0][0][1] = 97.;
    require(std::get<DataRef<ROI>>(node(editor, id).initParams.at(0)).get()[0][0][1] == 2.,
            "later ROI draft edit changed committed initialization");
    editor.markSaved();
    const auto revision = node(editor, id).executionRevision;
    try
    {
        editor.setLiteral("flow_1", id, 1, Array::fromNested(NestedVector<bool, 2>{{true}}));
        throw std::runtime_error("incompatible draft accepted");
    }
    catch (const ContractError &)
    {
    }
    require(!editor.project().dirty && node(editor, id).executionRevision == revision,
            "rejected candidate changed project identity");
    require(numberAt(node(editor, id).inputs.at(1).literal, 0) == 31,
            "rejected candidate changed committed container");
}

void reconciledDefaultsAreIndependent()
{
    const auto   tool = configuredTool();
    ToolRegistry registry;
    registry.addDefinition(tool);
    ProjectEditor editor;
    editor.addNode("flow_1", tool, {});
    editor.addNode("flow_1", tool, {1, 0});
    auto missing = editor.project();
    for (auto &entry : missing.flows.front().nodes)
    {
        entry.inputs.clear();
        entry.initParams.clear();
        entry.signatures.inputs.clear();
        entry.signatures.initParams.clear();
    }
    const auto  restored                                 = ProjectStorage(registry).reconcile(missing);
    const auto &nodes                                    = restored.project.flows.front().nodes;
    numberAt(nodes[0].inputs.at(1).literal, 0) = 94;
    const auto *registered = registry.find(tool.nameEn);
    const auto  declared   = std::find_if(registered->inputs.begin(), registered->inputs.end(),
                                          [](const auto &port) { return port.id == 1; });
    require(declared != registered->inputs.end() && numberAt(declared->defaultValue, 0) == 2,
            "reconciliation changed registry defaults");
    require(numberAt(nodes[1].inputs.at(1).literal, 0) == 2, "reconciled nodes share Array defaults");
    std::get<DataRef<ROI>>(nodes[0].initParams.at(0)).get()[0][0][1] = 79.;
    require(std::get<DataRef<ROI>>(nodes[1].initParams.at(0)).get()[0][0][1] == 1.,
            "reconciled nodes share ROI defaults");
}

template<class Vector>
Vector sample()
{
    using Element = typename Vector::value_type;
    if constexpr (std::is_same_v<Element, std::string>)
        return {"中文/图像", ""};
    else if constexpr (std::is_same_v<Element, bool>)
        return {true, false};
    else if constexpr (std::is_integral_v<Element>)
        return {std::numeric_limits<Element>::min(), std::numeric_limits<Element>::max()};
    else if constexpr (std::is_floating_point_v<Element>)
        return {1.25, -2.5};
    else
        return {sample<Element>(), Element{}};
}

// One sample Array per supported signature: the readable nested form is the builder input, the
// stored value is the flat buffer the project file round-trips.
template<class Scalar>
Array sampleArrayOfRank(int rank)
{
    switch (rank)
    {
    case 1:
        return Array::fromNested(sample<NestedVector<Scalar, 1>>());
    case 2:
        return Array::fromNested(sample<NestedVector<Scalar, 2>>());
    case 3:
        return Array::fromNested(sample<NestedVector<Scalar, 3>>());
    case 4:
        return Array::fromNested(sample<NestedVector<Scalar, 4>>());
    }
    throw std::runtime_error("不支持的数组 rank");
}

Array sampleArray(const TypeSpec &type)
{
    switch (*type.dtype)
    {
    case DType::UInt8:
        return sampleArrayOfRank<std::uint8_t>(type.rank);
    case DType::Int32:
        return sampleArrayOfRank<std::int32_t>(type.rank);
    case DType::Int64:
        return sampleArrayOfRank<std::int64_t>(type.rank);
    case DType::Float32:
        return sampleArrayOfRank<float>(type.rank);
    case DType::Float64:
        return sampleArrayOfRank<double>(type.rank);
    case DType::Bool:
        return sampleArrayOfRank<bool>(type.rank);
    case DType::String:
        return sampleArrayOfRank<std::string>(type.rank);
    }
    throw std::runtime_error("未知数组 dtype");
}

void allValuesSurviveRealProjectFiles()
{
    QTemporaryDir directory;
    require(directory.isValid(), "temporary project directory unavailable");
    ToolDefinition tool;
    tool.nameEn          = "test.project_matrix";
    tool.nameZh = "工程类型矩阵";
    tool.version     = "1";
    tool.kind        = ToolKind::Cpp;
    require(Array::supportedTypes().size() == 28, "incomplete Array signature matrix");
    // The matrix tool numbers its inputs in declaration order; the note keeps the type visible.
    PortId nextInput = 1;
    for (const auto &type : Array::supportedTypes())
    {
        tool.inputs.push_back({nextInput, std::string(typeName(type.type)) + " 数组", type, sampleArray(type)});
        ++nextInput;
    }
    tool.inputs.push_back({nextInput++, "区域",
        {Type::ROI},
        DataRef<ROI>(ROI{{{0., 1.5, 2.5}, {1., 1., 2., 3., 4.}, {2., 1., 2., 3., 4., 0.}},
          {},
          {{3., 2., 4., 7.}, {4., 0., 0., 3., 0., 3., 2.}, {5., 1., 2., 3., 4.}}}
        )
    });
    tool.inputs.push_back({nextInput++, "图像", {Type::Image}, DataRef<Image>(Image{})});
    // A declaration without an explicit value is normalised to the type default before a node
    // ever stores it, so the saved project states 0 rather than a null placeholder.
    tool.inputs.push_back({nextInput++, "空值", {Type::Int}, Missing{}});
    ToolRegistry registry;
    registry.addDefinition(tool);
    ProjectEditor editor;
    editor.addNode("flow_1", *registry.find(tool.nameEn), {});
    auto           project  = editor.project();
    const auto     filename = directory.filePath("完整 工程.vrt.json");
    ProjectStorage storage(registry);
    storage.save(project, std::filesystem::path(filename.toStdWString()));
    require(!project.dirty, "successful project commit did not install saved identity");
    QFile file(filename);
    require(file.open(QIODevice::ReadOnly), "saved project unavailable");
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    require(root["format_version"].toInt() == 7, "saved project does not use format7");
    const auto saved    = root["flows"].toArray()[0].toObject()["nodes"].toArray()[0].toObject();
    // Inputs are an entry list keyed by port number, not an object keyed by a name.
    const auto literals = saved["inputs"].toArray();
    const auto entry    = [&](PortId port) -> QJsonObject
    {
        for (const auto &item : literals)
            if (item.toObject().value("port_id").toInt() == static_cast<int>(port))
                return item.toObject();
        return {};
    };
    const auto literal = entry(1)["literal"].toObject();
    require(literal.contains("values") && (literal.contains("shape") || literal.contains("offsets")),
            "saved Array must state its own shape or ragged offsets");
    require(entry(29)["literal"].toArray().size() == 3, "saved ROI groups lost");
    require(entry(30)["literal"].toArray().isEmpty(), "empty Image value lost");
    require(entry(31)["literal"].toInt() == 0, "type default not saved");
    const auto  reopened = storage.load(project.path);
    const auto &restored = reopened.project.flows.front().nodes.front();
    for (const auto &port : registry.find(tool.nameEn)->inputs)
    {
        require(restored.signatures.inputs.at(port.id) == port.type, "project type signature changed");
        require(equalValues(restored.inputs.at(port.id).literal, port.defaultValue), "project nested literal changed");
    }
    const auto selected = std::find_if(tool.inputs.begin(), tool.inputs.end(), [](const auto &port)
                                       { return port.type == TypeSpec{Type::Array, DType::Int64, 2}; });
    require(selected != tool.inputs.end(), "Int64 rank2 fixture missing");
    numberAt(restored.inputs.at(selected->id).literal, 0) = 77;
    require(numberAt(project.flows.front().nodes.front().inputs.at(selected->id).literal, 0)
                == std::numeric_limits<std::int64_t>::min(),
            "reopened project shares source containers");
}
}

int main()
try
{
    defaultsBelongToEachNode();
    copiedConfigurationIsIndependent();
    submittedDraftsDoNotRemainLive();
    reconciledDefaultsAreIndependent();
    allValuesSurviveRealProjectFiles();
    std::cout << "Project values own their configuration containers\n";
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
