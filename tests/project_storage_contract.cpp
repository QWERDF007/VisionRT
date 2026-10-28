#include "core/ProjectStorage.h"
#include "core/DefinitionJson.h"
#include "core/ProjectEditor.h"
#include "core/ToolRegistry.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::filesystem::path path(const QString &value)
{
    return std::filesystem::path(value.toStdWString());
}

std::string utf8Path(const std::filesystem::path &value)
{
    const auto text = value.generic_u8string();
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}

void put(const QString &filename, const QByteArray &text)
{
    QFile file(filename);
    require(file.open(QIODevice::WriteOnly), "cannot create contract fixture");
    require(file.write(text) == text.size(), "cannot write contract fixture");
}

QByteArray get(const QString &filename)
{
    QFile file(filename);
    require(file.open(QIODevice::ReadOnly), "cannot read saved project");
    return file.readAll();
}

bool has(const std::vector<ProjectStorageDiagnostic> &diagnostics, ProjectStorageCode code)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [code](const auto &diagnostic) { return diagnostic.code == code; });
}

QJsonObject savedInput(const QString &source = "7", QJsonValue literal = qint64{9007199254740993LL})
{
    QJsonObject result{
        {   "mode", source.isEmpty() ? "literal" : "upstream"},
        {"literal",                                   literal}
    };
    if (!source.isEmpty())
        result.insert("source", QJsonObject{
                                    {"node_seq",  source},
                                    { "port_id", "value"}
        });
    return result;
}

QJsonObject savedNode(const QString &id = "7", const QString &source = "7")
{
    return {
        {          "node_seq",                                                                                                      id},
        {           "tool_id",                                                                                           "unavailable"},
        {"tool_name_snapshot",                                                                                          "原始名称"},
        {             "alias",                                                                                                      ""},
        {               "pos",                                                                                        QJsonArray{0, 0}},
        {  "vendor_extension",                               QJsonObject{{"opaque", QJsonArray{"one", QJsonObject{{"two", "three"}}}}}},
        {       "init_params",                                                                QJsonObject{{"unknown", "保留中文"}}},
        {     "type_snapshot", QJsonObject{{"init_params", QJsonObject{{"unknown", QJsonObject{{"type", "String"}}}}},
 {"inputs", QJsonObject{{"value", QJsonObject{{"type", "Int"}}}}},
 {"outputs", QJsonObject{{"value", QJsonObject{{"type", "Int"}}}}}}                                     },
        {            "inputs",                                                              QJsonObject{{"value", savedInput(source)}}}
    };
}

QJsonObject savedProject(const QJsonArray &nodes, const QString &nextNode = "8", const QString &nextFlow = "2")
{
    return {
        {"format_version",                                                                     4                          },
        {       "project", QJsonObject{{"name", "缺失工具"}, {"next_node_seq", nextNode}, {"next_flow_seq", nextFlow}}},
        {         "flows",
         QJsonArray{QJsonObject{{"flow_id", "flow_1"}, {"name", "流程1"}, {"run_selected", true}, {"nodes", nodes}}}    }
    };
}

ToolDefinition scalarTool()
{
    ToolDefinition tool;
    tool.id           = "unavailable";
    tool.displayName  = "原始名称";
    tool.kind         = ToolKind::Cpp;

    PortDefinition value;
    value.id           = "value";
    value.name         = "值";
    value.type         = {Type::Int};
    value.defaultValue = std::int64_t{4};
    tool.inputs.push_back(value);
    tool.outputs.push_back(value);
    PortDefinition restored;
    restored.id   = "unknown";
    restored.name = "恢复";
    restored.type = {Type::String};
    tool.initParams.push_back(restored);
    return tool;
}

