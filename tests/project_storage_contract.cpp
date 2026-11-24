#include "ToolResultAssertions.h"
#include "core/DefinitionJson.h"
#include "core/NodeContracts.h"
#include "core/ProjectEditor.h"
#include "core/ProjectStorage.h"
#include "core/ToolRegistry.h"
#include "core/ValueJson.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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

QJsonObject savedType(PortId port, QJsonObject type)
{
    return QJsonObject{
        {"port_id", static_cast<int>(port)},
        {   "type",                   type}
    };
}

// 端口编号就是身份：输入绑定写成条目数组，上游引用记的是源端口号。
QJsonObject savedInput(PortId port, const QString &source = "7", QJsonValue literal = qint64{9007199254740993LL})
{
    QJsonObject result{
        {"port_id",                                                            static_cast<int>(port)},
        {   "mode", static_cast<int>(source.isEmpty() ? BindingMode::Literal : BindingMode::Upstream)},
        {"literal",                                                                           literal}
    };
    if (!source.isEmpty())
        result.insert("source", QJsonObject{
                                    {"node_seq", source},
                                    { "port_id",      2}
        });
    return result;
}

QJsonObject savedNode(const QString &id = "7", const QString &source = "7")
{
    QJsonObject node;
    node.insert("node_seq", id);
    node.insert("tool_id", QStringLiteral("unavailable"));
    node.insert("tool_name_snapshot", QStringLiteral("原始名称"));
    node.insert("alias", QString());
    node.insert("pos", QJsonArray{0, 0});
    node.insert("vendor_extension",
                QJsonObject{
                    {"opaque", QJsonArray{QStringLiteral("one"), QJsonObject{{"two", QStringLiteral("three")}}}}
    });
    // 初始化端口 0 保存一个当前声明里没有的值：它必须原样保留。
    node.insert("init_params", QJsonArray{
                                   QJsonObject{{"port_id", 0}, {"value", QStringLiteral("保留中文")}}
    });
    node.insert("type_snapshot", QJsonObject{
                                     {"init_params", QJsonArray{savedType(0, ValueJson::encodeType({Type::String}))}},
                                     {     "inputs",    QJsonArray{savedType(1, ValueJson::encodeType({Type::Int}))}},
                                     {    "outputs",    QJsonArray{savedType(2, ValueJson::encodeType({Type::Int}))}}
    });
    node.insert("inputs", QJsonArray{savedInput(1, source)});
    return node;
}

QJsonObject savedProject(const QJsonArray &nodes, const QString &nextNode = "8", const QString &nextFlow = "2")
{
    return {
        {"format_version",                                                                     8                          },
        {       "project", QJsonObject{{"name", "缺失工具"}, {"next_node_seq", nextNode}, {"next_flow_seq", nextFlow}}},
        {         "flows",
         QJsonArray{QJsonObject{{"flow_id", "flow_1"}, {"name", "流程1"}, {"run_selected", true}, {"nodes", nodes}}}    }
    };
}

