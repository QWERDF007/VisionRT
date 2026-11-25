#include "ToolResultAssertions.h"
#include "ui/OutputModel.h"
#include "core/ToolValueValidation.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <limits>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template<class Vector>
Vector nestedSample()
{
    using Element = typename Vector::value_type;
    if constexpr (std::is_same_v<Element, std::string>)
        return {"中文\n\"图像\"", "", "尾部"};
    else if constexpr (std::is_arithmetic_v<Element>)
        return {static_cast<Element>(1), static_cast<Element>(0)};
    else
        return {nestedSample<Element>(), {}, nestedSample<Element>()};
}

template<class Element, int Rank>
void arrayDisplay()
{
    PortValue      value = Array::fromNested(nestedSample<NestedVector<Element, Rank>>());
    const auto     type  = *typeOf(value);
    ToolDefinition definition;
    definition.outputs = {
        {2, "数组", type}
    };
    auto snapshot = std::make_shared<NodeSnapshot>();
    snapshot->outputs.emplace(2, value);
    OutputModel model;
    model.setResult(&definition, snapshot, {});
    const auto full = OutputModel::fullText(*model.capture(2));
    const auto text = model.get(2)["preview"].toString();
    require(!model.get(2)["typeDisplay"].toString().contains(QStringLiteral("位")),
            "UI array type descriptions must omit bit widths");
    require(text == full, "Array display is not the complete encoded value");
    require(QJsonDocument::fromJson(text.toUtf8()).array() == QJsonDocument::fromJson(full.toUtf8()).array(),
            "Array preview flattened nested values or did not escape native strings");
    require(OutputModel::estimatedTextBytes(*model.capture(2)) >= static_cast<std::size_t>(full.toUtf8().size()),
            "Array full text estimate is below the encoded bytes");
    if constexpr (Rank < 4)
        arrayDisplay<Element, Rank + 1>();
}
}

