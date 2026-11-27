#include "ToolResultAssertions.h"
#include "core/DefinitionJson.h"
#include "core/NodeContracts.h"
#include "core/ProjectArchive.h"
#include "core/ProjectEditor.h"
#include "core/ProjectStorage.h"
#include "core/ToolRegistry.h"
#include "core/ValueJson.h"

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

void putArchive(const QString &filename, const QByteArray &json)
{
    ProjectArchive::writeProgramJson(path(filename), json);
}

QByteArray getArchive(const QString &filename)
{
    return ProjectArchive::readProgramJson(path(filename));
}

bool has(const std::vector<ProjectStorageDiagnostic> &diagnostics, ProjectStorageCode code)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [code](const auto &diagnostic) { return diagnostic.code == code; });
}

QJsonObject paramEntry(PortId port, const TypeSpec &type, const QJsonValue &value)
{
    return QJsonObject{
        { "port",            static_cast<int>(port)},
        { "type",      ValueJson::encodeType(type)},
        {"value",                            value}
    };
}

QJsonObject connectionEntry(const QString &fromNode, PortId fromPort, const QString &toNode, PortId toPort,
                            const QString &toStage = QStringLiteral("inputs"))
{
    return QJsonObject{
        {"from", QJsonObject{{"node", fromNode}, {"port", static_cast<int>(fromPort)}}},
        {  "to", QJsonObject{{"node", toNode}, {"stage", toStage}, {"port", static_cast<int>(toPort)}}}
    };
}

QJsonObject savedNode(const QString &id = "7", const QString &toolId = "unavailable",
                      const QString &toolName = "原始名称", const QString &version = "1.0.0",
                      QJsonValue inputLiteral = qint64{9007199254740993LL})
{
    QJsonObject params{
        {  "init", QJsonArray{paramEntry(0, {Type::String}, QStringLiteral("保留中文"))}},
        {"inputs", QJsonArray{paramEntry(1, {Type::Int}, inputLiteral)}}
    };
    return QJsonObject{
        {    "id", id},
        {  "tool", QJsonObject{{"id", toolId}, {"name", toolName}, {"version", version}}},
        { "alias", QString()},
        {"params", params}
    };
}

QJsonObject savedProject(const QJsonArray &nodes, const QJsonArray &layout, const QJsonArray &connections,
                         const QString &nextNode = "8", const QString &nextFlow = "2")
{
    return {
        {"format_version", 10},
        {       "project", QJsonObject{{"name", "缺失工具"},
                                       {"notes", ""},
                                       {"app_version", "0.2.0"},
                                       {"contracts_version", "0.1.0"},
                                       {"next_node_seq", nextNode},
                                       {"next_flow_seq", nextFlow}}},
        {         "flows", QJsonArray{QJsonObject{{"id", "flow_1"},
                                                  {"name", "流程1"},
                                                  {"run_selected", true},
                                                  {"nodes", nodes},
                                                  {"layout", layout},
                                                  {"connections", connections}}}}
    };
}

ToolDefinition scalarTool()
{
    ToolDefinition tool;
    tool.nameEn  = "unavailable";
    tool.nameZh  = "原始名称";
    tool.version = "1.0.0";
    tool.kind    = ToolKind::Cpp;

    PortDefinition value;
    value.id           = 1;
    value.nameZh       = "值";
    value.type         = {Type::Int};
    value.defaultValue = std::int64_t{4};
    tool.inputs.push_back(value);
    value.id = 2;
    tool.outputs.push_back(value);
    PortDefinition restored;
    restored.id     = 0;
    restored.nameZh = "恢复";
    restored.type   = {Type::String};
    tool.initParams.push_back(restored);
    return tool;
}

