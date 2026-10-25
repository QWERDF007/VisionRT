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

NestedVector<std::int64_t, 2> &numbers(const PortValue &value)
{
    return std::get<Array>(value).get<NestedVector<std::int64_t, 2>>();
}

ToolDefinition configuredTool()
{
    ToolDefinition tool;
    tool.id          = "test.project_values";
    tool.displayName = "配置值";
    tool.version     = "1";
    tool.kind        = ToolKind::Cpp;
    tool.inputs.push_back({
        "numbers",
        "数值",
        {Type::Array, DType::Int64,   2},
        Array(NestedVector<std::int64_t, 2>{     {2, 9},           {}, {7}}
        )
    });
    tool.initParams.push_back({"regions", "区域", {Type::ROI}, DataRef<ROI>(ROI{{{2., 1., 2., 3., 4., 0.}}})});
    return tool;
}

void defaultsBelongToEachNode()
{
    const auto    tool = configuredTool();
    ProjectEditor editor;
    const auto    first                                             = editor.addNode("flow_1", tool, {});
    const auto    second                                            = editor.addNode("flow_1", tool, {1, 0});
    numbers(node(editor, first).inputs.at("numbers").literal)[0][0] = 83;
    require(numbers(tool.inputs.front().defaultValue)[0][0] == 2, "node edit changed declaration default");
    require(numbers(node(editor, second).inputs.at("numbers").literal)[0][0] == 2, "node defaults share Array storage");
    std::get<DataRef<ROI>>(node(editor, first).initParams.at("regions")).get()[0][0][1] = 91.;
    require(std::get<DataRef<ROI>>(tool.initParams.front().defaultValue).get()[0][0][1] == 1.,
            "node edit changed ROI default");
    require(std::get<DataRef<ROI>>(node(editor, second).initParams.at("regions")).get()[0][0][1] == 1.,
            "node defaults share ROI storage");
}

void copiedConfigurationIsIndependent()
{
    ProjectEditor editor;
    const auto    source = editor.addNode("flow_1", configuredTool(), {});
    const auto    copied = editor.copySelection("flow_1", std::array{source});
    numbers(node(editor, source).inputs.at("numbers").literal)[0][0] = 83;
    require(numbers(copied.nodes.front().inputs.at("numbers").literal)[0][0] == 2,
            "clipboard changed after editing source configuration");
    const auto pasted = editor.pasteSelection("flow_1", copied, {1, 0}).at(source);
    numbers(copied.nodes.front().inputs.at("numbers").literal)[0][0] = 54;
    require(numbers(node(editor, pasted).inputs.at("numbers").literal)[0][0] == 2,
            "pasted node shares clipboard configuration storage");
    const auto repeated = editor.pasteSelection("flow_1", copied, {2, 0}).at(source);
    numbers(node(editor, repeated).inputs.at("numbers").literal)[0][0] = 76;
    require(numbers(copied.nodes.front().inputs.at("numbers").literal)[0][0] == 54,
            "repeated paste changed clipboard configuration");
    const auto  flowId = editor.copyFlow("flow_1");
    const auto &flows  = editor.project().flows;
    const auto &flow = *std::find_if(flows.begin(), flows.end(), [&](const auto &value) { return value.id == flowId; });
    numbers(flow.nodes.front().inputs.at("numbers").literal)[0][0] = 68;
    require(numbers(node(editor, source).inputs.at("numbers").literal)[0][0] == 83,
            "flow copy shares source configuration storage");
    std::get<DataRef<ROI>>(flow.nodes.front().initParams.at("regions")).get()[0][0][1] = 95.;
    require(std::get<DataRef<ROI>>(node(editor, source).initParams.at("regions")).get()[0][0][1] == 1.,
            "flow copy shares source ROI configuration storage");
}