int main(int argc, char **argv)
try
{
    QCoreApplication application(argc, argv);
    {
        auto detection = hostPort(PortStage::Outputs, kStatusCodePort);
        detection.id = 2;
        detection.nameZh = "检测值";
        detection.producedBy = ProducedBy::Execute;
        auto training = detection;
        training.id = 3;
        training.nameZh = "训练值";
        training.producedBy = ProducedBy::Train;
        const std::vector<PortDefinition> ports{hostPort(PortStage::Outputs, 0), hostPort(PortStage::Outputs, 1), detection, training};
        auto result = toolResult({{3, std::int64_t{7}}}, executionSuccess());
        require(validateToolExecution(ports, result.outputs, {}, ProducedBy::Train).code == 0
                    && isMissing(result.outputs.at(2)), "training must clear detection-only outputs");
        result = toolResult({{2, std::int64_t{9}}}, executionSuccess());
        require(validateToolExecution(ports, result.outputs, {}).code == 0 && isMissing(result.outputs.at(3)),
                "execution must clear training-only outputs");
        result = toolResult({}, executionSuccess());
        bool rejected = false;
        try { validateToolExecution(ports, result.outputs, {}, ProducedBy::Train); }
        catch (const ContractError &) { rejected = true; }
        require(rejected, "a produced output remains mandatory");
    }
    {
        ToolDefinition decimalOutputs;
        decimalOutputs.outputs = {
            {2, "小数", {Type::Float}},
            {3, "对齐数据", {Type::Array, DType::Float64, 3}},
            {4, "圆", {Type::Circle}},
            {5, "单精度数组", {Type::Array, DType::Float32, 2}},
            {6, "文本", {Type::String}}
        };
        auto precise = std::make_shared<NodeSnapshot>();
        precise->outputs = {
            {2, 1.123456789},
            {3, Array::fromNested(NestedVector<double, 3>{{{1.123456789, -2.7755575615628914e-17,
                                                           2.7755575615628914e-15, 1.23, 1}}, {}})},
            {4, Circle::fromEncoded(ROI{{makeCircle({10.123456789, 20.123456789}, 3.25)}})},
            {5, Array::fromNested(NestedVector<float, 2>{{0.123456789f, 1}})},
            {6, std::string("1.000000")}
        };
        OutputModel decimals;
        decimals.setResult(&decimalOutputs, precise, {});
        require(decimals.get(2)["preview"] == "1.123457", "output floats must display at most six decimals");
        require(decimals.get(3)["preview"] == "[[[1.123457,0,0,1.23,1]],[]]",
                "output arrays must omit trailing zeros and rounded negative zero");
        require(decimals.get(3)["typeDisplay"] == QStringLiteral("三维浮点数数组"),
                "double array descriptions must omit the redundant bit width");
        require(decimals.get(4)["preview"] == "[[[10.123457,20.123457,3.25]]]",
                "output ROI coordinates must share the numeric display policy");
        require(decimals.get(5)["preview"] == "[[0.123457,1]]", "float32 arrays did not use the display precision");
        require(decimals.get(5)["typeDisplay"] == QStringLiteral("二维单精度浮点数数组"),
                "float32 arrays must use a semantic description without a bit width");
        require(decimals.get(6)["preview"] == "1.000000", "numeric display formatting changed string contents");
        const auto original = QJsonDocument::fromJson(OutputModel::fullText(*decimals.capture(3)).toUtf8()).array();
        require(original[0].toArray()[0].toArray()[0].toDouble() == 1.123456789
                    && std::get<Array>(precise->outputs.at(3)).elements<double>()[0] == 1.123456789,
                "output display formatting changed copied or native values");
        precise->outputs[2] = std::numeric_limits<double>::infinity();
        precise->outputs[3] = Array::fromNested(NestedVector<double, 3>{{{std::numeric_limits<double>::quiet_NaN(),
                                                                        2.123456789}}});
        decimals.setResult(&decimalOutputs, precise, {});
        require(QJsonDocument::fromJson(decimals.get(2)["preview"].toString().toUtf8()).object()["non_finite"] == "Infinity",
                "non-finite scalar display lost its typed representation");
        const auto exceptional = QJsonDocument::fromJson(decimals.get(3)["preview"].toString().toUtf8()).array()[0].toArray()[0].toArray();
        require(exceptional[0].toObject()["non_finite"] == "NaN" && exceptional[1].toDouble() == 2.123457,
                "non-finite array elements were lost during numeric formatting");
    }
    arrayDisplay<std::uint8_t, 1>();
    arrayDisplay<std::int32_t, 1>();
    arrayDisplay<std::int64_t, 1>();
    arrayDisplay<float, 1>();
    arrayDisplay<double, 1>();
    arrayDisplay<bool, 1>();
    arrayDisplay<std::string, 1>();
    ToolDefinition definition;
    // Outputs are numbered 2..5: 0 and 1 belong to the host-owned status envelope.
    definition.outputs = {
        {2, "数组", {Type::Array, DType::Int64, 2}},
        {3, "区域", {Type::Circle}},
        {4, "未运行", {Type::String}},
        {5, "文字", {Type::String}}
    };
    auto snapshot         = std::make_shared<NodeSnapshot>();
    snapshot->status      = {0, "运行成功"};
    snapshot->publication = 19;
    snapshot->outputs     = {
        {2, Array::fromNested(NestedVector<std::int64_t, 2>{{9007199254740993LL}, {}, {4, 5}})},
        {3, Circle::fromEncoded(ROI{{{3., 1., 2., 3.}}, {}})},
        {4, Missing{}},
        {5, std::string("中文\n\"图像\"")}
    };
    OutputModel model;
    model.setResult(&definition, snapshot,
                    {
                        {2, {{"3", 1, "下游-3-输入"}, {"4", 1, "下游-4-输入"}}}
    });
    const auto values = model.capture(2);
    require(values && values->publication == 19, "output publication lost");
    const auto data = QJsonDocument::fromJson(OutputModel::fullText(*values).toUtf8()).array();
    require(data.size() == 3 && data[0].toArray()[0].toInteger() == 9007199254740993LL && data[1].toArray().isEmpty(),
            "nested Array output lost shape or int64 precision");
    require(OutputModel::fullText(*model.capture(4)) == "null", "Missing copy is not JSON null");
    require(model.get(4)["preview"].toString().isEmpty(), "Missing preview is not blank");
    require(OutputModel::fullText(*model.capture(5)) == "\"中文\\n\\\"图像\\\"\"",
            "string output not JSON escaped");
    require(model.get(2)["type"].toString() == "Array<int64> rank=2",
            "output type still exposes layout or a legacy type");
    require(model.get(2)["preview"].toString().contains("9007199254740993"), "Array preview lost integer precision");
    require(model.get(2)["count"].toULongLong() == 2, "output relation count changed");
    const auto frozen = model.get(2)["preview"].toString();
    std::get<Array>(snapshot->outputs.at(2)).elements<std::int64_t>()[0] = 71;
    require(model.get(2)["preview"].toString() == frozen, "cached display text borrowed mutable output storage");
    model.setResult(&definition, snapshot, {});
    require(model.get(2)["preview"].toString().contains("71"), "next safe display update did not read current values");
    // Growing the structure replaces the value: published storage is never resized in place.
    snapshot->outputs[2] = Array::fromNested(NestedVector<std::int64_t, 2>(10000, {1, 2, 3}));
    model.setResult(&definition, snapshot, {});
    require(model.get(2)["preview"].toString() == OutputModel::fullText(*model.capture(2))
                && model.get(2)["tooltip"].toString() == model.get(2)["preview"].toString(),
            "output display replaced array values with a truncated preview");
    require(OutputModel::fullText(*model.capture(2)).size() > 4096, "full output was replaced with a preview");
    const auto regions = QJsonDocument::fromJson(OutputModel::fullText(*model.capture(3)).toUtf8()).array();
    require(regions.size() == 2 && regions[0].toArray()[0].toArray().size() == 3,
            "circle lost its headerless three-level coordinate encoding");
    snapshot->outputs[3] = Circle::fromNested({{{5, 6, 2}, {7, 8, 3}}});
    snapshot->outputs[5] = QString(10000, QChar(0x4e2d)).toUtf8().toStdString();
    model.setResult(&definition, snapshot, {});
    require(model.get(3)["typeDisplay"].toString() == QStringLiteral("圆"),
            "output type hid its concrete geometry kinds");
    require(model.get(5)["preview"].toString() == QString(10000, QChar(0x4e2d)),
            "long Unicode string display was truncated");
    std::cout << "Native output values preserve complete data, concrete geometry and independent cached text\n";
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