void knownFailureDiagnostics(const QString &directory)
{
    ToolRegistry registry;
    const auto   filename = directory + "/错误 工程.vrt.json";
    for (const auto &[document, reason] : {
             std::pair{R"json({"format_version":4,"project":{},"flows":{}})json",                             "flows 必须是数组"},
             std::pair{                        R"json({"format_version":1})json", "format_version 不受支持，要求 4–5，实际 1"},
             std::pair{                        R"json({"format_version":2})json", "format_version 不受支持，要求 4–5，实际 2"},
             std::pair{                      R"json({"format_version":"4"})json",                    "format_version 必须是整数"},
             std::pair{                                                  "[1,2]",              "工程 JSON 根节点必须是对象"}
    })
    {
        put(filename, document);
        bool rejected = false;
        try
        {
            ProjectStorage(registry).load(path(filename));
        }
        catch (const ContractError &error)
        {
            const std::string message = error.what();
            require(
                message.find("错误 工程.vrt.json") != std::string::npos && message.find(reason) != std::string::npos,
                "project contract failure lost its file and precise Chinese field reason");
            require(message.find("格式不受支持") == std::string::npos,
                    "project field failure was relabeled as an unsupported format");
            rejected = true;
        }
        require(rejected, "known project failure did not use ContractError");
    }
    for (const auto extension : {".yaml", ".YML"})
    {
        const auto legacy = directory + "/旧工程" + extension;
        put(legacy, "format_version: 2\nproject: {}\nflows: []\n");
        bool rejected = false;
        try
        {
            ProjectStorage(registry).load(path(legacy));
        }
        catch (const ContractError &error)
        {
            rejected = std::string(error.what()).find("工程格式不受支持，请使用 JSON 工程") != std::string::npos;
        }
        require(rejected, "legacy YAML did not report the JSON project requirement");
    }
}

void candidateAndRawPreservation(const QString &directory)
{
    ToolRegistry   registry;
    ProjectStorage storage(registry);
    const auto     source = directory + "/缺失.vrt.json";
    put(source, QJsonDocument(savedProject({savedNode()})).toJson());
    auto loaded = storage.load(path(source));
    require(!loaded.project.dirty && has(loaded.diagnostics, ProjectStorageCode::DefinitionMissing),
            "missing node was not retained cleanly");
    require(has(loaded.diagnostics, ProjectStorageCode::Cycle), "loaded cycle not diagnosed");
    loaded.project.flows[0].nodes[0].alias = "可编辑备注";
    const auto saved                       = directory + "/保留.vrt.json";
    storage.save(loaded.project, path(saved));
    const auto root = QJsonDocument::fromJson(get(saved)).object();
    const auto raw  = root.value("flows").toArray()[0].toObject().value("nodes").toArray()[0].toObject();
    require(root.value("format_version").toInteger() == 5 && raw.value("node_seq").isString(),
            "project version or identity encoding is wrong");
    require(raw.value("vendor_extension").toObject().value("opaque").toArray()[1].toObject().value("two") == "three",
            "extension lost");
    require(raw.value("inputs").toObject().value("value").toObject().value("literal").toInteger() == 9007199254740993LL,
            "int64 precision lost");
    require(raw.value("alias") == "可编辑备注", "alias lost");
    const auto before    = get(saved);
    const auto priorPath = loaded.project.path;
    loaded.project.dirty = true;
    bool failed          = false;
    try
    {
        storage.save(loaded.project, path(directory + "/absent/failed.vrt.json"));
    }
    catch (const std::exception &)
    {
        failed = true;
    }
    require(failed && loaded.project.path == priorPath && loaded.project.dirty && get(saved) == before,
            "failed save installed candidate");
    auto copied                         = loaded.project;
    auto duplicate                      = copied.flows[0].nodes[0];
    duplicate.id                        = 9;
    duplicate.position.x                = 1;
    duplicate.inputs.at("value").source = SourcePort{9, "value"};
    copied.flows[0].nodes.push_back(std::move(duplicate));
    copied.nextNodeId = 10;
    storage.save(copied, path(directory + "/copied.vrt.json"));
    const auto reopened = storage.load(copied.path);
    require(reopened.project.flows[0].nodes[1].id == 9
                && reopened.project.flows[0].nodes[1].inputs.at("value").source->node == 9
                && loaded.project.flows[0].nodes[0].unresolved.value("node_seq") == "7",
            "unknown node copy shared identity");
    for (const auto &text : {QByteArray("[1,2]"), QByteArray("{\"format_version\":1}"),
                             QByteArray("{\"format_version\":2,\"project\":[]}"), QByteArray("{\"flows\":[1,]}"),
                             QByteArray("format_version: 1\nproject: {}\nflows: []")})
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
                "invalid candidate replaced project");
    }
    try
    {
        storage.load(path(source));
        require(false, "YAML project accepted");
    }
    catch (const std::exception &error)
    {
        const std::string message = error.what();
        require(message.find("需要标准 JSON 文档，offset=") != std::string::npos
                    && message.find(utf8Path(path(source))) != std::string::npos,
                "legacy syntax failure lost its JSON position or source file");
    }
}