void submittedDraftsDoNotRemainLive()
{
    ProjectEditor editor;
    const auto    id    = editor.addNode("flow_1", configuredTool(), {});
    PortValue     draft = Array(NestedVector<std::int64_t, 2>{{31}, {}});
    require(editor.setLiteral("flow_1", id, "numbers", draft), "changed draft not submitted");
    numbers(draft)[0][0] = 84;
    require(numbers(node(editor, id).inputs.at("numbers").literal)[0][0] == 31,
            "later draft edit changed committed literal");
    PortValue regions = DataRef<ROI>(ROI{{{3., 2., 4., 7.}}});
    require(editor.setInitParam("flow_1", id, "regions", regions), "changed ROI draft not submitted");
    std::get<DataRef<ROI>>(regions).get()[0][0][1] = 97.;
    require(std::get<DataRef<ROI>>(node(editor, id).initParams.at("regions")).get()[0][0][1] == 2.,
            "later ROI draft edit changed committed initialization");
    editor.markSaved();
    const auto revision = node(editor, id).executionRevision;
    try
    {
        editor.setLiteral("flow_1", id, "numbers", Array(NestedVector<bool, 2>{{true}}));
        throw std::runtime_error("incompatible draft accepted");
    }
    catch (const ContractError &)
    {
    }
    require(!editor.project().dirty && node(editor, id).executionRevision == revision,
            "rejected candidate changed project identity");
    require(numbers(node(editor, id).inputs.at("numbers").literal)[0][0] == 31,
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
    numbers(nodes[0].inputs.at("numbers").literal)[0][0] = 94;
    require(numbers(registry.find(tool.id)->inputs.front().defaultValue)[0][0] == 2,
            "reconciliation changed registry defaults");
    require(numbers(nodes[1].inputs.at("numbers").literal)[0][0] == 2, "reconciled nodes share Array defaults");
    std::get<DataRef<ROI>>(nodes[0].initParams.at("regions")).get()[0][0][1] = 79.;
    require(std::get<DataRef<ROI>>(nodes[1].initParams.at("regions")).get()[0][0][1] == 1.,
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

void allValuesSurviveRealProjectFiles()
{
    QTemporaryDir directory;
    require(directory.isValid(), "temporary project directory unavailable");
    ToolDefinition tool;
    tool.id          = "test.project_matrix";
    tool.displayName = "工程类型矩阵";
    tool.version     = "1";
    tool.kind        = ToolKind::Cpp;
    require(Array::supportedTypes().size() == 28, "incomplete Array signature matrix");
    for (const auto &type : Array::supportedTypes())
    {
        auto value = Array::empty(*type.dtype, type.rank);
        value.visit(
            [](const auto &data)
            {
                using Vector = std::remove_reference_t<decltype(data.get())>;
                data.get()   = sample<Vector>();
            });
        const auto id = "array_" + std::to_string(tool.inputs.size());
        tool.inputs.push_back({id, id, type, std::move(value)});
    }
    tool.inputs.push_back({
        "regions",
        "区域",
        {Type::ROI},
        DataRef<ROI>(ROI{{{0., 1.5, 2.5}, {1., 1., 2., 3., 4.}, {2., 1., 2., 3., 4., 0.}},
          {},
          {{3., 2., 4., 7.}, {4., 0., 0., 3., 0., 3., 2.}, {5., 1., 2., 3., 4.}}}
        )
    });
    tool.inputs.push_back({"images", "图像", {Type::Image}, DataRef<Image>(Image{})});
    tool.inputs.push_back({"missing", "空值", {Type::Int}, Missing{}});
    tool.inputs.back().required = false;
    ToolRegistry registry;
    registry.addDefinition(tool);
    ProjectEditor editor;
    editor.addNode("flow_1", *registry.find(tool.id), {});
    auto           project  = editor.project();
    const auto     filename = directory.filePath("完整 工程.vrt.json");
    ProjectStorage storage(registry);
    storage.save(project, std::filesystem::path(filename.toStdWString()));
    require(!project.dirty, "successful project commit did not install saved identity");
    QFile file(filename);
    require(file.open(QIODevice::ReadOnly), "saved project unavailable");
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    require(root["format_version"].toInt() == 4, "saved project does not use format4");
    const auto saved    = root["flows"].toArray()[0].toObject()["nodes"].toArray()[0].toObject();
    const auto literals = saved["inputs"].toObject();
    require(literals["array_0"].toObject()["literal"].isArray(), "saved Array used a DTO instead of nested lists");
    require(literals["regions"].toObject()["literal"].toArray().size() == 3, "saved ROI groups lost");
    require(literals["images"].toObject()["literal"].toArray().isEmpty(), "empty Image value lost");
    require(literals["missing"].toObject()["literal"].isNull(), "Missing not saved as null");
    const auto  reopened = storage.load(project.path);
    const auto &restored = reopened.project.flows.front().nodes.front();
    for (const auto &port : tool.inputs)
    {
        require(restored.signatures.inputs.at(port.id) == port.type, "project type signature changed");
        require(equalValues(restored.inputs.at(port.id).literal, port.defaultValue), "project nested literal changed");
    }
    const auto selected = std::find_if(tool.inputs.begin(), tool.inputs.end(), [](const auto &port)
                                       { return port.type == TypeSpec{Type::Array, DType::Int64, 2}; });
    require(selected != tool.inputs.end(), "Int64 rank2 fixture missing");
    numbers(restored.inputs.at(selected->id).literal)[0][0] = 77;
    require(numbers(project.flows.front().nodes.front().inputs.at(selected->id).literal)[0][0]
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
