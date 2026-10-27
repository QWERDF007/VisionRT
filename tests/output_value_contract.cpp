#include "ui/OutputModel.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <iostream>

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
void arrayPreviews()
{
    PortValue      value = Array(nestedSample<NestedVector<Element, Rank>>());
    const auto     type  = *typeOf(value);
    ToolDefinition definition;
    definition.outputs = {
        {"data", "数组", type}
    };
    auto snapshot = std::make_shared<NodeSnapshot>();
    snapshot->outputs.emplace("data", value);
    OutputModel model;
    model.setResult(&definition, snapshot, {});
    const auto full = OutputModel::fullText(*model.capture("data"));
    const auto text = model.get(2)["preview"].toString();
    require(!text.isEmpty() && text.size() <= 256, "Array dtype/rank preview is empty or unbounded");
    require(QJsonDocument::fromJson(text.toUtf8()).array() == QJsonDocument::fromJson(full.toUtf8()).array(),
            "Array preview flattened nested values or did not escape native strings");
    require(OutputModel::estimatedTextBytes(*model.capture("data")) >= static_cast<std::size_t>(full.toUtf8().size()),
            "Array full text estimate is below the encoded bytes");
    if constexpr (Rank < 4)
        arrayPreviews<Element, Rank + 1>();
}
}

int main(int argc, char **argv)
try
{
    QCoreApplication application(argc, argv);
    arrayPreviews<std::uint8_t, 1>();
    arrayPreviews<std::int32_t, 1>();
    arrayPreviews<std::int64_t, 1>();
    arrayPreviews<float, 1>();
    arrayPreviews<double, 1>();
    arrayPreviews<bool, 1>();
    arrayPreviews<std::string, 1>();
    ToolDefinition definition;
    definition.outputs = {
        { "values",    "数组", {Type::Array, DType::Int64, 2}},
        {"regions",    "区域",                    {Type::ROI}},
        {  "empty", "未运行",                 {Type::String}},
        {   "text",    "文字",                 {Type::String}}
    };
    auto snapshot         = std::make_shared<NodeSnapshot>();
    snapshot->status      = {0, "运行成功"};
    snapshot->publication = 19;
    snapshot->outputs     = {
        {"values", Array(NestedVector<std::int64_t, 2>{{9007199254740993LL}, {}, {4, 5}})},
        {"regions", DataRef<ROI>(ROI{{{3., 1., 2., 3.}}, {}})},
        {"empty", Missing{}},
        {"text", std::string("中文\n\"图像\"")}
    };
    OutputModel model;
    model.setResult(&definition, snapshot,
                    {
                        {"values", 2}
    });
    const auto values = model.capture("values");
    require(values && values->publication == 19, "output publication lost");
    const auto data = QJsonDocument::fromJson(OutputModel::fullText(*values).toUtf8()).array();
    require(data.size() == 3 && data[0].toArray()[0].toInteger() == 9007199254740993LL && data[1].toArray().isEmpty(),
            "nested Array output lost shape or int64 precision");
    require(OutputModel::fullText(*model.capture("empty")) == "null", "Missing copy is not JSON null");
    require(model.get(4)["preview"].toString().isEmpty(), "Missing preview is not blank");
    require(OutputModel::fullText(*model.capture("text")) == "\"中文\\n\\\"图像\\\"\"",
            "string output not JSON escaped");
    require(model.get(2)["type"].toString() == "Array<int64> rank=2",
            "output type still exposes layout or a legacy type");
    require(model.get(2)["preview"].toString().contains("9007199254740993"), "Array preview lost integer precision");
    require(model.get(2)["count"].toULongLong() == 2, "output relation count changed");
    const auto frozen = model.get(2)["preview"].toString();
    std::get<Array>(snapshot->outputs.at("values")).get<NestedVector<std::int64_t, 2>>()[0][0] = 71;
    require(model.get(2)["preview"].toString() == frozen, "cached display text borrowed mutable output storage");
    model.setResult(&definition, snapshot, {});
    require(model.get(2)["preview"].toString().contains("71"), "next safe display update did not read current values");
    std::get<Array>(snapshot->outputs.at("values")).get<NestedVector<std::int64_t, 2>>().assign(10000, {1, 2, 3});
    model.setResult(&definition, snapshot, {});
    require(model.get(2)["preview"].toString().size() <= 256 && model.get(2)["tooltip"].toString().size() <= 4096,
            "output preview expanded the complete Array");
    require(OutputModel::fullText(*model.capture("values")).size() > 4096, "full output was replaced with a preview");
    const auto regions = QJsonDocument::fromJson(OutputModel::fullText(*model.capture("regions")).toUtf8()).array();
    require(regions.size() == 2 && regions[0].toArray()[0].toArray().size() == 4,
            "ROI lost native three-level list encoding");
    std::cout << "Native output values preserve JSON, bounded previews and independent cached text\n";
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