void reconciliationAndIdentities(const QString &directory)
{
    ToolRegistry registry;
    const auto   tool = scalarTool();
    registry.addDefinition(tool);
    ProjectStorage storage(registry);
    const auto     file     = directory + "/reconcile.vrt.json";
    const auto     document = savedProject({savedNode()}, "1", "1");
    put(file, QJsonDocument(document).toJson());
    auto        recovered = storage.load(path(file));
    const auto &node      = recovered.project.flows[0].nodes[0];
    require(node.unresolved.isEmpty() && std::get<std::string>(node.initParams.at("unknown")) == "保留中文",
            "definition did not recover");
    require(node.id == 7 && recovered.project.nextNodeId == 8 && recovered.project.nextFlowId == 2
                && recovered.project.dirty && has(recovered.diagnostics, ProjectStorageCode::CounterRepaired),
            "counter repair changed identity");
    ToolRegistry absentRegistry;
    auto         absent                                = ProjectStorage(absentRegistry).load(path(file));
    absent.project.flows[0].nodes[0].executionRevision = 5;
    const auto restored                                = storage.reconcile(absent.project);
    require(!absent.project.flows[0].nodes[0].unresolved.isEmpty()
                && restored.project.flows[0].nodes[0].unresolved.isEmpty()
                && restored.project.flows[0].nodes[0].executionRevision == 6,
            "reconciliation did not invalidate separate candidate");
    for (int damage = 0; damage != 8; ++damage)
    {
        auto invalid     = document;
        auto flow        = invalid.value("flows").toArray()[0].toObject();
        auto nodes       = flow.value("nodes").toArray();
        auto damagedNode = nodes[0].toObject();
        if (damage == 0)
            invalid.insert("flows", QJsonArray{flow, flow});
        if (damage == 1)
            nodes.append(nodes[0]);
        if (damage == 2)
            flow.insert("flow_id", "");
        if (damage == 3)
            damagedNode.insert("node_seq", "18446744073709551616");
        if (damage == 4)
            damagedNode.insert("node_seq", qint64{7});
        if (damage == 5)
            flow.insert("run_selected", "true");
        if (damage == 6)
            damagedNode.insert("pos", QJsonArray{0.5, 0});
        if (damage == 7)
        {
            auto second = flow;
            second.insert("flow_id", "flow_2");
            invalid.insert("flows", QJsonArray{flow, second});
        }
        if (damage != 0 && damage != 7)
        {
            nodes[0] = damagedNode;
            flow.insert("nodes", nodes);
            invalid.insert("flows", QJsonArray{flow});
        }
        put(file, QJsonDocument(invalid).toJson());
        bool rejected = false;
        try
        {
            recovered = storage.load(path(file));
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        require(rejected && recovered.project.flows[0].nodes[0].id == 7, "invalid or duplicated identity accepted");
    }
    auto source = savedNode("8", "");
    source.insert("pos", QJsonArray{1, 0});
    const auto target = savedNode("7", "8");
    put(file, QJsonDocument(savedProject({target, source}, "9")).toJson());
    require(storage.load(path(file)).project.flows[0].nodes[0].inputs.at("value").source->node == 8,
            "definition treated as unavailable implementation");
    auto changedTool           = tool;
    changedTool.inputs[0].type = changedTool.outputs[0].type = {Type::Float};
    changedTool.inputs[0].defaultValue = changedTool.outputs[0].defaultValue = 2.5;
    ToolRegistry changedRegistry;
    changedRegistry.addDefinition(changedTool);
    const auto  changed = ProjectStorage(changedRegistry).load(path(file));
    const auto &binding = changed.project.flows[0].nodes[0].inputs.at("value");
    require(!binding.source && binding.mode == BindingMode::Upstream && std::get<double>(binding.literal) == 2.5,
            "schema change retained stale binding");
    source.insert("tool_id", "still_missing");
    auto snapshot = source.value("type_snapshot").toObject();
    snapshot.insert("outputs", QJsonObject{});
    source.insert("type_snapshot", snapshot);
    put(file, QJsonDocument(savedProject({target, source}, "9")).toJson());
    const auto pending = storage.load(path(file));
    require(pending.project.flows[0].nodes[0].inputs.at("value").source.has_value()
                && has(pending.diagnostics, ProjectStorageCode::PendingReference),
            "unknown source cleared");
    put(file, QJsonDocument(savedProject({target})).toJson());
    require(!storage.load(path(file)).project.flows[0].nodes[0].inputs.at("value").source, "absent source retained");
}

void typedArraysAndRanges(const QString &directory)
{
    auto node = savedNode();
    node.insert("init_params", QJsonObject{});
    node.insert(
        "type_snapshot",
        QJsonObject{
            {"init_params",                                                                             QJsonObject{}},
            {    "outputs",                                                                             QJsonObject{}},
            {     "inputs", QJsonObject{{"value", QJsonObject{{"type", "Array"}, {"dtype", "float32"}, {"rank", 2}}}}}
    });
    node.insert("inputs", QJsonObject{
                              {"value", savedInput("", QJsonArray{QJsonArray{}, QJsonArray{}})}
    });
    auto tool = scalarTool();
    tool.initParams.clear();
    tool.outputs.clear();
    tool.inputs[0].type         = {Type::Array, DType::Int64, 2};
    tool.inputs[0].defaultValue = Missing{};
    ToolRegistry registry;
    registry.addDefinition(tool);
    const auto file = directory + "/array.vrt.json";
    put(file, QJsonDocument(savedProject({node})).toJson());
    const auto changed = ProjectStorage(registry).load(path(file));
    require(isMissing(changed.project.flows[0].nodes[0].inputs.at("value").literal) && changed.project.dirty,
            "float32 empty array reinterpreted as int64");
    tool.inputs[0].type.dtype = DType::Float32;
    ToolRegistry oldRegistry;
    oldRegistry.addDefinition(tool);
    const auto  old   = ProjectStorage(oldRegistry).load(path(file));
    const auto &array = std::get<Array>(old.project.flows[0].nodes[0].inputs.at("value").literal);
    const auto &rows  = array.get<NestedVector<float, 2>>();
    require(rows.size() == 2 && rows[0].empty() && rows[1].empty(), "empty nested rows lost");
    auto rangedTool              = scalarTool();
    rangedTool.inputs[0].maximum = std::int64_t{10};
    ToolRegistry rangedRegistry;
    rangedRegistry.addDefinition(rangedTool);
    const auto rangedNode = savedNode("7", "");
    put(file, QJsonDocument(savedProject({rangedNode})).toJson());
    const auto ranged = ProjectStorage(rangedRegistry).load(path(file));
    // Loading consumes the same normalisation as editing: an out-of-range stored value is written
    // back in its legal form, reported once, and the project becomes dirty because it changed.
    require(std::get<std::int64_t>(ranged.project.flows[0].nodes[0].inputs.at("value").literal) == 10
                && has(ranged.diagnostics, ProjectStorageCode::RangeWarning) && ranged.project.dirty,
            "saved range violation was not clamped to the declared maximum");
    auto overflow = QJsonDocument(savedProject({rangedNode})).toJson();
    overflow.replace("9007199254740993", "-9223372036854775809");
    put(file, overflow);
    bool rejected = false;
    try
    {
        ProjectStorage(rangedRegistry).load(path(file));
    }
    catch (const std::exception &)
    {
        rejected = true;
    }
    require(rejected, "raw int64 overflow accepted from file after Qt rounding");
}

void saveAsPaths(const QString &directory)
{
    ToolRegistry registry;
    auto         tool = scalarTool();
    tool.initParams.clear();
    tool.outputs.clear();
    tool.inputs.clear();
    for (const auto &id : {"path", "text", "empty", "remote", "absolute"})
    {
        PortDefinition port;
        port.id     = id;
        port.name   = id;
        port.type   = {Type::String};
        port.editor = std::string(id) == "text" ? "text" : "file";
        tool.inputs.push_back(port);
    }
    PortDefinition mode;
    mode.id     = "mode";
    mode.name   = "模式";
    mode.type   = {Type::String};
    mode.editor = "text";
    tool.inputs.push_back(mode);
    PortDefinition dynamic;
    dynamic.id           = "dynamic";
    dynamic.name         = "动态路径";
    dynamic.type         = {Type::String};
    dynamic.editor       = "path";
    dynamic.pathModeFrom = "mode";
    tool.inputs.push_back(dynamic);
    PortDefinition folder = tool.inputs[0];
    folder.id             = "folder";
    folder.editor         = "directory";
    tool.initParams.push_back(folder);
    registry.addDefinition(tool);
    ProjectStorage    storage(registry);
    ProjectDefinition project;
    NodeDefinition    node;
    node.id                         = 0;
    node.toolId                     = tool.id;
    node.toolNameSnapshot           = tool.displayName;
    node.inputs["path"]             = {BindingMode::Upstream, std::string("图像/不存在.png"), std::nullopt};
    node.inputs["text"].literal     = std::string("图像/不存在.png");
    node.inputs["empty"].literal    = std::string("");
    node.inputs["remote"].literal   = std::string("Z:/异盘/资源.png");
    node.inputs["dynamic"].literal  = std::string("动态/图像.png");
    node.initParams["folder"]       = std::string("目录");
    const auto oldDirectory         = directory + "/旧位置";
    const auto newDirectory         = directory + "/新位置";
    const auto absolutePath         = utf8Path(path(oldDirectory) / std::filesystem::path(u8"绝对.png"));
    node.inputs["absolute"].literal = std::string(absolutePath);
    for (const auto &port : tool.inputs) node.signatures.inputs.emplace(port.id, port.type);
    node.signatures.initParams.emplace(folder.id, folder.type);
    project.flows[0].nodes.push_back(node);
    project.nextNodeId = 1;
    require(QDir().mkpath(oldDirectory) && QDir().mkpath(newDirectory), "cannot create save-as directories");
    storage.save(project, path(oldDirectory + "/首次"));
    require(project.path.filename() == std::filesystem::path(u8"首次.vrt.json") && !project.dirty,
            "first save did not append extension or clear dirty");
    require(std::get<std::string>(project.flows[0].nodes[0].inputs.at("path").literal) == "图像/不存在.png",
            "unnamed relative path was resolved using a fabricated old directory");
    require(std::get<std::string>(project.flows[0].nodes[0].inputs.at("absolute").literal) == absolutePath,
            "unnamed project guessed a relative base for an absolute path");
    project.dirty = true;
    storage.save(project, path(newDirectory + "/另存.vrt.json"));
    const auto &saved = project.flows[0].nodes[0];
    require(std::get<std::string>(saved.inputs.at("path").literal) == "../旧位置/图像/不存在.png",
            "hidden upstream literal path did not rebase");
    require(std::get<std::string>(saved.initParams.at("folder")) == "../旧位置/目录",
            "directory init parameter did not rebase");
    require(std::get<std::string>(saved.inputs.at("dynamic").literal) == "../旧位置/动态/图像.png",
            "dynamic path input did not rebase");
    {
        const auto &text  = std::get<std::string>(saved.inputs.at("text").literal);
        const auto &empty = std::get<std::string>(saved.inputs.at("empty").literal);
        const auto  emsg  = "save-as rewrote non-path String or empty path: text=" + text + " empty=" + empty;
        require(text == "图像/不存在.png" && empty.empty(), emsg.c_str());
    }
    require(std::get<std::string>(saved.inputs.at("remote").literal) == "Z:/异盘/资源.png",
            "cross-drive resource lost its absolute path");
    auto reopened = storage.load(project.path);
    require(std::get<std::string>(reopened.project.flows[0].nodes[0].inputs.at("path").literal)
                == "../旧位置/图像/不存在.png",
            "saved path differs from installed candidate");
    const auto before  = std::get<std::string>(saved.inputs.at("path").literal);
    const auto oldPath = project.path;
    project.dirty      = true;
    bool failed        = false;
    try
    {
        storage.save(project, path(directory + "/not-created/failed.vrt.json"));
    }
    catch (const std::exception &)
    {
        failed = true;
    }
    require(failed && project.dirty && project.path == oldPath
                && std::get<std::string>(project.flows[0].nodes[0].inputs.at("path").literal) == before,
            "failed save-as installed rebased candidate");
}

void retiredInitialization(const QString &directory)
{
    const auto tool = parseToolDefinitionJson(R"json({
        "schema_version":4,"tool_id":"unavailable","display_name":"接收器","version":"1.0",
        "kind":"host","entry":{"binding":"visionrt.receiver"},
        "init_params":[
            {"id":"source_flow_id","name":"流程","type":"String","default":""},
            {"id":"source_node_seq","name":"节点","type":"String","default":""},
            {"id":"source_port_id","name":"端口","type":"String","default":"value"}],
        "inputs":[],"outputs":[{"id":"value","name":"值","type_from":{
            "flow":"source_flow_id","node":"source_node_seq","port":"source_port_id"}}]
    })json");
    auto       raw  = savedNode("9007199254740993", "");
    raw.insert("init_params", QJsonDocument::fromJson(R"json({
        "source_flow_name":"旧流程","source_node_name":"同名节点","source_port_id":"value",
        "retired_values":[[9007199254740993],[],[-17]]
    })json")
                                  .object());
    raw.insert("inputs", QJsonObject{});
    raw.insert("type_snapshot", QJsonDocument::fromJson(R"json({
        "init_params":{
            "source_flow_name":{"type":"String"},"source_node_name":{"type":"String"},
            "source_port_id":{"type":"String"},"retired_values":{"type":"Array","dtype":"int64","rank":2}
        },"inputs":{},"outputs":{"value":{"type":"Int"}}
    })json")
                                    .object());
    const auto input  = directory + "/retired.vrt.json";
    const auto output = directory + "/retained.vrt.json";
    put(input, QJsonDocument(savedProject({raw}, "9007199254740994")).toJson());
    ToolRegistry registry;
    registry.addDefinition(tool);
    ProjectStorage storage(registry);
    auto           loaded = storage.load(path(input));
    require(has(loaded.diagnostics, ProjectStorageCode::InvalidReference), "old names were guessed as IDs");
    require(!loaded.project.flows[0].nodes[0].initParams.contains("source_node_name"),
            "retired parameters leaked into executable configuration");
    storage.save(loaded.project, path(output));
    const auto saved = QJsonDocument::fromJson(get(output))
                           .object()["flows"]
                           .toArray()[0]
                           .toObject()["nodes"]
                           .toArray()[0]
                           .toObject();
    const auto values = saved["init_params"].toObject();
    require(values["source_flow_name"] == "旧流程" && values["source_node_name"] == "同名节点",
            "reconciliation discarded unresolved name-based configuration");
    const auto retainedArray = values["retired_values"].toArray();
    require(retainedArray.size() == 3 && retainedArray[0].toArray()[0].toInteger() == 9007199254740993LL
                && retainedArray[1].toArray().isEmpty() && retainedArray[2].toArray()[0].toInteger() == -17,
            "retired nested values lost integer precision or empty branches");
    require(
        saved["type_snapshot"].toObject()["init_params"].toObject()["source_node_name"].toObject()["type"] == "String",
        "retired parameter type was not preserved");
    const auto reloaded = storage.load(path(output));
    require(has(reloaded.diagnostics, ProjectStorageCode::InvalidReference),
            "retired reference diagnostics disappeared after save/reopen");
    require(!reloaded.project.dirty, "preserved parameters made an unchanged project perpetually dirty");
    auto unchanged                                = reloaded.project;
    unchanged.flows[0].nodes[0].executionRevision = 19;
    const auto reconciled                         = storage.reconcile(unchanged);
    require(reconciled.project.flows[0].nodes[0].executionRevision == 19,
            "retained non-executable parameters invalidated an unchanged tool instance");
    ProjectEditor editor(reconciled.project);
    editor.copyFlow("flow_1");
    auto       copied     = editor.project();
    const auto copiedPath = directory + "/copied-retained.vrt.json";
    storage.save(copied, path(copiedPath));
    const auto copiedProject = storage.load(path(copiedPath)).project;
    require(
        copiedProject.flows.size() == 2 && copiedProject.flows[0].nodes[0].id != copiedProject.flows[1].nodes[0].id
            && copiedProject.flows[0].nodes[0].retiredInitParams == copiedProject.flows[1].nodes[0].retiredInitParams,
        "copying a flow lost opaque initialization metadata");
    require(std::any_of(reloaded.diagnostics.begin(), reloaded.diagnostics.end(),
                        [](const auto &diagnostic)
                        {
                            return diagnostic.code == ProjectStorageCode::InvalidReference
                                && diagnostic.message.find("重新选择") != std::string::npos;
                        }),
            "unresolved retained parameters did not explain how to repair the source");
    auto relocated = reloaded.project;
    require(QDir().mkpath(directory + "/迁移"), "cannot create retired-parameter relocation directory");
    require(has(storage.save(relocated, path(directory + "/迁移/retained.vrt.json")), ProjectStorageCode::UnknownPaths),
            "Save As silently treated undeclared parameter values as known paths");
    const auto movedNode = QJsonDocument::fromJson(get(directory + "/迁移/retained.vrt.json"))
                               .object()["flows"]
                               .toArray()[0]
                               .toObject()["nodes"]
                               .toArray()[0]
                               .toObject();
    require(movedNode["init_params"] == saved["init_params"] && movedNode["type_snapshot"] == saved["type_snapshot"],
            "Save As rewrote opaque initialization values or types");
    auto previous             = tool;
    previous.initParams[0].id = "source_flow_name";
    previous.initParams[1].id = "source_node_name";
    for (auto &port : previous.outputs)
        if (port.id == "value")
            port.typeFrom = PortTypeReference{"source_flow_name", "source_node_name", "source_port_id"};
    ToolRegistry restoredRegistry;
    restoredRegistry.addDefinition(previous);
    const auto restored = ProjectStorage(restoredRegistry).load(path(output));
    require(std::get<std::string>(restored.project.flows[0].nodes[0].initParams.at("source_node_name")) == "同名节点",
            "restored declaration did not recover its original parameter");
    const auto  revived     = ProjectStorage(restoredRegistry).reconcile(unchanged);
    const auto &revivedNode = revived.project.flows[0].nodes[0];
    require(std::get<std::string>(revivedNode.initParams.at("source_node_name")) == "同名节点"
                && !revivedNode.retiredInitParams.contains("source_node_name") && revivedNode.executionRevision == 20,
            "catalog reconciliation did not revive declared parameters and invalidate the instance once");
}