ToolDefinition scalarTool()
{
    ToolDefinition tool;
    tool.nameEn = "unavailable";
    tool.nameZh = "原始名称";
    tool.kind   = ToolKind::Cpp;

    // 输入 1、输出 2 是两个命名空间：同名不代表同号，也不冲突。
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
    const auto     source = directory + "/缺失.json";
    put(source, QJsonDocument(savedProject({savedNode()})).toJson());
    auto loaded = storage.load(path(source));
    require(!loaded.project.dirty && has(loaded.diagnostics, ProjectStorageCode::DefinitionMissing),
            "missing node was not retained cleanly");
    require(has(loaded.diagnostics, ProjectStorageCode::Cycle), "loaded cycle not diagnosed");
    loaded.project.flows[0].nodes[0].alias = "可编辑备注";
    const auto saved                       = directory + "/保留.json";
    storage.save(loaded.project, path(saved));
    const auto root = QJsonDocument::fromJson(get(saved)).object();
    const auto raw  = root.value("flows").toArray()[0].toObject().value("nodes").toArray()[0].toObject();
    require(root.value("format_version").toInteger() == 8 && raw.value("node_seq").isString(),
            "project version or identity encoding is wrong");
    require(raw.value("vendor_extension").toObject().value("opaque").toArray()[1].toObject().value("two") == "three",
            "extension lost");
    require(raw.value("inputs").toArray()[0].toObject().value("literal").toInteger() == 9007199254740993LL,
            "int64 precision lost");
    require(raw.value("alias") == "可编辑备注", "alias lost");
    const auto before    = get(saved);
    const auto priorPath = loaded.project.path;
    loaded.project.dirty = true;
    bool failed          = false;
    try
    {
        storage.save(loaded.project, path(directory + "/absent/failed.json"));
    }
    catch (const std::exception &)
    {
        failed = true;
    }
    require(failed && loaded.project.path == priorPath && loaded.project.dirty && get(saved) == before,
            "failed save installed candidate");
    auto copied                   = loaded.project;
    auto duplicate                = copied.flows[0].nodes[0];
    duplicate.id                  = 9;
    duplicate.position.x          = 1;
    duplicate.inputs.at(1).source = SourcePort{9, 2};
    copied.flows[0].nodes.push_back(std::move(duplicate));
    copied.nextNodeId = 10;
    storage.save(copied, path(directory + "/copied.json"));
    const auto reopened = storage.load(copied.path);
    require(reopened.project.flows[0].nodes[1].id == 9
                && reopened.project.flows[0].nodes[1].inputs.at(1).source->node == 9
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
    const auto     file     = directory + "/reconcile.json";
    const auto     document = savedProject({savedNode()}, "1", "1");
    put(file, QJsonDocument(document).toJson());
    auto        recovered = storage.load(path(file));
    const auto &node      = recovered.project.flows[0].nodes[0];
    require(node.unresolved.isEmpty() && std::get<std::string>(node.initParams.at(0)) == "保留中文",
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
    require(storage.load(path(file)).project.flows[0].nodes[0].inputs.at(1).source->node == 8,
            "definition treated as unavailable implementation");
    auto changedTool           = tool;
    changedTool.inputs[0].type = changedTool.outputs[0].type = {Type::Float};
    changedTool.inputs[0].defaultValue = changedTool.outputs[0].defaultValue = 2.5;
    ToolRegistry changedRegistry;
    changedRegistry.addDefinition(changedTool);
    const auto  changed = ProjectStorage(changedRegistry).load(path(file));
    const auto &binding = changed.project.flows[0].nodes[0].inputs.at(1);
    // A changed declaration is reported, not repaired: the saved integer literal and the upstream
    // reference both stay, and the node fails its input contract until the user corrects them.
    require(binding.source && binding.source->node == 8 && binding.mode == BindingMode::Upstream
                && std::get<std::int64_t>(binding.literal) == 9007199254740993LL,
            "schema change rewrote the saved value or dropped the upstream reference");
    require(has(changed.diagnostics, ProjectStorageCode::SchemaChanged),
            "a type-incompatible input was kept without a diagnostic");
    source.insert("tool_id", "still_missing");
    auto snapshot = source.value("type_snapshot").toObject();
    snapshot.insert("outputs", QJsonArray{});
    source.insert("type_snapshot", snapshot);
    put(file, QJsonDocument(savedProject({target, source}, "9")).toJson());
    const auto pending = storage.load(path(file));
    require(pending.project.flows[0].nodes[0].inputs.at(1).source.has_value()
                && has(pending.diagnostics, ProjectStorageCode::PendingReference),
            "unknown source cleared");
    put(file, QJsonDocument(savedProject({target})).toJson());
    const auto absentSource = storage.load(path(file));
    require(absentSource.project.flows[0].nodes[0].inputs.at(1).source.has_value()
                && has(absentSource.diagnostics, ProjectStorageCode::InvalidReference),
            "an absent source was silently unbound instead of reported");
}

void typedArraysAndRanges(const QString &directory)
{
    auto node = savedNode();
    node.insert("init_params", QJsonArray{});
    node.insert("type_snapshot",
                QJsonObject{
                    {"init_params",                                                                     QJsonArray{}},
                    {    "outputs",                                                                     QJsonArray{}},
                    {     "inputs", QJsonArray{savedType(1, QJsonObject{{"type", static_cast<int>(Type::Array)},
 {"dtype", static_cast<int>(DType::Float32)},
 {"rank", 2}})}                                                                  }
    });
    node.insert("inputs", QJsonArray{savedInput(1, "", QJsonArray{QJsonArray{}, QJsonArray{}})});
    auto tool = scalarTool();
    tool.initParams.clear();
    tool.outputs.clear();
    tool.inputs[0].type         = {Type::Array, DType::Int64, 2};
    tool.inputs[0].defaultValue = Missing{};
    ToolRegistry registry;
    registry.addDefinition(tool);
    const auto file = directory + "/array.json";
    put(file, QJsonDocument(savedProject({node})).toJson());
    const auto  changed = ProjectStorage(registry).load(path(file));
    const auto &retyped = changed.project.flows[0].nodes[0].inputs.at(1).literal;
    // A dtype change does not rewrite the saved array and never reinterprets float32 bytes as
    // int64: the original value stays with its own dtype and the node reports the mismatch when it
    // runs, so the user decides what the array should become.
    require(std::holds_alternative<Array>(retyped)
                && std::get<Array>(retyped).type() == TypeSpec{Type::Array, DType::Float32, 2}
                && has(changed.diagnostics, ProjectStorageCode::SchemaChanged),
            "a dtype change rewrote the saved array");
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
    const auto rangedNode = savedNode("7", "");
    put(file, QJsonDocument(savedProject({rangedNode})).toJson());
    const auto ranged = ProjectStorage(rangedRegistry).load(path(file));
    // Loading consumes the same normalisation as editing: an out-of-range stored value is written
    // back in its legal form, reported once, and the project becomes dirty because it changed.
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
    // 输入按声明顺序从 1 编号：path 1、text 2、empty 3、remote 4、absolute 5、dynamic 6。
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
    dynamic.id     = next++; // 6
    dynamic.nameZh = "动态路径";
    dynamic.type   = {Type::String};
    dynamic.editor = EditorKind::Path;
    tool.inputs.push_back(dynamic);
    // 目录参数是同一个编辑器、另一条初始化编号：初始化从 0 起。
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
    node.inputs[1]          = {BindingMode::Upstream, std::string("图像/不存在.png"), std::nullopt};
    node.inputs[2].literal  = std::string("图像/不存在.png");
    node.inputs[3].literal  = std::string("");
    node.inputs[4].literal  = std::string("Z:/异盘/资源.png");
    node.inputs[6].literal  = std::string("动态/图像.png");
    node.initParams[0]      = std::string("目录");
    const auto oldDirectory = directory + "/旧位置";
    const auto newDirectory = directory + "/新位置";
    const auto absolutePath = utf8Path(path(oldDirectory) / std::filesystem::path(u8"绝对.png"));
    node.inputs[5].literal  = std::string(absolutePath);
    for (const auto &port : tool.inputs) node.signatures.inputs.emplace(port.id, port.type);
    node.signatures.initParams.emplace(folder.id, folder.type);
    project.flows[0].nodes.push_back(node);
    project.nextNodeId = 1;
    require(QDir().mkpath(oldDirectory) && QDir().mkpath(newDirectory), "cannot create save-as directories");
    storage.save(project, path(oldDirectory + "/首次"));
    require(project.path.filename() == std::filesystem::path(u8"首次.json") && !project.dirty,
            "first save did not append extension or clear dirty");
    require(std::get<std::string>(project.flows[0].nodes[0].inputs.at(1).literal) == "图像/不存在.png",
            "unnamed relative path was resolved using a fabricated old directory");
    require(std::get<std::string>(project.flows[0].nodes[0].inputs.at(5).literal) == absolutePath,
            "unnamed project guessed a relative base for an absolute path");
    project.dirty = true;
    storage.save(project, path(newDirectory + "/另存.json"));
    const auto &saved = project.flows[0].nodes[0];
    // Save As keeps every literal exactly as the user entered it: a path is interpreted by the tool
    // against the product directory, so moving the project never rewrites one.
    require(std::get<std::string>(saved.inputs.at(1).literal) == "图像/不存在.png",
            "save-as rewrote a hidden upstream literal");
    require(std::get<std::string>(saved.initParams.at(0)) == "目录", "save-as rewrote a directory parameter");
    require(std::get<std::string>(saved.inputs.at(6).literal) == "动态/图像.png", "save-as rewrote a path input");
    {
        const auto &text  = std::get<std::string>(saved.inputs.at(2).literal);
        const auto &empty = std::get<std::string>(saved.inputs.at(3).literal);
        const auto  emsg  = "save-as rewrote non-path String or empty path: text=" + text + " empty=" + empty;
        require(text == "图像/不存在.png" && empty.empty(), emsg.c_str());
    }
    require(std::get<std::string>(saved.inputs.at(4).literal) == "Z:/异盘/资源.png",
            "cross-drive resource lost its absolute path");
    auto reopened = storage.load(project.path);
    require(std::get<std::string>(reopened.project.flows[0].nodes[0].inputs.at(1).literal) == "图像/不存在.png",
            "reopened literal differs from the saved value");
    const auto before  = std::get<std::string>(saved.inputs.at(1).literal);
    const auto oldPath = project.path;
    project.dirty      = true;
    bool failed        = false;
    try
    {
        storage.save(project, path(directory + "/not-created/failed.json"));
    }
    catch (const std::exception &)
    {
        failed = true;
    }
    require(failed && project.dirty && project.path == oldPath
                && std::get<std::string>(project.flows[0].nodes[0].inputs.at(1).literal) == before,
            "failed save-as installed rebased candidate");
}

void retiredInitialization(const QString &directory)
{
    // 端口 9 当前声明里没有：它的值必须原样保留，但不能进入可执行配置。
    const auto tool = parseToolDefinitionJson(
        R"({
  "name_en": "unavailable",
  "name_zh": "接收器",
  "version": "1.0",
  "implementation": {
    "kind": "host",
    "binding": "visionrt.receiver"
  },
  "init_params": [
    {
      "id": 0,
      "name_zh": "流程",
      "type": "string"
    },
    {
      "id": 1,
      "name_zh": "节点",
      "type": "string"
    },
    {
      "id": 2,
      "name_zh": "端口",
      "type": "int",
      "default": 2
    }
  ],
  "outputs": [
    {
      "id": 2,
      "name_zh": "值",
      "type_from": {
        "flow": {
          "stage": "init",
          "id": 0
        },
        "node": {
          "stage": "init",
          "id": 1
        },
        "port": {
          "stage": "init",
          "id": 2
        }
      }
    }
  ],
  "schema_version": 2
})");
    auto raw = savedNode("9007199254740993", "");
    raw.insert("init_params",
               QJsonArray{
                   QJsonObject{{"port_id", 0},    {"value", "flow_1"}                                              },
                   QJsonObject{{"port_id", 1},                                                       {"value", "7"}},
                   QJsonObject{{"port_id", 2},                                                         {"value", 2}},
                   QJsonObject{{"port_id", 9},
                               {"value", QJsonArray{QJsonArray{9007199254740993LL}, QJsonArray{}, QJsonArray{-17}}}}
    });
    raw.insert("inputs", QJsonArray{});
    raw.insert("type_snapshot",
               QJsonObject{
                   {"init_params", QJsonArray{savedType(0, QJsonObject{{"type", static_cast<int>(Type::String)}}),
 savedType(1, QJsonObject{{"type", static_cast<int>(Type::String)}}),
 savedType(2, QJsonObject{{"type", static_cast<int>(Type::Int)}}),
 savedType(9, QJsonObject{{"type", static_cast<int>(Type::Array)},
 {"dtype", static_cast<int>(DType::Int64)},
 {"rank", 2}})}                                                                      },
                   {     "inputs",                                                                         QJsonArray{}},
                   {    "outputs",         QJsonArray{savedType(2, QJsonObject{{"type", static_cast<int>(Type::Int)}})}}
    });
    const auto input  = directory + "/retired.json";
    const auto output = directory + "/retained.json";
    put(input, QJsonDocument(savedProject({raw}, "9007199254740994")).toJson());
    ToolRegistry registry;
    registry.addDefinition(tool);
    ProjectStorage storage(registry);
    auto           loaded     = storage.load(path(input));
    const auto    &loadedNode = loaded.project.flows[0].nodes[0];
    require(has(loaded.diagnostics, ProjectStorageCode::SchemaChanged) && loaded.project.dirty,
            "an undeclared initialization port was accepted silently");
    require(!loadedNode.initParams.contains(9) && loadedNode.retiredInitParams.contains("9"),
            "retired parameter leaked into executable configuration");
    require(loadedNode.signatures.initParams.size() == 3, "retired parameter stayed in the signature");

    storage.save(loaded.project, path(output));
    const auto saved = QJsonDocument::fromJson(get(output))
                           .object()["flows"]
                           .toArray()[0]
                           .toObject()["nodes"]
                           .toArray()[0]
                           .toObject();
    // 保存后，退役值以条目形式留在 retired_init_params 里，编号与原文都不变。
    require(saved["init_params"].toArray().size() == 3, "retired parameter was written as executable");
    const auto retired = saved["retired_init_params"].toArray();
    require(retired.size() == 1 && retired[0].toObject().value("port_id").toInt() == 9,
            "retired parameter lost its port number");
    // 二维数组按偏移表存：三行，第一行一个整数，中间一行空，最后一行一个负数。
    const auto retainedValue = retired[0].toObject().value("value").toObject();
    const auto rows          = retainedValue.value("offsets").toArray();
    const auto cells         = retainedValue.value("values").toArray();
    require(rows.size() == 1 && rows[0].toArray().size() == 4 && rows[0].toArray()[1].toInt() == 1
                && rows[0].toArray()[2].toInt() == 1 && rows[0].toArray()[3].toInt() == 2,
            "retired nested shape lost its empty branch");
    require(cells.size() == 2 && cells[0].toInteger() == 9007199254740993LL && cells[1].toInteger() == -17,
            "retired nested values lost integer precision");
    require(retired[0].toObject().value("type").toObject().value("type").toInt() == static_cast<int>(Type::Array),
            "retired parameter type was not preserved");

    const auto reloaded = storage.load(path(output));
    require(has(reloaded.diagnostics, ProjectStorageCode::SchemaChanged)
                && reloaded.project.flows[0].nodes[0].retiredInitParams.contains("9"),
            "retired parameter disappeared after save/reopen");
    require(!reloaded.project.dirty, "an unchanged project with retired values stays dirty");
    auto unchanged                                = reloaded.project;
    unchanged.flows[0].nodes[0].executionRevision = 19;
    const auto reconciled                         = storage.reconcile(unchanged);
    require(reconciled.project.flows[0].nodes[0].executionRevision == 19,
            "retained non-executable parameters invalidated an unchanged tool instance");
    ProjectEditor editor(reconciled.project);
    editor.copyFlow("flow_1");
    auto       copied     = editor.project();
    const auto copiedPath = directory + "/copied-retained.json";
    storage.save(copied, path(copiedPath));
    const auto copiedProject = storage.load(path(copiedPath)).project;
    require(
        copiedProject.flows.size() == 2 && copiedProject.flows[0].nodes[0].id != copiedProject.flows[1].nodes[0].id
            && copiedProject.flows[0].nodes[0].retiredInitParams == copiedProject.flows[1].nodes[0].retiredInitParams,
        "copying a flow lost opaque initialization metadata");
    auto relocated = reloaded.project;
    require(QDir().mkpath(directory + "/迁移"), "cannot create retired-parameter relocation directory");
    storage.save(relocated, path(directory + "/迁移/retained.json"));
    const auto movedNode = QJsonDocument::fromJson(get(directory + "/迁移/retained.json"))
                               .object()["flows"]
                               .toArray()[0]
                               .toObject()["nodes"]
                               .toArray()[0]
                               .toObject();
    require(movedNode["init_params"] == saved["init_params"] && movedNode["type_snapshot"] == saved["type_snapshot"],
            "Save As rewrote opaque initialization values or types");

    // 声明承认端口 9 之后，同一个文件里的值直接成为配置，不再是退役项。
    auto extended = tool;
    extended.initParams.push_back({
        9,
        "补充参数",
        {Type::Array, DType::Int64, 2},
        Missing{}
    });
    ToolRegistry extendedRegistry;
    extendedRegistry.addDefinition(extended);
    const auto  adopted     = ProjectStorage(extendedRegistry).load(path(input));
    const auto &adoptedNode = adopted.project.flows[0].nodes[0];
    require(adoptedNode.retiredInitParams.isEmpty() && adoptedNode.initParams.contains(9)
                && std::get<Array>(adoptedNode.initParams.at(9)).elementCount() == 2,
            "a declared port did not adopt the value saved under its number");
    const auto revived = ProjectStorage(extendedRegistry).reconcile(unchanged);
    require(revived.project.flows[0].nodes[0].initParams.contains(9)
                && revived.project.flows[0].nodes[0].executionRevision == 20,
            "catalog reconciliation did not adopt a now-declared parameter and invalidate the instance once");
}

void wideIdsAndCycleMembership(const QString &directory)
{
    ToolRegistry   registry;
    ProjectStorage storage(registry);
    const auto     file = directory + "/wide.json";
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
    ToolRegistry   registry;
    ToolDefinition defA;
    defA.nameEn  = "test.producer";
    defA.nameZh  = "生产者";
    defA.outputs = {
        {2, "输出", {Type::Int}}
    };
    registry.addDefinition(defA);

    ToolDefinition defB;
    defB.nameEn = "test.consumer";
    defB.nameZh = "消费者";
    defB.inputs = {
        {1, "输入", {Type::Int}}
    };
    registry.addDefinition(defB);

    ProjectEditor editor;
    const auto    pA = editor.addNode("flow_1", defA, {0, 100}); // Y = 100
    const auto    pB = editor.addNode("flow_1", defB, {0, 50});  // Y = 50 (above pA)

    // Consumer (pB) has an input with Literal mode, but has a stale/dormant source pointing to pA (below pB)
    auto  project   = editor.project();
    auto &nodeB     = project.flows[0].nodes[1];
    auto &binding   = nodeB.inputs[1];
    binding.mode    = BindingMode::Literal;
    binding.literal = std::int64_t{42};
    binding.source  = SourcePort{pA, 2}; // If upstream, this would violate Y < Y

    const auto filePath = std::filesystem::path((directory + "/dormant_literal.json").toStdString());
    ProjectStorage(registry).save(project, filePath);

    const auto loaded                 = ProjectStorage(registry).load(filePath);
    bool       hasHierarchyDiagnostic = false;
    for (const auto &diag : loaded.diagnostics)
    {
        if (diag.field.rfind("inputs.", 0) == 0)
            hasHierarchyDiagnostic = true;
    }
    require(!hasHierarchyDiagnostic, "T-B04: Literal mode with dormant source produced false diagnostic");
    const auto &loadedBinding = loaded.project.flows[0].nodes[1].inputs.at(1);
    require(loadedBinding.mode == BindingMode::Literal, "T-B04: Binding mode changed from Literal");
    require(std::get<std::int64_t>(loadedBinding.literal) == 42, "T-B04: Literal value corrupted");

    // Switching back to Upstream mode must be rejected
    const auto check = validateBinding(loaded.project.flows[0], pA, 2, pB, 1);
    require(!check && check.reason == BindingError::SameOrUpperRow, "T-B04: Upstream mode did not reject illegal edge");
}

void canonicalSequences(const QString &directory)
{
    ToolRegistry   registry;
    ProjectStorage storage(registry);
    const auto     file = directory + "/非规范序号.json";
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
                require(message.contains(field) && message.contains("非规范序号.json"),
                        "sequence rejection lost the project file or field context");
                rejected = true;
            }
            require(rejected, "project accepted a noncanonical unsigned sequence");
        }
}
} // namespace

