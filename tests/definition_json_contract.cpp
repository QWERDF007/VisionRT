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
  "schema_version": 6,
  "name_en": "test.json",
  "name_zh": "JSON 声明",
  "version": "1.0.0",
  "implementation": {
    "kind": "cpp"
  },
  "next_port_id": {"init": 1, "inputs": 2, "outputs": 3},
  "ui": {
    "category": [
      "测试",
      "数值"
    ]
  },
  "ports": {
    "init": [
      {
        "id": 0,
        "name_en": "seed",
        "name_zh": "种子",
        "type": "int",
        "default": 9007199254740993
      }
    ],
    "inputs": [
      {
        "id": 1,
        "name_en": "optional",
        "name_zh": "可选输入",
        "type": "string"
      }
    ],
    "outputs": [
      {
        "id": 2,
        "name_en": "result",
        "name_zh": "结果",
        "type": "int"
      }
    ]
  }
})json");
    file.close();
    const auto definition = parseToolDefinitionFile(std::filesystem::path(name.toStdWString()));
    assert(definition.nameEn == "test.json");
    assert(std::get<std::int64_t>(definition.initParams[0].defaultValue) == 9007199254740993LL);
    assert(definition.inputs.size() == 2 && definition.inputs[0].id == kUpstreamStatusPort);
    assert(equalValues(definition.inputs[1].defaultValue, defaultValue(definition.inputs[1].type)));
    assert(definition.outputs.size() == 3);
    assert(definition.outputs[0].id == kStatusCodePort);
    const auto normalized = emitNormalizedToolJson(definition);
    const auto emitted    = QJsonDocument::fromJson(QByteArray::fromStdString(normalized)).object();
    assert(emitted["ports"].toObject()["outputs"].toArray().size() == 1);
    // Host-owned common ports are materialised in the effective contract but never written back
    // into the author declaration.
    assert(emitted["ports"].toObject()["inputs"].toArray().size() == 1
           && emitted["ports"].toObject()["inputs"].toArray()[0].toObject()["id"].toInt() == 1);
    assert(!emitted["implementation"].toObject().contains("entry"));
    const auto roundTrip = parseToolDefinitionJson(normalized);
    assert(equalValues(roundTrip.initParams[0].defaultValue, definition.initParams[0].defaultValue));
    assert(equalValues(roundTrip.inputs[1].defaultValue, definition.inputs[1].defaultValue));

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
        std::cerr << "accepted a declaration it should reject: " << text << std::endl;
        std::abort();
    };
    const auto rejectObject = [&](const QJsonObject &object)
    {
        rejects(QJsonDocument(object).toJson().toStdString());
    };

    // A port list is authored under ports.inputs and referenced by stage-qualified names.
    auto crop          = emitted;
    auto cropPorts     = crop["ports"].toObject();
    // 端口编号不重复即可，名称只是说明。
    cropPorts["inputs"] = QJsonArray{
        QJsonObject{{"id", 2}, {"name_en", "images"}, {"name_zh", "图像"}, {"type", "image"}},
        QJsonObject{{"id", 3},
                    {"name_en", "rect"},
                    {"name_zh", "矩形"},
                    {"type", "roi"},
                    {"ui", QJsonObject{{"editor", "rect"}}},
                    {"relations", QJsonObject{{"image", QJsonObject{{"stage", "inputs"}, {"id", 2}}}}}}
    };
    crop["ports"] = cropPorts;
    const auto cropDefinition = parseToolDefinitionJson(QJsonDocument(crop).toJson().toStdString());
    assert(cropDefinition.inputs[2].type.type == Type::ROI && cropDefinition.inputs[2].editor == "rect");
    assert((cropDefinition.inputs[2].image == PortRef{PortStage::Inputs, 2}));
    assert(parseToolDefinitionJson(emitNormalizedToolJson(cropDefinition)).inputs[2].editor == "rect");

    auto badEditor     = crop;
    auto badPorts      = badEditor["ports"].toObject();
    badPorts["inputs"] = QJsonArray{
        QJsonObject{{"id", 2}, {"name_en", "text"}, {"name_zh", "文本"}, {"type", "string"}, {"ui", QJsonObject{{"editor", "rect"}}}}
    };
    badEditor["ports"] = badPorts;
    rejectObject(badEditor);

    for (const auto field : {"extra", "tool_id", "display_name", "kind", "entry", "init_params", "inputs", "outputs"})
    {
        auto invalid = emitted;
        invalid[field] = 1;
        rejectObject(invalid);
    }
    auto invalid = emitted;
    invalid["schema_version"] = 4;
    rejectObject(invalid);
    invalid["schema_version"] = "5";
    rejectObject(invalid);

    // One token per type: the old three-field spelling and capitalised names are gone.
    for (const auto token : {"Int", "Array", "Float64", "vec_float64", "vec_vec_vec_vec_vec_int"})
    {
        auto declaration = emitted;
        auto ports       = declaration["ports"].toObject();
        auto inputs      = ports["inputs"].toArray();
        auto first       = inputs.first().toObject();
        first["type"]    = token;
        inputs.replace(0, first);
        ports["inputs"]  = inputs;
        declaration["ports"] = ports;
        rejectObject(declaration);
    }

    // A port keeps identity, type and value; the removed required flag is an unknown field.
    for (const auto field : {"required", "allow_missing"})
    {
        auto declaration = emitted;
        auto ports       = declaration["ports"].toObject();
        auto inputs      = ports["inputs"].toArray();
        auto first       = inputs.first().toObject();
        first[field]     = true;
        inputs.replace(0, first);
        ports["inputs"]  = inputs;
        declaration["ports"] = ports;
        rejectObject(declaration);
    }

    // Duplicate and reserved names stay author errors.
    {
        auto declaration = emitted;
        auto ports       = declaration["ports"].toObject();
        auto inputs      = ports["inputs"].toArray();
        inputs.append(inputs.first());
        ports["inputs"]  = inputs;
        declaration["ports"] = ports;
        rejectObject(declaration);
    }
    {
        // 编号是身份：声明不得占用宿主保留的状态输出编号 0/1。
        auto declaration = emitted;
        auto ports       = declaration["ports"].toObject();
        auto outputs     = ports["outputs"].toArray();
        auto first       = outputs.first().toObject();
        first["id"]      = 1;
        outputs.replace(0, first);
        ports["outputs"] = outputs;
        declaration["ports"] = ports;
        rejectObject(declaration);
    }

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

    auto python            = emitted;
    python["implementation"] = QJsonObject{
        {    "kind", "python_class"},
        {"package",  "示例 包/shared"},
        {   "file",          "script.py"},
        { "symbol",               "Tool"}
    };
    const auto pythonDefinition = parseToolDefinitionJson(QJsonDocument(python).toJson().toStdString());
    assert(pythonDefinition.entry.package == std::filesystem::u8path("示例 包/shared"));
    assert(pythonDefinition.entry.file == "script.py");
    assert(pythonDefinition.source.origin == ToolOrigin::Custom && pythonDefinition.source.roots.python.empty());
    const auto pythonRoundtrip = parseToolDefinitionJson(emitNormalizedToolJson(pythonDefinition));
    assert(pythonRoundtrip.entry == pythonDefinition.entry);
    for (const auto field : {"package", "file", "symbol"})
    {
        auto missing       = python;
        auto implementation = missing["implementation"].toObject();
        implementation.remove(field);
        missing["implementation"] = implementation;
        rejectObject(missing);
    }
    {
        auto bad          = python;
        bad["init_params"] = QJsonArray{};
        rejectObject(bad);
    }

    // A host tool declares its binding in implementation and its dynamic output by reference.
    auto host              = emitted;
    host["implementation"] = QJsonObject{{"kind", "host"}, {"binding", "visionrt.receiver"}};
    host["next_port_id"]   = QJsonObject{{"init", 3}, {"inputs", 2}, {"outputs", 3}};
    auto hostPorts         = host["ports"].toObject();
    // 源端口编号按数值传递，因此第三个初始化参数是 int。
    hostPorts["init"]      = QJsonArray{
        QJsonObject{{"id", 0}, {"name_en", "flow"}, {"name_zh", "流程"}, {"type", "string"}},
        QJsonObject{{"id", 1}, {"name_en", "node"}, {"name_zh", "节点"}, {"type", "string"}},
        QJsonObject{{"id", 2}, {"name_en", "port"}, {"name_zh", "端口"}, {"type", "int"}}
    };
    QJsonObject dynamic{
        {       "id",                                                            2},
        {  "name_en",                                                      "value"},
        {  "name_zh",                                                          "值"},
        {"type_from", QJsonObject{{"flow", QJsonObject{{"stage", "init"}, {"id", 0}}},
                                  {"node", QJsonObject{{"stage", "init"}, {"id", 1}}},
                                  {"port", QJsonObject{{"stage", "init"}, {"id", 2}}}}}
    };
    hostPorts["outputs"] = QJsonArray{dynamic};
    host["ports"]        = hostPorts;
    const auto receiver  = parseToolDefinitionJson(QJsonDocument(host).toJson().toStdString());
    assert(receiver.hostBinding == "visionrt.receiver" && receiver.outputs.back().typeFrom);
    assert(parseToolDefinitionJson(emitNormalizedToolJson(receiver)).outputs.back().typeFrom);
    dynamic["type"]      = "int";
    hostPorts["outputs"] = QJsonArray{dynamic};
    host["ports"]        = hostPorts;
    rejectObject(host);

    std::cout << "Qt JSON tool declarations preserve values and derive system outputs\n";
}