void wideIdsAndCycleMembership(const QString &directory)
{
    ToolRegistry   registry;
    ProjectStorage storage(registry);
    const auto     file = directory + "/wide.vrt.json";
    for (const auto &id : {QString("0"), QString("1"), QString("9007199254740993"), QString("18446744073709551615")})
    {
        const auto next = id == "18446744073709551615" ? id : QString::number(id.toULongLong() + 1);
        put(file, QJsonDocument(savedProject({savedNode(id, id)}, next)).toJson());
        auto loaded = storage.load(path(file));
        storage.save(loaded.project);
        const auto reopened = storage.load(path(file));
        require(reopened.project.flows[0].nodes[0].id == id.toULongLong()
                    && reopened.project.nextNodeId == next.toULongLong(),
                "uint64 precision lost");
        if (id == "18446744073709551615")
            require(has(reopened.diagnostics, ProjectStorageCode::SequenceExhausted), "ID exhaustion not diagnosed");
    }
    auto first  = savedNode("7", "8");
    auto second = savedNode("8", "7");
    second.insert("pos", QJsonArray{1, 0});
    auto downstream = savedNode("9", "8");
    downstream.insert("pos", QJsonArray{2, 0});
    put(file, QJsonDocument(savedProject({first, second, downstream}, "10")).toJson());
    const auto       cyclic = storage.load(path(file));
    std::set<NodeId> members;
    for (const auto &diagnostic : cyclic.diagnostics)
        if (diagnostic.code == ProjectStorageCode::Cycle)
            members.insert(*diagnostic.node);
    require(members == std::set<NodeId>{7, 8}, "downstream-only nodes included in cycle");
}