// 工具更新：声明改了端口类型或删了端口，连线按新声明断开，并在一次通知里说明两端和原因；同一
// 版本同一声明重开后不再提示。只改版本、端口形状不变的更新仍然要提示，且连线全部保留。
void toolUpdatesCutIncompatibleWires(const QString &directory)
{
    ToolDefinition upstream = scalarTool();
    upstream.nameZh         = "上游";
    upstream.version        = "1.0.0";
    // 这条用例只关心输出与连线：上游只产出，初始化参数在别处覆盖。
    upstream.initParams.clear();
    upstream.inputs.clear();
    ToolDefinition sink;
    sink.nameEn = "sink";
    sink.nameZh = "下游";
    sink.kind   = ToolKind::Cpp;
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

    const auto node = [](const QString &id, const QString &tool, const QString &name, const QString &version, int row,
                         const QJsonObject &snapshot, const QJsonArray &inputs)
    {
        return QJsonObject{
            {             "node_seq",                 id},
            {              "tool_id",               tool},
            {   "tool_name_snapshot",               name},
            {"tool_version_snapshot",            version},
            {                "alias",                 ""},
            {                  "pos", QJsonArray{0, row}},
            {        "type_snapshot",           snapshot},
            {               "inputs",             inputs}
        };
    };
    const QJsonObject sourceSnapshot{
        {"init_params",                                                 QJsonArray{}},
        {     "inputs",                                                 QJsonArray{}},
        {    "outputs", QJsonArray{savedType(2, ValueJson::encodeType({Type::Int}))}}
    };
    const QJsonObject sinkSnapshot{
        {"init_params",                                                 QJsonArray{}},
        {     "inputs", QJsonArray{savedType(1, ValueJson::encodeType({Type::Int}))}},
        {    "outputs", QJsonArray{savedType(2, ValueJson::encodeType({Type::Int}))}}
    };
    const auto file = directory + "/tool-update.json";
    const auto project
        = savedProject({node("7", "unavailable", "上游", "1.0.0", 0, sourceSnapshot, QJsonArray{}),
                        node("8", "sink", "下游", "1.0.0", 1, sinkSnapshot, QJsonArray{savedInput(1, "7")})},
                       "9");
    put(file, QJsonDocument(project).toJson());
    require(ProjectStorage(original).load(path(file)).toolUpdates.empty(),
            "an unchanged declaration was reported as a tool update");

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
    require(std::any_of(updated.diagnostics.begin(), updated.diagnostics.end(),
                        [](const auto &diagnostic)
                        {
                            return diagnostic.code == ProjectStorageCode::InvalidReference
                                && diagnostic.message.find("连线已断开") != std::string::npos;
                        }),
            "cutting a wire was not reported");

    auto kept = updated.project;
    ProjectStorage(replaced).save(kept, path(file));
    require(ProjectStorage(replaced).load(path(file)).toolUpdates.empty(), "the same declaration was reported again");

    put(file, QJsonDocument(project).toJson());
    ToolDefinition patched = upstream;
    patched.version        = "1.0.1";
    ToolRegistry compatible;
    compatible.addDefinition(patched);
    compatible.addDefinition(sink);
    const auto bumped = ProjectStorage(compatible).load(path(file));
    require(bumped.toolUpdates.size() == 1 && bumped.toolUpdates.front().kept == 1
                && bumped.toolUpdates.front().removed.empty() && !bumped.project.dirty,
            "a version bump with an intact wiring was not announced as kept only");

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
    const auto refreshed = ProjectStorage(bothChanged).reconcile(ProjectStorage(original).load(path(file)).project);
    require(refreshed.project.flows.front().nodes.back().inputs.at(1).source == SourcePort{7, 2},
            "catalog refresh removed a connection accepted by the current declarations");
    auto persisted = sameType.project;
    ProjectStorage(bothChanged).save(persisted, path(file));
    const auto reopened = ProjectStorage(bothChanged).load(path(file));
    require(reopened.toolUpdates.empty()
                && reopened.project.flows.front().nodes.back().inputs.at(1).source == SourcePort{7, 2},
            "saving a compatible update changed its connection or repeated the notice");

    put(file, QJsonDocument(project).toJson());
    ToolDefinition removedInput = sink;
    removedInput.version        = "2.0.0";
    removedInput.inputs.clear();
    ToolRegistry removed;
    removed.addDefinition(upstream);
    removed.addDefinition(removedInput);
    const auto deleted = ProjectStorage(removed).load(path(file));
    require(deleted.toolUpdates.size() == 1 && deleted.toolUpdates.front().removed.size() == 1,
            "deleting the target input omitted the removed connection from the update notice");
    const auto &removedWire = deleted.toolUpdates.front().removed.front();
    require(removedWire.source.find("上游-7") != std::string::npos
                && removedWire.target.find("下游-8") != std::string::npos
                && removedWire.reason.find("删除") != std::string::npos,
            "a deleted target input lost its connection endpoints or removal reason");
    require(!deleted.project.flows.front().nodes.back().inputs.contains(1)
                && dependencyEdges(deleted.project.flows.front()).empty(),
            "a removed input kept an execution dependency");
}

