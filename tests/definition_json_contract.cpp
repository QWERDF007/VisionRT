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
try
{
    QTemporaryDir directory;
    assert(directory.isValid());
    const auto name = directory.path() + "/声明.json";
    QFile      file(name);
    assert(file.open(QIODevice::WriteOnly));
    file.write(R"json({
  "name_en": "test.json",
  "name_zh": "JSON 声明",
  "version": "1.0.0",
  "implementation": {
    "kind": "cpp"
  },
  "init_params": [
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
    // The three groups are plain top-level arrays; there is no wrapping object and no format number.
    assert(!emitted.contains("ports") && !emitted.contains("schema_version") && !emitted.contains("next_port_id"));
    assert(emitted["outputs"].toArray().size() == 1);
    // Host-owned common ports are materialised in the effective contract but never written back
    // into the author declaration.
    assert(emitted["inputs"].toArray().size() == 1 && emitted["inputs"].toArray()[0].toObject()["id"].toInt() == 1);
    assert(!emitted["implementation"].toObject().contains("entry"));
    const auto roundTrip = parseToolDefinitionJson(normalized);
    assert(equalValues(roundTrip.initParams[0].defaultValue, definition.initParams[0].defaultValue));
    assert(equalValues(roundTrip.inputs[1].defaultValue, definition.inputs[1].defaultValue));

    // display.visible is the author's image-selector choice: Image only, absent means false, and it
    // survives the emit/reparse round trip instead of being a UI-only flag.
    auto imageDeclaration       = emitted;
    imageDeclaration["inputs"]  = QJsonArray{
        QJsonObject{{"id", 1}, {"name_en", "source"}, {"name_zh", "源图"}, {"type", "image"}},
        QJsonObject{{"id", 2},
                    {"name_en", "mask"},
                    {"name_zh", "掩模"},
                    {"type", "image"},
                    {"display", QJsonObject{{"visible", true}}}}
    };
    const auto imageParsed = parseToolDefinitionJson(QJsonDocument(imageDeclaration).toJson().toStdString());
    assert(!imageParsed.inputs[1].imageVisible);
    assert(imageParsed.inputs[2].imageVisible);
    const auto imageEmitted
        = QJsonDocument::fromJson(QByteArray::fromStdString(emitNormalizedToolJson(imageParsed))).object();
    const auto imageEmittedInputs = imageEmitted["inputs"].toArray();
    assert(!imageEmittedInputs[0].toObject()["display"].toObject()["visible"].toBool());
    assert(imageEmittedInputs[1].toObject()["display"].toObject()["visible"].toBool());

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

    // A port list is authored under inputs and referenced by stage-qualified references.
    auto crop          = emitted;
    // 端口编号不重复即可，名称只是说明。
    crop["inputs"] = QJsonArray{
        QJsonObject{{"id", 2}, {"name_en", "images"}, {"name_zh", "图像"}, {"type", "image"}},
        QJsonObject{{"id", 3},
                    {"name_en", "rect"},
                    {"name_zh", "矩形"},
                    {"type", "roi"},
                    {"ui", QJsonObject{{"editor", "rect"}}},
                    {"relations", QJsonObject{{"image", QJsonObject{{"stage", "inputs"}, {"id", 2}}}}}}
    };
    const auto cropDefinition = parseToolDefinitionJson(QJsonDocument(crop).toJson().toStdString());
    assert(cropDefinition.inputs[2].type.type == Type::ROI && cropDefinition.inputs[2].editor == "rect");
    assert((cropDefinition.inputs[2].image == PortRef{PortStage::Inputs, 2}));
    assert(parseToolDefinitionJson(emitNormalizedToolJson(cropDefinition)).inputs[2].editor == "rect");

    // display.visible belongs to Image ports and is a boolean, never a truthy string.
    auto visibleOnInt      = emitted;
    visibleOnInt["inputs"] = QJsonArray{
        QJsonObject{{"id", 1},
                    {"name_en", "value"},
                    {"name_zh", "数值"},
                    {"type", "int"},
                    {"display", QJsonObject{{"visible", true}}}}
    };
    rejectObject(visibleOnInt);

    auto visibleText      = imageDeclaration;
    visibleText["inputs"] = QJsonArray{
        QJsonObject{{"id", 1},
                    {"name_en", "source"},
                    {"name_zh", "源图"},
                    {"type", "image"},
                    {"display", QJsonObject{{"visible", "yes"}}}}
    };
    rejectObject(visibleText);

    auto badEditor      = crop;
    badEditor["inputs"] = QJsonArray{
        QJsonObject{{"id", 2}, {"name_en", "text"}, {"name_zh", "文本"}, {"type", "string"}, {"ui", QJsonObject{{"editor", "rect"}}}}
    };
    rejectObject(badEditor);

    // Old wrappers and any other unknown root field are ordinary field-validation failures.
    for (const auto field : {"extra", "tool_id", "display_name", "kind", "entry", "schema_version", "ports",
                             "next_port_id"})
    {
        auto invalid   = emitted;
        invalid[field] = 1;
        rejectObject(invalid);
    }
    {
        auto wrapped      = emitted;
        wrapped["ports"]  = QJsonObject{{"inputs", emitted["inputs"]}};
        wrapped.remove("inputs");
        rejectObject(wrapped);
    }
    {
        auto versioned       = emitted;
        versioned["schema_version"] = 6;
        rejectObject(versioned);
    }

    // One token per type: the old three-field spelling and capitalised names are gone.
    for (const auto token : {"Int", "Array", "Float64", "vec_float64", "vec_vec_vec_vec_vec_int"})
    {
        auto declaration = emitted;
        auto inputs      = declaration["inputs"].toArray();
        auto first       = inputs.first().toObject();
        first["type"]    = token;
        inputs.replace(0, first);
        declaration["inputs"] = inputs;
        rejectObject(declaration);
    }

    // A port keeps identity, type and value; the removed required flag is an unknown field.
    for (const auto field : {"required", "allow_missing"})
    {
        auto declaration = emitted;
        auto inputs      = declaration["inputs"].toArray();
        auto first       = inputs.first().toObject();
        first[field]     = true;
        inputs.replace(0, first);
        declaration["inputs"] = inputs;
        rejectObject(declaration);
    }

    // Duplicate and reserved numbers stay author errors.
    {
        auto declaration = emitted;
        auto inputs      = declaration["inputs"].toArray();
        inputs.append(inputs.first());
        declaration["inputs"] = inputs;
        rejectObject(declaration);
    }
    {
        // 编号是身份：声明不得占用宿主保留的状态输出编号 0/1。
        auto declaration  = emitted;
        auto outputs      = declaration["outputs"].toArray();
        auto first        = outputs.first().toObject();
        first["id"]       = 1;
        outputs.replace(0, first);
        declaration["outputs"] = outputs;
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

    auto python              = emitted;
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
        // A function tool has no configuration at all; declaring one stays an author error.
        auto bad              = python;
        bad["implementation"] = QJsonObject{
            {"kind", "python_function"}, {"package", "p"}, {"file", "f.py"}, {"symbol", "execute"}
        };
        bad["init_params"] = QJsonArray{QJsonObject{{"id", 0}, {"name_zh", "种子"}, {"type", "int"}}};
        rejectObject(bad);
    }

    // A host tool declares its binding in implementation and its dynamic output by reference.
    auto host              = emitted;
    host["implementation"] = QJsonObject{{"kind", "host"}, {"binding", "visionrt.receiver"}};
    // 源端口编号按数值传递，因此第三个初始化参数是 int。
    host["init_params"]    = QJsonArray{
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
    host["outputs"]     = QJsonArray{dynamic};
    const auto receiver = parseToolDefinitionJson(QJsonDocument(host).toJson().toStdString());
    assert(receiver.hostBinding == "visionrt.receiver" && receiver.outputs.back().typeFrom);
    assert(parseToolDefinitionJson(emitNormalizedToolJson(receiver)).outputs.back().typeFrom);
    dynamic["type"]  = "int";
    host["outputs"]  = QJsonArray{dynamic};
    rejectObject(host);

    // The declaration writer replaces a file only after the whole document is written, and an
    // invalid declaration never replaces what is already there.
    {
        const auto target = directory.path() + QStringLiteral("/写入.json");
        auto       draft  = parseToolDefinitionJson(QJsonDocument(emitted).toJson().toStdString());
        draft.nameZh      = "写入后的声明";
        const auto stored = writeToolDefinitionFile(target.toStdWString(), draft);
        assert(stored.definitionPath == std::filesystem::path(target.toStdWString()));
        assert(parseToolDefinitionFile(target.toStdWString()).nameZh == "写入后的声明");

        auto broken = draft;
        broken.nameZh.clear();
        bool refused = false;
        try
        {
            (void)writeToolDefinitionFile(target.toStdWString(), broken);
        }
        catch (const std::exception &)
        {
            refused = true;
        }
        assert(refused && parseToolDefinitionFile(target.toStdWString()).nameZh == "写入后的声明");
    }

    std::cout << "Qt JSON tool declarations preserve values and derive system outputs\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