void dormantLiteralSources(const QString &directory)
{
    // T-B04: Dormant source on Literal binding mode must not block execution or produce false diagnostics
    ToolRegistry registry;
    ToolDefinition defA;
    defA.id = "test.producer";
    defA.displayName = "生产者";
    defA.outputs = {{"out", "输出", {Type::Int}}};
    registry.addDefinition(defA);

    ToolDefinition defB;
    defB.id = "test.consumer";
    defB.displayName = "消费者";
    defB.inputs = {{"in", "输入", {Type::Int}}};
    registry.addDefinition(defB);

    ProjectEditor editor;
    const auto pA = editor.addNode("flow_1", defA, {0, 100}); // Y = 100
    const auto pB = editor.addNode("flow_1", defB, {0, 50});  // Y = 50 (above pA)

    // Consumer (pB) has an input with Literal mode, but has a stale/dormant source pointing to pA (below pB)
    auto project = editor.project();
    auto &nodeB = project.flows[0].nodes[1];
    auto &binding = nodeB.inputs["in"];
    binding.mode = BindingMode::Literal;
    binding.literal = std::int64_t{42};
    binding.source = SourcePort{pA, "out"}; // If upstream, this would violate Y < Y

    const auto filePath = std::filesystem::path((directory + "/dormant_literal.vrt.json").toStdString());
    ProjectStorage(registry).save(project, filePath);

    const auto loaded = ProjectStorage(registry).load(filePath);
    bool hasHierarchyDiagnostic = false;
    for (const auto &diag : loaded.diagnostics)
    {
        if (diag.field == "inputs.in")
            hasHierarchyDiagnostic = true;
    }
    require(!hasHierarchyDiagnostic, "T-B04: Literal mode with dormant source produced false diagnostic");
    const auto &loadedBinding = loaded.project.flows[0].nodes[1].inputs.at("in");
    require(loadedBinding.mode == BindingMode::Literal, "T-B04: Binding mode changed from Literal");
    require(std::get<std::int64_t>(loadedBinding.literal) == 42, "T-B04: Literal value corrupted");

    // Switching back to Upstream mode must be rejected
    const auto check = validateBinding(loaded.project.flows[0], pA, "out", pB, "in");
    require(!check && check.reason == BindingError::SameOrUpperRow, "T-B04: Upstream mode did not reject illegal edge");
}