void initializationTypeChanges(const QString &directory)
{
    auto          tool = scalarTool();
    ProjectEditor editor;
    editor.addNode(editor.project().flows.front().id, tool, {0, 0});
    auto project                                      = editor.project();
    project.flows.front().nodes.front().initParams[0] = std::string("保留值");
    tool.initParams.front().type                      = {Type::Int};
    tool.initParams.front().defaultValue              = std::int64_t{0};
    ToolRegistry retyped;
    retyped.addDefinition(tool);
    auto       changed = ProjectStorage(retyped).reconcile(project).project;
    const auto file    = path(directory + "/initialization-types.json");
    ProjectStorage(retyped).save(changed, file);
    const auto restored = ProjectStorage(retyped).load(file);
    tool.initParams.clear();
    ToolRegistry removed;
    removed.addDefinition(tool);
    auto retired = ProjectStorage(removed).reconcile(restored.project).project;
    ProjectStorage(removed).save(retired, file);
    const auto reopened = ProjectStorage(removed).load(file);
    const auto record   = reopened.project.flows.front().nodes.front().retiredInitParams.value("0").toObject();
    require(record.value("value").toString() == "保留值"
                && record.value("type").toObject().value("type").toInt() == static_cast<int>(Type::String),
            "retiring a retyped initialization parameter lost its actual value type");
}

