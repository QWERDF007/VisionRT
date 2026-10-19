#include "core/ProjectStorage.h"
#include "core/ToolRegistry.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::filesystem::path path(const QString &value)
{
    const auto utf8 = value.toUtf8();
    return std::filesystem::u8path(utf8.constData(), utf8.constData() + utf8.size());
}

void put(const QString &filename, const std::string &text)
{
    QFile file(filename);
    require(file.open(QIODevice::WriteOnly), "cannot create contract fixture");
    require(file.write(text.data(), static_cast<qint64>(text.size())) == static_cast<qint64>(text.size()),
            "cannot write contract fixture");
}

std::string get(const QString &filename)
{
    QFile file(filename);
    require(file.open(QIODevice::ReadOnly), "cannot read saved project");
    return file.readAll().toStdString();
}

bool has(const std::vector<ProjectStorageDiagnostic> &diagnostics, ProjectStorageCode code)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [code](const auto &diagnostic) { return diagnostic.code == code; });
}

const char *missingDocument = R"(format_version: 1
project: {name: 缺失工具, next_node_seq: 8, next_flow_seq: 2}
flows:
- flow_id: flow_1
  name: 流程1
  run_selected: true
  nodes:
  - node_seq: 7
    tool_id: unavailable
    tool_name_snapshot: 原始名称
    alias: ''
    pos: [0, 0]
    vendor_extension: {opaque: [one, {two: three}]}
    init_params: {unknown: 保留中文}
    type_snapshot:
      init_params: {unknown: {type: String}}
      inputs: {value: {type: Int}}
      outputs: {value: {type: Int}}
    inputs:
      value: {mode: upstream, literal: 9007199254740993, source: {node_seq: 7, port_id: value}}
)";

void candidateAndRawPreservation(const QString &directory)
{
    ToolRegistry   registry;
    ProjectStorage storage(registry);
    const auto     source = directory + "/缺失.vrt";
    put(source, missingDocument);
    auto loaded = storage.load(path(source));
    require(!loaded.project.dirty, "diagnostics alone made a project dirty");
    require(has(loaded.diagnostics, ProjectStorageCode::DefinitionMissing), "missing definition not diagnosed");
    require(has(loaded.diagnostics, ProjectStorageCode::Cycle), "loaded cycle not diagnosed");
    require(loaded.project.flows[0].nodes[0].inputs.at("value").source->node == 7, "cycle was silently cut");
    loaded.project.flows[0].nodes[0].alias = "可编辑备注";
    loaded.project.dirty                   = true;
    const auto saved                       = directory + "/保留.vrt";
    storage.save(loaded.project, path(saved));
    const auto raw = YAML::Load(get(saved))["flows"][0]["nodes"][0];
    require(raw["vendor_extension"]["opaque"][1]["two"].as<std::string>() == "three",
            "missing tool extension was discarded");
    require(raw["inputs"]["value"]["literal"].as<std::int64_t>() == 9007199254740993LL, "int64 literal lost precision");
    require(raw["alias"].as<std::string>() == "可编辑备注", "missing node alias was not saved");
    const auto before    = get(saved);
    const auto priorPath = loaded.project.path;
    loaded.project.dirty = true;
    bool failed          = false;
    try
    {
        storage.save(loaded.project, path(directory + "/absent/failed.vrt"));
    }
    catch (const std::exception &)
    {
        failed = true;
    }
    require(failed && loaded.project.path == priorPath && loaded.project.dirty,
            "failed save changed current project path or dirty");
    require(get(saved) == before, "failed save altered previous file");
    auto copiedProject                  = loaded.project;
    auto duplicate                      = copiedProject.flows[0].nodes[0];
    duplicate.id                        = 9;
    duplicate.position.x                = 1;
    duplicate.inputs.at("value").source = SourcePort{9, "value"};
    copiedProject.flows[0].nodes.push_back(std::move(duplicate));
    copiedProject.nextNodeId = 10;
    const auto copiedFile    = directory + "/copied.vrt";
    storage.save(copiedProject, path(copiedFile));
    const auto copied = YAML::Load(get(copiedFile))["flows"][0]["nodes"];
    require(
        copied[0]["node_seq"].as<NodeId>() == 7 && copied[1]["node_seq"].as<NodeId>() == 9
            && copied[1]["inputs"]["value"]["source"]["node_seq"].as<NodeId>() == 9
            && loaded.project.flows[0].nodes[0].unresolved["inputs"]["value"]["source"]["node_seq"].as<NodeId>() == 7,
        "copying a missing tool reused its raw ID/source or mutated the original raw subtree");

    for (const auto &text :
         {std::string("[not, a, project]"), std::string("format_version: 2\nproject: {}\nflows: []"),
          std::string("format_version: 1\nproject: [broken]\nflows: []"),
          std::string("format_version: 1\nproject: {name: x, next_node_seq: -1, next_flow_seq: 2}\nflows: []")})
    {
        put(source, text);
        failed = false;
        try
        {
            loaded = storage.load(path(source));
        }
        catch (const std::exception &)
        {
            failed = true;
        }
        require(failed && loaded.project.path == priorPath && loaded.project.dirty,
                "invalid candidate replaced current project");
    }
}