void canonicalSequences(const QString &directory)
{
    ToolRegistry   registry;
    ProjectStorage storage(registry);
    const auto     file = directory + "/非规范序号.vrt.json";
    for (const auto &field :
         {QString("node_seq"), QString("next_node_seq"), QString("next_flow_seq"), QString("source.node_seq")})
        for (const auto &value :
             {QString("00"), QString("07"), QString("+7"), QString(" 7"), QString("7 "), QString("-0"), QString("-1"),
              QString("1e1"), QString("0x7"), QString("７"), QString("18446744073709551616")})
        {
            auto document = savedProject(
                {savedNode(field == "node_seq" ? value : "7", field == "source.node_seq" ? value : "7")});
            if (field.startsWith("next_"))
            {
                auto header = document["project"].toObject();
                header.insert(field, value);
                document.insert("project", header);
            }
            put(file, QJsonDocument(document).toJson());
            bool rejected = false;
            try
            {
                storage.load(path(file));
            }
            catch (const ContractError &error)
            {
                const auto message = QString::fromUtf8(error.what());
                require(message.contains(field) && message.contains("非规范序号.vrt.json"),
                        "sequence rejection lost the project file or field context");
                rejected = true;
            }
            require(rejected, "project accepted a noncanonical unsigned sequence");
        }
}
}

int main()
{
    try
    {
        QTemporaryDir temporary;
        require(temporary.isValid(), "temporary directory unavailable");
        knownFailureDiagnostics(temporary.path());
        candidateAndRawPreservation(temporary.path());
        reconciliationAndIdentities(temporary.path());
        typedArraysAndRanges(temporary.path());
        saveAsPaths(temporary.path());
        retiredInitialization(temporary.path());
        wideIdsAndCycleMembership(temporary.path());
        canonicalSequences(temporary.path());
        dormantLiteralSources(temporary.path());
        std::cout << "project storage JSON contract passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