void rejectsHostExecutionGuard(const QString &directory)
{
    auto         tool = scalarTool();
    ToolRegistry registry;
    registry.addDefinition(tool);
    ProjectEditor editor;
    editor.addNode(editor.project().flows.front().id, tool, {0, 0});
    const auto     filename = directory + "/removed-guard.json";
    ProjectStorage storage(registry);
    auto           project = editor.project();
    storage.save(project, path(filename));
    auto document           = QJsonDocument::fromJson(get(filename)).object();
    auto flows              = document["flows"].toArray();
    auto flow               = flows[0].toObject();
    auto nodes              = flow["nodes"].toArray();
    auto node               = nodes[0].toObject();
    node["execution_guard"] = QJsonObject{
        {"mode", static_cast<int>(BindingMode::Upstream)}
    };
    nodes[0]          = node;
    flow["nodes"]     = nodes;
    flows[0]          = flow;
    document["flows"] = flows;
    put(filename, QJsonDocument(document).toJson());
    bool rejected = false;
    try
    {
        storage.load(path(filename));
    }
    catch (const ContractError &error)
    {
        rejected = std::string(error.what()).find("execution_guard") != std::string::npos;
    }
    require(rejected, "removed host execution guards must not be silently ignored");
}

void trainingAssets(const QString &directory)
{
    const auto declaration = R"json({
      "schema_version":2,"name_en":"ProbeAssets","name_zh":"资产探针","version":"1.0.0",
      "implementation":{"kind":"cpp"},
      "train_params":[
        {"id":7,"name_zh":"模型路径","type":"string","scope":"tool"},
        {"id":1,"name_zh":"采样区","type":"roi","scope":"record","canvas":{"min":1,"max":2}}
      ],
      "inputs":[{"id":1,"name_zh":"图像","type":"image"}],
      "outputs":[{"id":2,"name_zh":"模型路径","type":"string"}],
      "training":{"image":{"stage":"inputs","id":1},"max_records":1,
        "artifacts":[{"id":"model","source_port":2,"format":"probe.model","version":1,"target":{"stage":"train_params","id":7}}]}
    })json";
    ToolRegistry registry;
    auto         tool   = parseToolDefinitionJson(declaration);
    tool.definitionPath = path(directory + "/ProbeAssets.json");
    registry.addDefinition(tool);

    ProjectEditor editor;
    const auto    nodeId   = editor.addNode("flow_1", tool, {0, 0});
    const auto    revision = path(directory)
                        / std::filesystem::path((QStringLiteral("资产探针") + QStringLiteral(".assets")).toStdWString())
                        / L"training" / std::to_wstring(nodeId) / L"1";
    require(std::filesystem::create_directories(revision / L"models"),
            "training asset fixture directory was not created");
    put(QString::fromStdWString((revision / L"models" / L"model.bin").wstring()), "MODEL");
    put(QString::fromStdWString((revision / L"manifest.json").wstring()),
        R"({"schema_version":4,"artifacts":[{"id":"model","file":"models/model.bin"}]})");
    const std::vector<LiteralWrite> writes{{PortStage::Train, 7, utf8Path((revision / L"models" / L"model.bin").lexically_relative(path(directory)))}};
    require(editor.setLiteralWrites("flow_1", nodeId, writes, [&] { return revision; }),
            "the revision reference was not stored");

    const auto file    = directory + "/assets.json";
    auto       project = editor.project();
    ProjectStorage(registry).save(project, path(file));
    const auto saved  = QJsonDocument::fromJson(get(file)).object();
    const auto raw    = saved.value("flows").toArray().at(0).toObject().value("nodes").toArray().at(0).toObject();
    const auto stored = raw.value("training_asset").toString();
    require(QFileInfo(stored).isRelative() && stored.contains(QStringLiteral("/training/")),
            "a published reference must be stored relative to the project");

    const auto  loaded = ProjectStorage(registry).load(path(file));
    const auto &node   = loaded.project.flows[0].nodes[0];
    require(node.trainingAsset == revision.lexically_normal(), "loading did not resolve the stored reference");
    require(nodeParameters(node, tool).empty(), "models must not be copied into hidden initialization parameters");
    const auto models = nodeModelReferences(node, tool, path(directory));
    require(models.at("model") == revision / L"models" / L"model.bin",
            "the editable model path did not resolve against the project directory");
    std::filesystem::remove(revision / L"manifest.json");
    require(nodeModelReferences(node, tool, path(directory)) == models,
            "loading an existing model must not depend on training records");
}

int main()
{
    try
    {
        QTemporaryDir temporary;
        require(temporary.isValid(), "temporary directory unavailable");
        rejectsHostExecutionGuard(temporary.path());
        candidateAndRawPreservation(temporary.path());
        reconciliationAndIdentities(temporary.path());
        typedArraysAndRanges(temporary.path());
        saveAsPaths(temporary.path());
        retiredInitialization(temporary.path());
        toolUpdatesCutIncompatibleWires(temporary.path());
        initializationTypeChanges(temporary.path());
        wideIdsAndCycleMembership(temporary.path());
        canonicalSequences(temporary.path());
        dormantLiteralSources(temporary.path());
        trainingAssets(temporary.path());
        std::cout << "project storage JSON contract passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