ToolDefinition scalarTool(std::string id)
{
    ToolDefinition tool;
    tool.id           = std::move(id);
    tool.displayName  = "标量";
    tool.kind         = ToolKind::CppClass;
    tool.registryName = tool.id;
    PortDefinition value;
    value.id           = "value";
    value.name         = "值";
    value.type         = {Type::Int};
    value.defaultValue = std::int64_t{4};
    tool.inputs.push_back(value);
    tool.outputs.push_back(value);
    return tool;
}

void reconciliationAndIdentities(const QString &directory)
{
    ToolRegistry   registry;
    auto           tool = scalarTool("unavailable");
    PortDefinition restored;
    restored.id   = "unknown";
    restored.name = "恢复";
    restored.type = {Type::String};
    tool.initParams.push_back(restored);
    registry.addDefinition(tool);
    ProjectStorage storage(registry);
    auto           document              = YAML::Load(missingDocument);
    document["project"]["next_node_seq"] = 1;
    document["project"]["next_flow_seq"] = 1;
    const auto file                      = directory + "/reconcile.vrt";
    put(file, YAML::Dump(document));
    auto        recovered = storage.load(path(file));
    const auto &node      = recovered.project.flows[0].nodes[0];
    require(!node.unresolved.IsDefined() || node.unresolved.IsNull(), "restored node stayed unresolved");
    require(std::get<Utf8>(node.initParams.at("unknown")).str() == "保留中文",
            "unknown tool parameter did not recover");
    require(node.id == 7 && recovered.project.nextNodeId == 8 && recovered.project.nextFlowId == 2,
            "loading did not repair next IDs without renumbering nodes");
    require(recovered.project.dirty && has(recovered.diagnostics, ProjectStorageCode::CounterRepaired),
            "counter correction did not mark dirty");
    ToolRegistry absentRegistry;
    auto         absent                                = ProjectStorage(absentRegistry).load(path(file));
    absent.project.flows[0].nodes[0].executionRevision = 5;
    const auto restoredCandidate                       = storage.reconcile(absent.project);
    require(absent.project.flows[0].nodes[0].unresolved.IsMap()
                && !restoredCandidate.project.flows[0].nodes[0].unresolved.IsMap()
                && restoredCandidate.project.flows[0].nodes[0].executionRevision == 6,
            "reconciliation did not restore a separate candidate and invalidate its execution contract");

    for (int damage = 0; damage != 5; ++damage)
    {
        auto invalid = YAML::Clone(document);
        if (damage == 0)
            invalid["flows"].push_back(YAML::Clone(invalid["flows"][0]));
        if (damage == 1)
            invalid["flows"][0]["nodes"].push_back(YAML::Clone(invalid["flows"][0]["nodes"][0]));
        if (damage == 2)
            invalid["flows"][0]["flow_id"] = "";
        if (damage == 3)
            invalid["flows"][0]["nodes"][0]["node_seq"] = "18446744073709551616";
        if (damage == 4)
        {
            auto duplicate       = YAML::Clone(invalid["flows"][0]);
            duplicate["flow_id"] = "flow_2";
            invalid["flows"].push_back(duplicate);
        }
        put(file, YAML::Dump(invalid));
        bool rejected = false;
        try
        {
            recovered = storage.load(path(file));
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        require(rejected && recovered.project.flows[0].nodes[0].id == 7, "invalid or duplicated identity was accepted");
    }

    auto source                       = YAML::Clone(document["flows"][0]["nodes"][0]);
    source["node_seq"]                = 8;
    source["pos"][0]                  = 1;
    source["inputs"]["value"]["mode"] = "literal";
    source["inputs"]["value"].remove("source");
    document["flows"][0]["nodes"].push_back(source);
    document["flows"][0]["nodes"][0]["inputs"]["value"]["source"]["node_seq"] = 8;
    document["project"]["next_node_seq"]                                      = 9;
    put(file, YAML::Dump(document));
    auto unchanged = storage.load(path(file));
    require(unchanged.project.flows[0].nodes[0].inputs.at("value").source->node == 8,
            "implementation-unavailable definition was treated as missing");
    ToolRegistry changedRegistry;
    auto         changedTool            = scalarTool("unavailable");
    changedTool.initParams              = tool.initParams;
    changedTool.inputs[0].type          = {Type::Float};
    changedTool.inputs[0].defaultValue  = 2.5;
    changedTool.outputs[0].type         = {Type::Float};
    changedTool.outputs[0].defaultValue = 2.5;
    changedRegistry.addDefinition(changedTool);
    auto        changed      = ProjectStorage(changedRegistry).load(path(file));
    const auto &changedInput = changed.project.flows[0].nodes[0].inputs.at("value");
    require(!changedInput.source && changedInput.mode == BindingMode::Upstream,
            "both-end signature change retained a stale edge or activated hidden literal");
    require(std::get<double>(changedInput.literal) == 2.5 && changed.project.dirty,
            "incompatible scalar was reinterpreted instead of replaced");

    source["tool_id"] = "still_missing";
    source["type_snapshot"]["outputs"].remove("value");
    document["flows"][0]["nodes"][1] = source;
    put(file, YAML::Dump(document));
    auto pending = storage.load(path(file));
    require(pending.project.flows[0].nodes[0].inputs.at("value").source.has_value()
                && has(pending.diagnostics, ProjectStorageCode::PendingReference),
            "unknown source port was cleared instead of retained pending recovery");
    const auto retained           = YAML::Clone(document["flows"][0]["nodes"][0]);
    document["flows"][0]["nodes"] = YAML::Node(YAML::NodeType::Sequence);
    document["flows"][0]["nodes"].push_back(retained);
    put(file, YAML::Dump(document));
    auto invalidSource = storage.load(path(file));
    require(!invalidSource.project.flows[0].nodes[0].inputs.at("value").source
                && has(invalidSource.diagnostics, ProjectStorageCode::InvalidReference),
            "definitely absent source was not cleared");
}

void typedArraysAndRanges(const QString &directory)
{
    auto document                        = YAML::Load(missingDocument);
    auto node                            = document["flows"][0]["nodes"][0];
    node["tool_name_snapshot"]           = "数组";
    node["init_params"]                  = YAML::Node(YAML::NodeType::Map);
    node["type_snapshot"]["init_params"] = YAML::Node(YAML::NodeType::Map);
    node["type_snapshot"]["inputs"]["value"]
        = YAML::Load("{type: NumericArray, dtype: float32, rank: 2, layout: dense}");
    node["type_snapshot"]["outputs"] = YAML::Node(YAML::NodeType::Map);
    node["inputs"]["value"]          = YAML::Load("{mode: literal, literal: {shape: [0, 2], data: []}}");
    ToolDefinition tool;
    tool.id           = "unavailable";
    tool.displayName  = "数组";
    tool.kind         = ToolKind::CppFunction;
    tool.registryName = tool.id;
    PortDefinition input;
    input.id   = "value";
    input.name = "数组";
    input.type = {Type::NumericArray, DType::Int64, 2, Layout::Dense};
    tool.inputs.push_back(input);
    ToolRegistry registry;
    registry.addDefinition(tool);
    const auto file = directory + "/array.vrt";
    put(file, YAML::Dump(document));
    auto changed = ProjectStorage(registry).load(path(file));
    require(isMissing(changed.project.flows[0].nodes[0].inputs.at("value").literal),
            "empty float32 array silently became an int64 array");
    require(changed.project.dirty && has(changed.diagnostics, ProjectStorageCode::SchemaChanged),
            "array signature change not diagnosed");
    tool.inputs[0].type.dtype = DType::Float32;
    ToolRegistry oldRegistry;
    oldRegistry.addDefinition(tool);
    auto        old   = ProjectStorage(oldRegistry).load(path(file));
    const auto &array = std::get<NumericArray>(old.project.flows[0].nodes[0].inputs.at("value").literal);
    require(array.dtype() == DType::Float32 && array.buffers()[0].shape()[0] == 0 && array.buffers()[0].shape()[1] == 2,
            "typed empty array was not decoded from saved signature");

    auto rangedTool              = scalarTool("unavailable");
    rangedTool.inputs[0].maximum = std::int64_t{10};
    ToolRegistry rangeRegistry;
    rangeRegistry.addDefinition(rangedTool);
    document                                                         = YAML::Load(missingDocument);
    document["flows"][0]["nodes"][0]["init_params"]                  = YAML::Node(YAML::NodeType::Map);
    document["flows"][0]["nodes"][0]["type_snapshot"]["init_params"] = YAML::Node(YAML::NodeType::Map);
    put(file, YAML::Dump(document));
    auto ranged = ProjectStorage(rangeRegistry).load(path(file));
    require(std::get<std::int64_t>(ranged.project.flows[0].nodes[0].inputs.at("value").literal) == 9007199254740993LL
                && has(ranged.diagnostics, ProjectStorageCode::RangeWarning),
            "new UI range clamped saved int64");
}

void saveAsPaths(const QString &directory)
{
    ToolRegistry registry;
    auto         tool = scalarTool("path_tool");
    tool.outputs.clear();
    tool.inputs.clear();
    for (const auto &id : {"path", "text", "empty", "remote"})
    {
        PortDefinition port;
        port.id     = id;
        port.name   = id;
        port.type   = {Type::String};
        port.editor = std::string(id) == "text" ? "text" : "file";
        tool.inputs.push_back(port);
    }
    PortDefinition folder = tool.inputs[0];
    folder.id             = "folder";
    folder.editor         = "directory";
    tool.initParams.push_back(folder);
    registry.addDefinition(tool);
    ProjectStorage    storage(registry);
    ProjectDefinition project;
    NodeDefinition    node;
    node.id                       = 0;
    node.toolId                   = tool.id;
    node.toolNameSnapshot         = tool.displayName;
    node.inputs["path"]           = {BindingMode::Upstream, Utf8("图像/不存在.png"), std::nullopt};
    node.inputs["text"].literal   = Utf8("图像/不存在.png");
    node.inputs["empty"].literal  = Utf8("");
    node.inputs["remote"].literal = Utf8("Z:/异盘/资源.png");
    node.initParams["folder"]     = Utf8("目录");
    for (const auto &port : tool.inputs) node.signatures.inputs.emplace(port.id, port.type);
    node.signatures.initParams.emplace(folder.id, folder.type);
    project.flows[0].nodes.push_back(node);
    project.nextNodeId      = 1;
    const auto oldDirectory = directory + "/旧位置";
    const auto newDirectory = directory + "/新位置";
    require(QDir().mkpath(oldDirectory) && QDir().mkpath(newDirectory), "cannot create save-as directories");
    storage.save(project, path(oldDirectory + "/首次"));
    require(project.path.extension() == ".vrt" && !project.dirty, "first save did not append extension or clear dirty");
    require(std::get<Utf8>(project.flows[0].nodes[0].inputs.at("path").literal).str() == "图像/不存在.png",
            "unnamed relative path was resolved using a fabricated old directory");
    project.dirty = true;
    storage.save(project, path(newDirectory + "/另存.vrt"));
    const auto &saved = project.flows[0].nodes[0];
    require(std::get<Utf8>(saved.inputs.at("path").literal).str() == "../旧位置/图像/不存在.png",
            "hidden upstream literal path did not rebase");
    require(std::get<Utf8>(saved.initParams.at("folder")).str() == "../旧位置/目录",
            "directory init parameter did not rebase");
    require(std::get<Utf8>(saved.inputs.at("text").literal).str() == "图像/不存在.png"
                && std::get<Utf8>(saved.inputs.at("empty").literal).str().empty(),
            "save-as rewrote non-path String or empty path");
    require(std::get<Utf8>(saved.inputs.at("remote").literal).str() == "Z:/异盘/资源.png",
            "cross-drive resource lost its absolute path");
    auto reopened = storage.load(project.path);
    require(std::get<Utf8>(reopened.project.flows[0].nodes[0].inputs.at("path").literal).str()
                == "../旧位置/图像/不存在.png",
            "saved path differs from installed candidate");
    const auto before  = std::get<Utf8>(saved.inputs.at("path").literal).str();
    const auto oldPath = project.path;
    project.dirty      = true;
    bool failed        = false;
    try
    {
        storage.save(project, path(directory + "/not-created/failed.vrt"));
    }
    catch (const std::exception &)
    {
        failed = true;
    }
    require(failed && project.dirty && project.path == oldPath
                && std::get<Utf8>(project.flows[0].nodes[0].inputs.at("path").literal).str() == before,
            "failed save-as installed rebased candidate");
}

void wideIdsAndCycleMembership(const QString &directory)
{
    ToolRegistry     registry;
    ProjectStorage   storage(registry);
    auto             document                                                 = YAML::Load(missingDocument);
    const auto       file                                                     = directory + "/wide.vrt";
    constexpr NodeId large                                                    = 9007199254740993ULL;
    document["flows"][0]["nodes"][0]["node_seq"]                              = large;
    document["flows"][0]["nodes"][0]["inputs"]["value"]["source"]["node_seq"] = large;
    document["project"]["next_node_seq"]                                      = large + 1;
    put(file, YAML::Dump(document));
    auto loaded = storage.load(path(file));
    storage.save(loaded.project);
    auto reopened = storage.load(path(file));
    require(reopened.project.flows[0].nodes[0].id == large && reopened.project.nextNodeId == large + 1,
            "node identity crossed the JS safe integer boundary imprecisely");
    constexpr NodeId maximum                                                  = std::numeric_limits<NodeId>::max();
    document["flows"][0]["nodes"][0]["node_seq"]                              = maximum;
    document["flows"][0]["nodes"][0]["inputs"]["value"]["source"]["node_seq"] = maximum;
    document["project"]["next_node_seq"]                                      = maximum;
    put(file, YAML::Dump(document));
    const auto exhausted = storage.load(path(file));
    require(exhausted.project.flows[0].nodes[0].id == maximum && exhausted.project.nextNodeId == maximum
                && has(exhausted.diagnostics, ProjectStorageCode::SequenceExhausted),
            "exhausted uint64 identity wrapped instead of retaining a read-only exhausted sequence");

    document                                                                  = YAML::Load(missingDocument);
    document["project"]["next_node_seq"]                                      = 10;
    document["flows"][0]["nodes"][0]["inputs"]["value"]["source"]["node_seq"] = 8;
    auto second                                     = YAML::Clone(document["flows"][0]["nodes"][0]);
    second["node_seq"]                              = 8;
    second["pos"][0]                                = 1;
    second["inputs"]["value"]["source"]["node_seq"] = 7;
    document["flows"][0]["nodes"].push_back(second);
    auto downstream        = YAML::Clone(second);
    downstream["node_seq"] = 9;
    downstream["pos"][0]   = 2;
    document["flows"][0]["nodes"].push_back(downstream);
    put(file, YAML::Dump(document));
    const auto       cyclic = storage.load(path(file));
    std::set<NodeId> members;
    for (const auto &diagnostic : cyclic.diagnostics)
        if (diagnostic.code == ProjectStorageCode::Cycle)
            members.insert(*diagnostic.node);
    require(members == std::set<NodeId>{7, 8}, "cycle diagnosis incorrectly included downstream-only nodes");
}
} // namespace

int main()
{
    try
    {
        QTemporaryDir temporary;
        require(temporary.isValid(), "temporary directory unavailable");
        candidateAndRawPreservation(temporary.path());
        reconciliationAndIdentities(temporary.path());
        typedArraysAndRanges(temporary.path());
        saveAsPaths(temporary.path());
        wideIdsAndCycleMembership(temporary.path());
        std::cout << "project storage contract passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
