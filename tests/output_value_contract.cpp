#include "ToolResultAssertions.h"
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
        {3, "区域", {Type::ROI}},
        {4, "未运行", {Type::String}},
        {5, "文字", {Type::String}}
    };
    auto snapshot         = std::make_shared<NodeSnapshot>();
    snapshot->status      = {0, "运行成功"};
    snapshot->publication = 19;
    snapshot->outputs     = {
        {2, Array::fromNested(NestedVector<std::int64_t, 2>{{9007199254740993LL}, {}, {4, 5}})},
        {3, DataRef<ROI>(ROI{{{3., 1., 2., 3.}}, {}})},
        {4, Missing{}},
        {5, std::string("中文\n\"图像\"")}
    };
    OutputModel model;
    model.setResult(&definition, snapshot,
                    {
                        {2, 2}
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
    require(regions.size() == 2 && regions[0].toArray()[0].toArray().size() == 4,
            "ROI lost native three-level list encoding");
    snapshot->outputs[3] = DataRef<ROI>(ROI{{makePoint(1, 2), makeSegment({0, 0}, {1, 1}),
        makeRectangle(1, 2, 3, 4), makeCircle({5, 6}, 2),
        makePolygon(std::vector<cv::Point2d>{{0, 0}, {4, 0}, {4, 3}})}});
    snapshot->outputs[5] = QString(10000, QChar(0x4e2d)).toUtf8().toStdString();
    model.setResult(&definition, snapshot, {});
    require(model.get(3)["typeDisplay"].toString() == QStringLiteral("点、线、矩形、圆、多边形"),
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
