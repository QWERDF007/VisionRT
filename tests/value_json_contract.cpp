#include "core/ValueJson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>

using namespace visionrt;

namespace {
template<class Action>
void rejects(Action action)
{
    bool rejected = false;
    try { action(); }
    catch (const ContractError &) { rejected = true; }
    assert(rejected);
}

void roundtrip(const PortValue &value, const TypeSpec &type)
{
    assert(ValueJson::decodeType(ValueJson::encodeType(type)) == type);
    assert(equalValues(value, ValueJson::parse(ValueJson::text(value, type), type)));
}
}

int main()
{
    const TypeSpec type{Type::Array, DType::Int64, 2};
    const auto     value = ValueJson::parse("[[9007199254740993], [], [-9223372036854775808]]", type);
    const auto    &items = std::get<Array>(value).get<NestedVector<std::int64_t, 2>>();
    assert(items.size() == 3 && items[0][0] == 9007199254740993LL && items[1].empty());
    assert(items[2][0] == std::numeric_limits<std::int64_t>::min());
    assert(equalValues(value, ValueJson::parse(ValueJson::text(value, type), type)));
    assert(ValueJson::encode(value, type).toArray()[1].toArray().isEmpty());
    assert(ValueJson::decodeType(ValueJson::encodeType(type)) == type);
    const std::map<DType, QString> leaves{
        {  DType::UInt8,                                                     "[0,255]"},
        {  DType::Int32,                                    "[-2147483648,2147483647]"},
        {  DType::Int64, "[-9223372036854775808,9223372036854775807,9007199254740993]"},
        {DType::Float32,                                                 "[1.25,-2.5]"},
        {DType::Float64,                                         "[16777217.125,-2.5]"},
        {   DType::Bool,                                                "[true,false]"},
        { DType::String,                          R"(["中文","\"\\\n", "null", ""])"}
    };
    std::size_t checked = 0;
    for (const auto &arrayType : Array::supportedTypes())
    {
        auto source = leaves.at(*arrayType.dtype);
        for (int rank = 1; rank < arrayType.rank; ++rank) source = '[' + source + ", []]";
        const auto decoded = ValueJson::parse(source, arrayType);
        assert(ValueJson::encode(decoded, arrayType).toArray() == QJsonDocument::fromJson(source.toUtf8()).array());
        roundtrip(decoded, arrayType);
        roundtrip(Array::empty(*arrayType.dtype, arrayType.rank), arrayType);
        if (arrayType.rank > 1)
            rejects([&] { ValueJson::parse(leaves.at(*arrayType.dtype), arrayType); });
        ++checked;
    }
    assert(checked == 28);
    const ValueJson::Document document(R"json({"nodes":[{
        "integer":-9223372036854775809,"valid":-9223372036854775808,
        "real":-9223372036854775809,"array":[[],[-9223372036854775809]],
        "text":"quoted \"123\" and -9223372036854775809"}]})json");
    rejects([&] { document.decode({"nodes", "0", "integer"}, {Type::Int}); });
    rejects([&] { document.decode({"nodes", "0", "array"}, type); });
    assert(std::get<std::int64_t>(document.decode({"nodes", "0", "valid"}, {Type::Int}))
           == std::numeric_limits<std::int64_t>::min());
    assert(std::isfinite(std::get<double>(document.decode({"nodes", "0", "real"}, {Type::Float}))));
    assert(std::get<std::string>(document.decode({"nodes", "0", "text"}, {Type::String}))
               .ends_with("-9223372036854775809"));
    rejects([&] { document.decode({"nodes", "2", "valid"}, {Type::Int}); });
    rejects([] { ValueJson::Document(R"({"broken":[1,]})"); });
    for (const auto number : {std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max(),
                              std::int64_t{9007199254740991}, std::int64_t{9007199254740993}})
    {
        roundtrip(number, {Type::Int});
        assert(ValueJson::text(number, {Type::Int}) == QString::number(number));
    }
    roundtrip(Missing{}, {Type::String});
    roundtrip(std::int64_t{0}, {Type::Int});
    roundtrip(false, {Type::Bool});
    roundtrip(std::string{}, {Type::String});
    roundtrip(std::string("中文 \"引号\"\nC:\\Images\\null"), {Type::String});
    roundtrip(1.25, {Type::Float});
    roundtrip(DataRef<Image>(Image{}), {Type::Image});
    const DataRef<ROI> regions(ROI{
        {},
        {{3, 2, 3, 1}, {2, 16777217.125, -2.5, 3.75, 4.5, 17.5}}
    });
    roundtrip(regions, {Type::ROI});
    const auto emptyRoi   = ValueJson::parse("[]", {Type::ROI});
    const auto emptyGroup = ValueJson::parse("[[]]", {Type::ROI});
    roundtrip(emptyRoi, {Type::ROI});
    roundtrip(emptyGroup, {Type::ROI});
    assert(!equalValues(emptyRoi, emptyGroup));
    for (const auto invalid : {"", "1,2", "1 trailing", "~", "yes", "[1,]", "NaN", "Infinity", ".nan"})
        rejects([&] { ValueJson::parse(invalid, {Type::Float}); });
    for (const auto invalid : {"true", "\"17\"", "1.5", "1.0", "1e2", "9223372036854775808", "-9223372036854775809"})
    {
        rejects([&] { ValueJson::parse(invalid, {Type::Int}); });
        rejects([&] { ValueJson::parse('[' + QString(invalid) + ']', {Type::Array, DType::Int64, 1}); });
    }
    rejects([] { ValueJson::parse("1", {Type::Bool}); });
    rejects([] { ValueJson::parse("false", {Type::String}); });
    rejects([] { ValueJson::decode(QJsonValue::Undefined, {Type::Int}); });
    rejects([] { ValueJson::parse(R"({"shape":[1],"data":[1]})", {Type::Array, DType::Int64, 1}); });
    rejects([] { ValueJson::parse("[[1]]", {Type::Array, DType::Int64, 1}); });
    for (const auto invalid : {"[-1]", "[256]", "[true]"})
        rejects([&] { ValueJson::parse(invalid, {Type::Array, DType::UInt8, 1}); });
    rejects([] { ValueJson::parse("[2147483648]", {Type::Array, DType::Int32, 1}); });
    rejects([] { ValueJson::parse("[1]", {Type::Array, DType::Bool, 1}); });
    rejects([] { ValueJson::parse("[1]", {Type::Array, DType::String, 1}); });
    rejects([] { ValueJson::parse("[1e100]", {Type::Array, DType::Float32, 1}); });
    for (const auto invalid : {"[3,1,2,3]", "[[[2,0,0,0,1,0]]]", "[[[3,1,2,-1]]]", "[[[9,1,2]]]"})
        rejects([&] { ValueJson::parse(invalid, {Type::ROI}); });
    rejects([] { ValueJson::parse("[1]", {Type::Image}); });
    for (const auto old : {"NumericArray", "StringArray", "ImageBatch", "Rect", "RoiBatch", "Unknown"})
        rejects(
            [&]
            {
                ValueJson::decodeType({
                    {"type", old}
                });
            });
    rejects([] { ValueJson::decodeType({{"type", "Int"}, {"rank", 1}}); });
    rejects(
        []
        {
            ValueJson::decodeType({
                { "type",   "Array"},
                {"dtype", "float64"},
                { "rank",       1.5}
            });
        });
    for (const auto rank : {0, 5})
        rejects(
            [&]
            {
                ValueJson::decodeType({
                    { "type",  "Array"},
                    {"dtype", "string"},
                    { "rank",     rank}
                });
            });
    rejects(
        []
        {
            ValueJson::decodeType({
                {"type", "Array"},
                {"rank",       1}
            });
        });
    rejects(
        []
        {
            ValueJson::decodeType({
                {  "type",  "Array"},
                { "dtype", "string"},
                {  "rank",        1},
                {"layout",  "dense"}
            });
        });
    for (const auto number : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                              -std::numeric_limits<double>::infinity()})
    {
        rejects([&] { ValueJson::text(number, {Type::Float}); });
        const auto display = ValueJson::encode(number, {Type::Float}, ValueJson::Purpose::Display).toObject();
        assert(display.value("type") == "Float" && display.value("non_finite").isString());
        rejects([&] { ValueJson::decode(display, {Type::Float}); });
    }
    const TypeSpec  floating{Type::Array, DType::Float64, 2};
    const PortValue mixed = Array(NestedVector<double, 2>{
        {0, std::numeric_limits<double>::infinity(), 2},
        {}
    });
    rejects([&] { ValueJson::text(mixed, floating); });
    const auto display = ValueJson::encode(mixed, floating, ValueJson::Purpose::Display).toArray();
    const auto data    = display[0].toArray();
    assert(display[1].toArray().isEmpty() && data.size() == 3 && data[0].toInteger() == 0 && data[2].toInteger() == 2);
    assert(data[1].toObject().value("type") == "float64" && data[1].toObject().value("non_finite") == "Infinity");
    rejects([&] { ValueJson::decode(display, floating); });
    const PortValue images = DataRef<Image>(Image{cv::Mat(2, 3, CV_8UC1, cv::Scalar(7))});
    rejects([&] { ValueJson::text(images, {Type::Image}); });
    const auto description = ValueJson::encode(images, {Type::Image}, ValueJson::Purpose::Display).toObject();
    assert(description.value("type") == "Image" && description.value("count").toInteger() == 1);
    assert(description.value("images").toArray()[0].toObject().value("rows").toInteger() == 2);
    rejects([&] { ValueJson::decode(description, {Type::Image}); });
    std::cout << "28 native nested array types, ROI precision, int64 lexemes and explicit display descriptions\n";
}