void candidateAndRawPreservation(const QString &directory)
{
    ToolRegistry   registry;
    ProjectStorage storage(registry);
    const auto     source = directory + "/缺失.vrtapp";
    putArchive(source, QJsonDocument(savedProject({savedNode("7")},
                                                  {QJsonObject{{"node", "7"}, {"col", 0}, {"row", 0}}},
                                                  {connectionEntry("7", 2, "7", 1)}))
                           .toJson());
    auto loaded = storage.load(path(source));
    require(!loaded.project.dirty && has(loaded.diagnostics, ProjectStorageCode::DefinitionMissing),
            "missing node was not retained cleanly");
    require(has(loaded.diagnostics, ProjectStorageCode::Cycle), "loaded cycle not diagnosed");
    loaded.project.flows[0].nodes[0].alias = "可编辑备注";
    const auto saved                       = directory + "/保留.vrtapp";
    storage.save(loaded.project, path(saved));
    const auto root = QJsonDocument::fromJson(getArchive(saved)).object();
    const auto flow = root.value("flows").toArray()[0].toObject();
    const auto raw  = flow.value("nodes").toArray()[0].toObject();
    require(root.value("format_version").toInteger() == 10 && raw.value("id").isString(),
            "project version or identity encoding is wrong");
    require(raw.value("params").toObject().value("inputs").toArray()[0].toObject().value("value").toInteger()
                == 9007199254740993LL,
            "int64 precision lost");
    require(raw.value("alias") == "可编辑备注", "alias lost");
    require(flow.value("connections").toArray().size() == 1, "missing node connection lost");

    const auto before    = getArchive(saved);
    const auto priorPath = loaded.project.path;
    loaded.project.dirty = true;
    bool failed          = false;
    try
    {
        storage.save(loaded.project, path(directory + "/absent/failed.vrtapp"));
    }
    catch (const std::exception &)
    {
        failed = true;
    }
    require(failed && loaded.project.path == priorPath && loaded.project.dirty && getArchive(saved) == before,
            "failed save installed candidate");

    auto copied                   = loaded.project;
    auto duplicate                = copied.flows[0].nodes[0];
    duplicate.id                  = 9;
    duplicate.position.x          = 1;
    duplicate.inputs.at(1).source = SourcePort{9, 2};
    copied.flows[0].nodes.push_back(std::move(duplicate));
    copied.nextNodeId = 10;
    storage.save(copied, path(directory + "/copied.vrtapp"));
    const auto reopened = storage.load(copied.path);
    require(reopened.project.flows[0].nodes[1].id == 9
                && reopened.project.flows[0].nodes[1].inputs.at(1).source->node == 9
                && loaded.project.flows[0].nodes[0].unresolved.value("id") == "7",
            "unknown node copy shared identity");

    for (const auto &text : {QByteArray("[1,2]"), QByteArray("{\"format_version\":9}"),
                             QByteArray("{\"format_version\":10,\"project\":[]}"), QByteArray("{\"flows\":[1,]}")})
    {
        putArchive(source, text);
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
}

void reconciliationAndIdentities(const QString &directory)
{
    ToolRegistry registry;
    const auto   tool = scalarTool();
    registry.addDefinition(tool);
    ProjectStorage storage(registry);
    const auto     file     = directory + "/reconcile.vrtapp";
    const auto     document = savedProject({savedNode("7")},
                                           {QJsonObject{{"node", "7"}, {"col", 0}, {"row", 0}}},
                                           {connectionEntry("7", 2, "7", 1)}, "1", "1");
    putArchive(file, QJsonDocument(document).toJson());
    auto        recovered = storage.load(path(file));
    const auto &node      = recovered.project.flows[0].nodes[0];
    require(node.unresolved.isEmpty() && std::get<std::string>(node.initParams.at(0).literal) == "保留中文",
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
        auto layout      = flow.value("layout").toArray();
        auto damagedNode = nodes[0].toObject();
        if (damage == 0)
            invalid.insert("flows", QJsonArray{flow, flow});
        if (damage == 1)
            nodes.append(nodes[0]);
        if (damage == 2)
            flow.insert("id", "");
        if (damage == 3)
            damagedNode.insert("id", "18446744073709551616");
        if (damage == 4)
            damagedNode.insert("id", qint64{7});
        if (damage == 5)
            flow.insert("run_selected", "true");
        if (damage == 6)
        {
            auto l0 = layout[0].toObject();
            l0.insert("col", 0.5);
            layout[0] = l0;
            flow.insert("layout", layout);
        }
        if (damage == 7)
        {
            auto second = flow;
            second.insert("id", "flow_2");
            invalid.insert("flows", QJsonArray{flow, second});
        }
        if (damage != 0 && damage != 7)
        {
            nodes[0] = damagedNode;
            flow.insert("nodes", nodes);
            invalid.insert("flows", QJsonArray{flow});
        }
        putArchive(file, QJsonDocument(invalid).toJson());
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

    // Valid source -> target connection.
    const auto sourceNode = savedNode("8");
    const auto targetNode = savedNode("7");
    putArchive(file, QJsonDocument(savedProject({targetNode, sourceNode},
                                                {QJsonObject{{"node", "7"}, {"col", 0}, {"row", 1}},
                                                 QJsonObject{{"node", "8"}, {"col", 0}, {"row", 0}}},
                                                {connectionEntry("8", 2, "7", 1)}, "9"))
                         .toJson());
    require(storage.load(path(file)).project.flows[0].nodes[0].inputs.at(1).source->node == 8,
            "valid connection not loaded");

    // Type change resets literal to new default and keeps upstream connection when both ends match.
    auto changedTool                   = tool;
    changedTool.inputs[0].type         = changedTool.outputs[0].type = {Type::Float};
    changedTool.inputs[0].defaultValue = changedTool.outputs[0].defaultValue = 2.5;
    ToolRegistry changedRegistry;
    changedRegistry.addDefinition(changedTool);
    const auto  changed = ProjectStorage(changedRegistry).load(path(file));
    const auto &binding = changed.project.flows[0].nodes[0].inputs.at(1);
    require(binding.source && binding.source->node == 8 && binding.mode == BindingMode::Upstream
                && std::get<double>(binding.literal) == 2.5,
            "schema change did not reset incompatible literal to declared default");
    require(has(changed.diagnostics, ProjectStorageCode::SchemaChanged),
            "a type-incompatible input was not diagnosed");

    // Missing upstream tool keeps PendingReference.
    const auto missingSource = savedNode("8", "still_missing");
    putArchive(file, QJsonDocument(savedProject({targetNode, missingSource},
                                                {QJsonObject{{"node", "7"}, {"col", 0}, {"row", 1}},
                                                 QJsonObject{{"node", "8"}, {"col", 0}, {"row", 0}}},
                                                {connectionEntry("8", 2, "7", 1)}, "9"))
                         .toJson());
    const auto pending = storage.load(path(file));
    require(pending.project.flows[0].nodes[0].inputs.at(1).source.has_value()
                && has(pending.diagnostics, ProjectStorageCode::PendingReference),
            "unknown source cleared");

    // Absent source node keeps InvalidReference.
    putArchive(file, QJsonDocument(savedProject({targetNode},
                                                {QJsonObject{{"node", "7"}, {"col", 0}, {"row", 1}}},
                                                {connectionEntry("8", 2, "7", 1)}, "9"))
                         .toJson());
    const auto absentSource = storage.load(path(file));
    require(absentSource.project.flows[0].nodes[0].inputs.at(1).source.has_value()
                && has(absentSource.diagnostics, ProjectStorageCode::InvalidReference),
            "an absent source was silently unbound instead of reported");
}

void typedArraysAndRanges(const QString &directory)
{
    QJsonObject node{
        {    "id", "7"},
        {  "tool", QJsonObject{{"id", "unavailable"}, {"name", "原始名称"}, {"version", "1.0.0"}}},
        { "alias", ""},
        {"params", QJsonObject{{"inputs", QJsonArray{paramEntry(1, {Type::Array, DType::Float32, 2},
                                                                QJsonArray{QJsonArray{}, QJsonArray{}})}}}}
    };
    auto tool = scalarTool();
    tool.initParams.clear();
    tool.outputs.clear();
    tool.inputs[0].type         = {Type::Array, DType::Int64, 2};
    tool.inputs[0].defaultValue = Missing{};
    ToolRegistry registry;
    registry.addDefinition(tool);
    const auto file = directory + "/array.vrtapp";
    putArchive(file, QJsonDocument(savedProject({node}, {QJsonObject{{"node", "7"}, {"col", 0}, {"row", 0}}}, {}))
                         .toJson());
    const auto  changed = ProjectStorage(registry).load(path(file));
    const auto &retyped = changed.project.flows[0].nodes[0].inputs.at(1).literal;
    require(std::holds_alternative<Array>(retyped)
                && std::get<Array>(retyped).type() == TypeSpec{Type::Array, DType::Int64, 2}
                && std::get<Array>(retyped).elementCount() == 0
                && has(changed.diagnostics, ProjectStorageCode::SchemaChanged),
            "a dtype change did not reset incompatible array to declared default");

    tool.inputs[0].type.dtype = DType::Float32;
    ToolRegistry oldRegistry;
    oldRegistry.addDefinition(tool);
    const auto  old   = ProjectStorage(oldRegistry).load(path(file));
    const auto &array = std::get<Array>(old.project.flows[0].nodes[0].inputs.at(1).literal);
    const auto  rows  = nestedOf<NestedVector<float, 2>>(array);
    require(rows.size() == 2 && rows[0].empty() && rows[1].empty(), "empty nested rows lost");

    auto rangedTool              = scalarTool();
    rangedTool.inputs[0].maximum = std::int64_t{10};
    ToolRegistry rangedRegistry;
    rangedRegistry.addDefinition(rangedTool);
    const auto rangedNode = savedNode("7");
    putArchive(file, QJsonDocument(savedProject({rangedNode},
                                                {QJsonObject{{"node", "7"}, {"col", 0}, {"row", 0}}}, {}))
                         .toJson());
    const auto ranged = ProjectStorage(rangedRegistry).load(path(file));
    require(std::get<std::int64_t>(ranged.project.flows[0].nodes[0].inputs.at(1).literal) == 10
                && has(ranged.diagnostics, ProjectStorageCode::RangeWarning) && ranged.project.dirty,
            "saved range violation was not clamped to the declared maximum");

    ToolRegistry unconstrained;
    unconstrained.addDefinition(scalarTool());
    auto beforeClamp                                          = ProjectStorage(unconstrained).load(path(file)).project;
    beforeClamp.flows.front().nodes.front().executionRevision = 19;
    const auto clamped                                        = ProjectStorage(rangedRegistry).reconcile(beforeClamp);
    require(clamped.project.flows.front().nodes.front().executionRevision == 20,
            "normalizing an effective literal did not invalidate its execution revision");
    beforeClamp.flows.front().nodes.front().inputs.at(1).mode   = BindingMode::Upstream;
    beforeClamp.flows.front().nodes.front().inputs.at(1).source = SourcePort{7, 2};
    const auto dormant                                          = ProjectStorage(rangedRegistry).reconcile(beforeClamp);
    require(dormant.project.flows.front().nodes.front().executionRevision == 19,
            "a dormant literal invalidated the execution revision");

    auto overflow = QJsonDocument(savedProject({rangedNode},
                                               {QJsonObject{{"node", "7"}, {"col", 0}, {"row", 0}}}, {}))
                        .toJson();
    overflow.replace("9007199254740993", "-9223372036854775809");
    putArchive(file, overflow);
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
    PortId next = 1;
    for (const auto &id : {"path", "text", "empty", "remote", "absolute"})
    {
        PortDefinition port;
        port.id     = next++;
        port.nameEn = id;
        port.nameZh = id;
        port.type   = {Type::String};
        port.editor = std::string(id) == "text" ? EditorKind::Text : EditorKind::Path;
        tool.inputs.push_back(port);
    }
    PortDefinition dynamic;
    dynamic.id     = next++;
    dynamic.nameZh = "动态路径";
    dynamic.type   = {Type::String};
    dynamic.editor = EditorKind::Path;
    tool.inputs.push_back(dynamic);
    PortDefinition folder = tool.inputs[0];
    folder.id             = 0;
    folder.nameEn         = "folder";
    folder.editor         = EditorKind::Path;
    tool.initParams.push_back(folder);
    registry.addDefinition(tool);

    ProjectStorage    storage(registry);
    ProjectDefinition project;
    NodeDefinition    node;
    node.id                 = 0;
    node.toolId             = tool.nameEn;
    node.toolNameSnapshot   = tool.nameZh;
    node.toolVersionSnapshot = tool.version;
    node.inputs[1]          = {BindingMode::Upstream, std::string("图像/不存在.png"), std::nullopt};
    node.inputs[2].literal  = std::string("图像/不存在.png");
    node.inputs[3].literal  = std::string("");
    node.inputs[4].literal  = std::string("Z:/异盘/资源.png");
    node.inputs[6].literal  = std::string("动态/图像.png");
    node.initParams[0].literal = std::string("目录");
    const auto oldDirectory = directory + "/旧位置";
    const auto newDirectory = directory + "/新位置";
    const auto absolutePath = utf8Path(path(oldDirectory) / std::filesystem::path(u8"绝对.png"));
    node.inputs[5].literal  = std::string(absolutePath);
    for (const auto &port : tool.inputs)
        node.signatures.inputs.emplace(port.id, port.type);
    node.signatures.initParams.emplace(folder.id, folder.type);
    project.flows[0].nodes.push_back(node);
    project.nextNodeId = 1;
    require(QDir().mkpath(oldDirectory) && QDir().mkpath(newDirectory), "cannot create save-as directories");
    storage.save(project, path(oldDirectory + "/首次"));
    require(project.path.filename() == std::filesystem::path(u8"首次.vrtapp") && !project.dirty,
            "first save did not append .vrtapp extension or clear dirty");
    require(!project.appVersion.empty() && !project.contractsVersion.empty(),
            "save did not record app_version and contracts_version");
    project.dirty = true;
    storage.save(project, path(newDirectory + "/另存.vrtapp"));
    const auto &saved = project.flows[0].nodes[0];
    require(std::get<std::string>(saved.inputs.at(1).literal) == "图像/不存在.png",
            "save-as rewrote a hidden upstream literal");
    require(std::get<std::string>(saved.initParams.at(0).literal) == "目录", "save-as rewrote a directory parameter");
    require(std::get<std::string>(saved.inputs.at(6).literal) == "动态/图像.png", "save-as rewrote a path input");
    require(std::get<std::string>(saved.inputs.at(4).literal) == "Z:/异盘/资源.png",
            "cross-drive resource lost its absolute path");
    auto reopened = storage.load(project.path);
    require(std::get<std::string>(reopened.project.flows[0].nodes[0].inputs.at(1).literal) == "图像/不存在.png"
                && reopened.project.flows[0].nodes[0].inputs.at(1).mode == BindingMode::Upstream,
            "reopened literal or unbound upstream mode differs from saved value");
}

void removedParametersDropped(const QString &directory)
{
    auto tool = scalarTool();
    ToolRegistry registry;
    registry.addDefinition(tool);
    ProjectStorage storage(registry);

    // Extra init param 9 not in declaration must be dropped with SchemaChanged, not kept in retired_init_params.
    QJsonObject raw = savedNode("7");
    auto        params = raw["params"].toObject();
    auto        inits  = params["init"].toArray();
    inits.append(paramEntry(9, {Type::Int}, 123));
    params["init"] = inits;
    raw["params"]  = params;

    const auto input  = directory + "/removed-param.vrtapp";
    const auto output = directory + "/cleaned-param.vrtapp";
    putArchive(input, QJsonDocument(savedProject({raw}, {QJsonObject{{"node", "7"}, {"col", 0}, {"row", 0}}}, {}))
                          .toJson());
    auto loaded = storage.load(path(input));
    require(has(loaded.diagnostics, ProjectStorageCode::SchemaChanged) && loaded.project.dirty,
            "undeclared init parameter was not diagnosed");
    require(!loaded.project.flows[0].nodes[0].initParams.contains(9),
            "undeclared init parameter was not dropped");

    storage.save(loaded.project, path(output));
    const auto savedNodeJson = QJsonDocument::fromJson(getArchive(output))
                                   .object()["flows"]
                                   .toArray()[0]
                                   .toObject()["nodes"]
                                   .toArray()[0]
                                   .toObject();
    require(!savedNodeJson.contains("retired_init_params") && !savedNodeJson.contains("type_snapshot"),
            "legacy retired_init_params or type_snapshot written to format 9");
    const auto reopened = storage.load(path(output));
    require(reopened.diagnostics.empty() && !reopened.project.dirty,
            "cleaned project still produced diagnostics after save");
}

void wideIdsAndCycleMembership(const QString &directory)
{
    ToolRegistry   registry;
    ProjectStorage storage(registry);
    const auto     file = directory + "/wide.vrtapp";
    for (const auto &id : {QString("0"), QString("1"), QString("9007199254740993"), QString("18446744073709551615")})
    {
        const auto next = id == "18446744073709551615" ? id : QString::number(id.toULongLong() + 1);
        putArchive(file, QJsonDocument(savedProject({savedNode(id)},
                                                    {QJsonObject{{"node", id}, {"col", 0}, {"row", 0}}},
                                                    {connectionEntry(id, 2, id, 1)}, next))
                             .toJson());
        auto loaded = storage.load(path(file));
        storage.save(loaded.project);
        const auto reopened = storage.load(path(file));
        require(reopened.project.flows[0].nodes[0].id == id.toULongLong()
                    && reopened.project.nextNodeId == next.toULongLong(),
                "uint64 precision lost");
        if (id == "18446744073709551615")
            require(has(reopened.diagnostics, ProjectStorageCode::SequenceExhausted), "ID exhaustion not diagnosed");
    }

    putArchive(file,
               QJsonDocument(savedProject(
                                 {savedNode("7"), savedNode("8"), savedNode("9")},
                                 {QJsonObject{{"node", "7"}, {"col", 0}, {"row", 0}},
                                  QJsonObject{{"node", "8"}, {"col", 1}, {"row", 0}},
                                  QJsonObject{{"node", "9"}, {"col", 2}, {"row", 0}}},
                                 {connectionEntry("8", 2, "7", 1), connectionEntry("7", 2, "8", 1),
                                  connectionEntry("8", 2, "9", 1)},
                                 "10"))
                   .toJson());
    const auto       cyclic = storage.load(path(file));
    std::set<NodeId> members;
    for (const auto &diagnostic : cyclic.diagnostics)
        if (diagnostic.code == ProjectStorageCode::Cycle)
            members.insert(*diagnostic.node);
    require(members == std::set<NodeId>{7, 8}, "downstream-only nodes included in cycle");
}

void dormantLiteralSources(const QString &directory)
{
    ToolRegistry   registry;
    ToolDefinition defA;
    defA.nameEn  = "test.producer";
    defA.nameZh  = "生产者";
    defA.version = "1.0.0";
    defA.outputs = {{2, "输出", {Type::Int}}};
    registry.addDefinition(defA);

    ToolDefinition defB;
    defB.nameEn  = "test.consumer";
    defB.nameZh  = "消费者";
    defB.version = "1.0.0";
    defB.inputs  = {{1, "输入", {Type::Int}}};
    registry.addDefinition(defB);

    ProjectEditor editor;
    const auto    pA = editor.addNode("flow_1", defA, {0, 100});
    const auto    pB = editor.addNode("flow_1", defB, {0, 50});

    auto  project   = editor.project();
    auto &nodeB     = project.flows[0].nodes[1];
    auto &binding   = nodeB.inputs[1];
    binding.mode    = BindingMode::Literal;
    binding.literal = std::int64_t{42};
    binding.source  = SourcePort{pA, 2};

    const auto filePath = path(directory + "/dormant_literal.vrtapp");
    ProjectStorage(registry).save(project, filePath);

    const auto loaded = ProjectStorage(registry).load(filePath);
    for (const auto &diag : loaded.diagnostics)
        require(diag.field.rfind("inputs.", 0) != 0, "Literal mode with dormant source produced false diagnostic");
    const auto &loadedBinding = loaded.project.flows[0].nodes[1].inputs.at(1);
    require(loadedBinding.mode == BindingMode::Literal && std::get<std::int64_t>(loadedBinding.literal) == 42,
            "Literal binding corrupted");
    const auto check = validateBinding(loaded.project.flows[0], pA, 2, pB, 1);
    require(!check && check.reason == BindingError::SameOrUpperRow, "Upstream mode did not reject illegal edge");
}

void canonicalSequences(const QString &directory)
{
    ToolRegistry   registry;
    ProjectStorage storage(registry);
    const auto     file = directory + "/非规范序号.vrtapp";
    for (const auto &field :
         {QString("node.id"), QString("next_node_seq"), QString("next_flow_seq"), QString("connection.from.node")})
        for (const auto &value :
             {QString("00"), QString("07"), QString("+7"), QString(" 7"), QString("7 "), QString("-0"), QString("-1"),
              QString("1e1"), QString("0x7"), QString("７"), QString("18446744073709551616")})
        {
            const auto nodeId   = field == "node.id" ? value : QString("7");
            const auto fromNode = field == "connection.from.node" ? value : QString("7");
            auto       document = savedProject({savedNode(nodeId)},
                                               {QJsonObject{{"node", nodeId}, {"col", 0}, {"row", 0}}},
                                               {connectionEntry(fromNode, 2, "7", 1)});
            if (field.startsWith("next_"))
            {
                auto header = document["project"].toObject();
                header.insert(field, value);
                document.insert("project", header);
            }
            putArchive(file, QJsonDocument(document).toJson());
            bool rejected = false;
            try
            {
                storage.load(path(file));
            }
            catch (const ContractError &error)
            {
                const auto message = QString::fromUtf8(error.what());
                require(message.contains(field) && message.contains("非规范序号.vrtapp"),
                        "sequence rejection lost the project file or field context");
                rejected = true;
            }
            require(rejected, "project accepted a noncanonical unsigned sequence");
        }
}

void toolUpdatesCutIncompatibleWires(const QString &directory)
{
    ToolDefinition upstream = scalarTool();
    upstream.nameZh         = "上游";
    upstream.version        = "1.0.0";
    upstream.initParams.clear();
    upstream.inputs.clear();

    ToolDefinition sink;
    sink.nameEn  = "sink";
    sink.nameZh  = "下游";
    sink.version = "1.0.0";
    sink.kind    = ToolKind::Cpp;
    PortDefinition value;
    value.id     = 1;
    value.nameZh = "值";
    value.type   = {Type::Int};
    sink.inputs.push_back(value);
    value.id = 2;
    sink.outputs.push_back(value);

    ToolRegistry original;
    original.addDefinition(upstream);
    original.addDefinition(sink);

    const auto file = directory + "/tool-update.vrtapp";
    QJsonObject upNode{
        {   "id", "7"},
        { "tool", QJsonObject{{"id", "unavailable"}, {"name", "上游"}, {"version", "1.0.0"}}},
        {"alias", ""}
    };
    QJsonObject downNode{
        {    "id", "8"},
        {  "tool", QJsonObject{{"id", "sink"}, {"name", "下游"}, {"version", "1.0.0"}}},
        { "alias", ""},
        {"params", QJsonObject{{"inputs", QJsonArray{paramEntry(1, {Type::Int}, qint64{4})}}}}
    };
    const auto project = savedProject({upNode, downNode},
                                      {QJsonObject{{"node", "7"}, {"col", 0}, {"row", 0}},
                                       QJsonObject{{"node", "8"}, {"col", 0}, {"row", 1}}},
                                      {connectionEntry("7", 2, "8", 1)}, "9");
    putArchive(file, QJsonDocument(project).toJson());
    require(ProjectStorage(original).load(path(file)).toolUpdates.empty(),
            "an unchanged declaration was reported as a tool update");

    // Retype upstream output to String: wire cut & ToolUpdate reports both ends and cause.
    ToolDefinition retyped               = upstream;
    retyped.version                      = "2.0.0";
    retyped.outputs.front().type         = {Type::String};
    retyped.outputs.front().defaultValue = std::string{};
    ToolRegistry replaced;
    replaced.addDefinition(retyped);
    replaced.addDefinition(sink);
    const auto updated = ProjectStorage(replaced).load(path(file));
    require(updated.project.dirty, "cutting a wire did not mark the project dirty");
    require(updated.toolUpdates.size() == 1, "one changed tool produced more than one notice");
    const auto &notice = updated.toolUpdates.front();
    require(notice.toolId == "unavailable" && notice.savedVersion == "1.0.0" && notice.currentVersion == "2.0.0",
            "the notice lost the version that moved");
    require(notice.nodes.size() == 1 && notice.nodes.front() == "上游-7", "the notice did not name the affected node");
    require(notice.kept == 0 && notice.removed.size() == 1, "the notice did not report the removed wire");
    require(notice.removed.front().reason.find("类型") != std::string::npos, "the removal did not name its cause");
    require(notice.removed.front().source.find("上游-7") != std::string::npos
                && notice.removed.front().target.find("下游-8") != std::string::npos,
            "the removal did not name both ends");
    const auto &binding = updated.project.flows[0].nodes[1].inputs.at(1);
    require(binding.mode == BindingMode::Literal && !binding.source, "an unusable wire stayed in the project");

    auto kept = updated.project;
    ProjectStorage(replaced).save(kept, path(file));
    require(ProjectStorage(replaced).load(path(file)).toolUpdates.empty(), "the same declaration was reported again");

    // Version bump with intact wiring.
    putArchive(file, QJsonDocument(project).toJson());
    ToolDefinition patched = upstream;
    patched.version        = "1.0.1";
    ToolRegistry compatible;
    compatible.addDefinition(patched);
    compatible.addDefinition(sink);
    const auto bumped = ProjectStorage(compatible).load(path(file));
    require(bumped.toolUpdates.size() == 1 && bumped.toolUpdates.front().kept == 1
                && bumped.toolUpdates.front().removed.empty() && !bumped.project.dirty,
            "a version bump with an intact wiring was not announced as kept only");

    // Both ends change to String -> wire stays connected!
    ToolDefinition retypedSink              = sink;
    retypedSink.version                     = "2.0.0";
    retypedSink.inputs.front().type         = {Type::String};
    retypedSink.inputs.front().defaultValue = std::string{};
    ToolRegistry bothChanged;
    bothChanged.addDefinition(retyped);
    bothChanged.addDefinition(retypedSink);
    const auto  sameType = ProjectStorage(bothChanged).load(path(file));
    const auto &retained = sameType.project.flows.front().nodes.back().inputs.at(1);
    require(retained.mode == BindingMode::Upstream && retained.source == SourcePort{7, 2},
            "updating both ends to the same type removed a valid connection");
    require(dependencyEdges(sameType.project.flows.front()).size() == 1,
            "a retained connection lost its execution dependency");

    // Removed input cuts wire and names both ends.
    putArchive(file, QJsonDocument(project).toJson());
    ToolDefinition removedInput = sink;
    removedInput.version        = "2.0.0";
    removedInput.inputs.clear();
    ToolRegistry removed;
    removed.addDefinition(upstream);
    removed.addDefinition(removedInput);
    const auto deleted = ProjectStorage(removed).load(path(file));
    require(deleted.toolUpdates.size() == 1 && deleted.toolUpdates.front().removed.size() == 1,
            "deleting the target input omitted the removed connection from the update notice");
    require(!deleted.project.flows.front().nodes.back().inputs.contains(1)
                && dependencyEdges(deleted.project.flows.front()).empty(),
            "a removed input kept an execution dependency");
}

void rejectsLegacyFields(const QString &directory)
{
    auto         tool = scalarTool();
    ToolRegistry registry;
    registry.addDefinition(tool);
    ProjectEditor editor;
    editor.addNode(editor.project().flows.front().id, tool, {0, 0});
    const auto     filename = directory + "/legacy-fields.vrtapp";
    ProjectStorage storage(registry);
    auto           project = editor.project();
    storage.save(project, path(filename));

    for (const char *legacyKey : {"execution_guard", "retired_init_params", "type_snapshot"})
    {
        auto document  = QJsonDocument::fromJson(getArchive(filename)).object();
        auto flows     = document["flows"].toArray();
        auto flow      = flows[0].toObject();
        auto nodes     = flow["nodes"].toArray();
        auto node      = nodes[0].toObject();
        node[legacyKey] = QJsonObject{};
        nodes[0]        = node;
        flow["nodes"]   = nodes;
        flows[0]        = flow;
        document["flows"] = flows;
        const auto badFile = directory + "/bad-" + legacyKey + ".vrtapp";
        putArchive(badFile, QJsonDocument(document).toJson());
        bool rejected = false;
        try
        {
            (void)storage.load(path(badFile));
        }
        catch (const ContractError &error)
        {
            rejected = std::string(error.what()).find(legacyKey) != std::string::npos;
        }
        require(rejected, "legacy removed field was not rejected");
    }
}

void modelReferences(const QString &directory)
{
    const auto declaration = R"json({
  "schema_version": 4,
  "name_en": "ProbeAssets",
  "name_zh": "资产探针",
  "version": "1.0.0",
  "implementation": { "kind": "cpp" },
  "train_params": [
    { "id": 7, "name_zh": "模型路径", "type": "string" },
    { "id": 0, "name_zh": "样本", "type": "training_set" }
  ],
  "inputs": [
    { "id": 1, "name_zh": "图像", "type": "image" }
  ],
  "outputs": [
    { "id": 2, "name_zh": "模型路径", "type": "string" }
  ],
  "training": {
    "datasets": [
      {
        "parameter": 0,
        "image": { "stage": "inputs", "id": 1 },
        "max_records": 1,
        "fields": [
          { "id": 1, "name_zh": "采样区", "type": "rectangle", "constraints": { "regions": { "min": 1, "max": 2 } } }
        ]
      }
    ],
    "artifacts": [
      { "id": "model", "source_port": 2, "format": "probe.model", "version": 1, "target": { "stage": "train_params", "id": 7 } }
    ]
  }
})json";
    ToolRegistry registry;
    auto         tool   = parseToolDefinitionJson(declaration);
    tool.definitionPath = path(directory + "/ProbeAssets.json");
    registry.addDefinition(tool);

    ProjectEditor editor;
    const auto    nodeId = editor.addNode("flow_1", tool, {0, 0});
    const auto    model  = path(directory) / "assets/model.zip";
    const std::vector<LiteralWrite> writes{{PortStage::Train, 7, std::string("assets/model.zip")}};
    require(editor.setLiteralWrites("flow_1", nodeId, writes, [] {}), "the model reference was not stored");

    const auto file = directory + "/assets.vrtapp";
    auto       project = editor.project();
    ProjectStorage(registry).save(project, path(file));
    const auto loaded = ProjectStorage(registry).load(path(file));
    const auto &node  = loaded.project.flows[0].nodes[0];
    require(nodeParameters(node, tool).empty(), "models must not be copied into hidden initialization parameters");
    const auto models = nodeModelReferences(node, tool, path(directory));
    require(models.at("model") == model.lexically_normal(),
            "the editable model path did not resolve against the resource root");
}
void stageBindingsPersistAndRoundTrip(const QString &directory)
{
    ToolDefinition tool;
    tool.nameEn  = "test.stage_storage";
    tool.nameZh  = "三阶段存储";
    tool.version = "1.0.0";
    tool.kind    = ToolKind::Cpp;
    tool.initParams = {
        {0, "初始化整数", {Type::Int}, std::int64_t{7}},
        {1, "初始化未连", {Type::Int}, std::int64_t{8}},
        {2, "初始化图像", {Type::Image}, DataRef<Image>(Image{})}
    };
    tool.trainParams = {
        {0, "训练路径", {Type::String}, std::string("m.zip")}
    };
    tool.inputs = {
        {1, "输入整数", {Type::Int}, std::int64_t{3}},
        {2, "输入图像", {Type::Image}, DataRef<Image>(Image{})}
    };
    tool.outputs = {
        {2, "输出整数", {Type::Int}},
        {3, "输出路径", {Type::String}},
        {4, "输出图像", {Type::Image}}
    };

    ToolRegistry registry;
    registry.addDefinition(tool);
    ProjectEditor editor;
    const auto    src = editor.addNode("flow_1", *registry.find(tool.nameEn), {0, 0});
    const auto    dst = editor.addNode("flow_1", *registry.find(tool.nameEn), {0, 1});
    editor.bindFreeInput("flow_1", {src, 2}, {dst, 0, PortStage::Init});
    editor.setMode("flow_1", dst, 1, BindingMode::Upstream, PortStage::Init);
    editor.bindFreeInput("flow_1", {src, 4}, {dst, 2, PortStage::Init});
    editor.bindFreeInput("flow_1", {src, 3}, {dst, 0, PortStage::Train});
    editor.bindFreeInput("flow_1", {src, 2}, {dst, 1, PortStage::Inputs});

    auto           project = editor.project();
    const auto     file    = directory + "/stage-bindings.vrtapp";
    ProjectStorage storage(registry);
    storage.save(project, path(file));

    const auto root = QJsonDocument::fromJson(getArchive(file)).object();
    require(root.value("format_version").toInteger() == 10, "format_version must be 10");
    const auto conns = root.value("flows").toArray()[0].toObject().value("connections").toArray();
    require(conns.size() == 4, "all stage connections must be serialized");
    bool sawInit = false, sawTrain = false, sawInputs = false;
    for (const auto &entry : conns)
    {
        const auto stage = entry.toObject().value("to").toObject().value("stage").toString();
        sawInit |= stage == "init";
        sawTrain |= stage == "train";
        sawInputs |= stage == "inputs";
    }
    require(sawInit && sawTrain && sawInputs, "connections must carry init, train and inputs stages");

    const auto loaded = storage.load(path(file));
    require(loaded.diagnostics.empty() && !loaded.project.dirty, "stage binding roundtrip must load cleanly");
    const auto &restored = loaded.project.flows[0].nodes[1];
    require(restored.initParams.at(0).mode == BindingMode::Upstream
                && restored.initParams.at(0).source == SourcePort{src, 2},
            "wired initParam did not roundtrip");
    require(restored.initParams.at(1).mode == BindingMode::Upstream && !restored.initParams.at(1).source,
            "unwired upstream initParam did not roundtrip");
    require(restored.initParams.at(2).mode == BindingMode::Upstream
                && restored.initParams.at(2).source == SourcePort{src, 4},
            "wired init image did not roundtrip");
    require(restored.trainParams.at(0).mode == BindingMode::Upstream
                && restored.trainParams.at(0).source == SourcePort{src, 3},
            "wired trainParam did not roundtrip");

    // When an upstream image output changes type, reconcile cuts the wire while keeping Image in Upstream mode.
    auto changedTool = tool;
    changedTool.outputs[2].type = {Type::String};
    ToolRegistry changedRegistry;
    changedRegistry.addDefinition(changedTool);
    const auto reconciled = ProjectStorage(changedRegistry).reconcile(loaded.project);
    const auto &after     = reconciled.project.flows[0].nodes[1];
    require(after.initParams.at(2).mode == BindingMode::Upstream && !after.initParams.at(2).source,
            "disconnected Image parameter must remain in Upstream mode");
}
} // namespace

int main()
{
    try
    {
        QTemporaryDir temporary;
        require(temporary.isValid(), "temporary directory unavailable");
        rejectsLegacyFields(temporary.path());
        candidateAndRawPreservation(temporary.path());
        reconciliationAndIdentities(temporary.path());
        typedArraysAndRanges(temporary.path());
        saveAsPaths(temporary.path());
        removedParametersDropped(temporary.path());
        toolUpdatesCutIncompatibleWires(temporary.path());
        wideIdsAndCycleMembership(temporary.path());
        canonicalSequences(temporary.path());
        dormantLiteralSources(temporary.path());
        modelReferences(temporary.path());
        stageBindingsPersistAndRoundTrip(temporary.path());
        std::cout << "project storage .vrtapp format 10 contract passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
