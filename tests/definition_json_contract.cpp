#include "core/DefinitionJson.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cassert>
#include <iostream>

using namespace visionrt;

int main()
{
    QTemporaryDir directory;
    assert(directory.isValid());
    const auto name = directory.path() + "/声明.json";
    QFile      file(name);
    assert(file.open(QIODevice::WriteOnly));
    file.write(R"json({
  "schema_version": 1,
  "tool_id": "test.json",
  "display_name": "JSON 声明",
  "version": "1.0.0",
  "category": "测试",
  "subcategory": "数值",
  "kind": "cpp",
  "init_params": [
    {"id": "seed", "name": "种子", "type": "Int", "default": 9007199254740993}
  ],
  "inputs": [
    {"id": "optional", "name": "可选输入", "type": "String", "required": false}
  ],
  "outputs": [
    {"id": "result", "name": "结果", "type": "Int"}
  ]
})json");
    file.close();
    const auto definition = parseToolDefinitionFile(std::filesystem::path(name.toStdWString()));
    assert(definition.id == "test.json");
    assert(std::get<std::int64_t>(definition.initParams[0].defaultValue) == 9007199254740993LL);
    assert(!definition.inputs[0].required);
    assert(definition.outputs.size() == 3);
    assert(definition.outputs[0].id == "__status_code");
    const auto normalized = emitNormalizedToolJson(definition);
    const auto emitted    = QJsonDocument::fromJson(QByteArray::fromStdString(normalized)).object();
    assert(emitted["outputs"].toArray().size() == 1);
    assert(!emitted.contains("entry"));
    const auto roundTrip = parseToolDefinitionJson(normalized);
    assert(equalValues(roundTrip.initParams[0].defaultValue, definition.initParams[0].defaultValue));
    assert(!roundTrip.inputs[0].required);

    const auto rejects = [](const std::string &text)
    {
        try
        {
            (void)parseToolDefinitionJson(text);
        }
        catch (const ContractError &)
        {
            return;
        }
        assert(false);
    };
    const auto rejectObject = [&](const QJsonObject &object)
    {
        rejects(QJsonDocument(object).toJson().toStdString());
    };
    auto invalid     = emitted;
    invalid["extra"] = 1;
    rejectObject(invalid);
    invalid                   = emitted;
    invalid["schema_version"] = "1";
    rejectObject(invalid);
    invalid          = emitted;
    invalid["entry"] = QJsonObject{};
    rejectObject(invalid);
    invalid         = emitted;
    invalid["kind"] = "cpp_class";
    rejectObject(invalid);
    invalid         = emitted;
    invalid["kind"] = "rust";
    rejectObject(invalid);
    for (const auto field : {"init_params", "inputs", "outputs", "tool_id", "display_name", "version"})
    {
        invalid = emitted;
        invalid.remove(field);
        rejectObject(invalid);
    }
    for (const auto field : {"required", "decimals", "default", "unknown"})
    {
        invalid           = emitted;
        auto input        = invalid["inputs"].toArray().first().toObject();
        input[field]      = QJsonArray{};
        invalid["inputs"] = QJsonArray{input};
        rejectObject(invalid);
    }
    invalid            = emitted;
    auto output        = invalid["outputs"].toArray().first().toObject();
    output["id"]       = "__custom";
    invalid["outputs"] = QJsonArray{output};
    rejectObject(invalid);
    invalid     = emitted;
    auto inputs = invalid["inputs"].toArray();
    inputs.append(inputs.first());
    invalid["inputs"] = inputs;
    rejectObject(invalid);
    auto       overflow = normalized;
    const auto number   = overflow.find("9007199254740993");
    assert(number != std::string::npos);
    overflow.replace(number, 16, "-9223372036854775809");
    rejects(overflow);
    overflow = normalized;
    overflow.replace(number, 16, "9223372036854775808");
    rejects(overflow);
    try
    {
        (void)parseToolDefinitionJson("{", "broken.json");
        assert(false);
    }
    catch (const ContractError &error)
    {
        assert(std::string(error.what()).find("offset=") != std::string::npos);
        assert(std::string(error.what()).find("broken.json") != std::string::npos);
    }
    auto python     = emitted;
    python["kind"]  = "python_class";
    python["entry"] = QJsonObject{
        {  "file", "../shared/script.py"},
        {"symbol",                "Tool"}
    };
    const auto pythonText       = QJsonDocument(python).toJson().toStdString();
    const auto pythonDefinition = parseToolDefinitionJson(pythonText, "/package/tools/tool.json");
    assert(pythonDefinition.script == pythonDefinition.definitionPath.parent_path().parent_path() / "shared/script.py");
    python["kind"] = "python_function";
    rejectObject(python);
    python["init_params"] = QJsonArray{};
    assert(parseToolDefinitionJson(QJsonDocument(python).toJson().toStdString()).kind == ToolKind::PythonFunction);
    auto host     = emitted;
    host["kind"]  = "host";
    host["entry"] = QJsonObject{
        {"binding", "visionrt.receiver"}
    };
    host["init_params"] = QJsonArray{
        QJsonObject{{"id", "flow"}, {"name", "流程"}, {"type", "String"}},
        QJsonObject{{"id", "node"}, {"name", "节点"}, {"type", "String"}},
        QJsonObject{{"id", "port"}, {"name", "端口"}, {"type", "String"}}
    };
    output = QJsonObject{
        {       "id",                                                           "value"},
        {     "name",                                                             "值"},
        {"type_from", QJsonObject{{"flow", "flow"}, {"node", "node"}, {"port", "port"}}}
    };
    host["outputs"]     = QJsonArray{output};
    const auto receiver = parseToolDefinitionJson(QJsonDocument(host).toJson().toStdString());
    assert(receiver.hostBinding == "visionrt.receiver" && receiver.outputs.back().typeFrom);
    assert(parseToolDefinitionJson(emitNormalizedToolJson(receiver)).outputs.back().typeFrom);
    output["type"]  = "Int";
    host["outputs"] = QJsonArray{output};
    rejectObject(host);
    std::cout << "Qt JSON tool declarations preserve values and derive system outputs\n";
}
