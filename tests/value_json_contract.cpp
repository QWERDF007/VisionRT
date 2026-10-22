#include "core/ValueJson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace visionrt;

namespace {
template<class Action>
void rejects(Action action, const char *context = "invalid JSON contract")
{
    bool rejected = false;
    try { action(); }
    catch (const ContractError &) { rejected = true; }
    if (!rejected)
        std::cerr << "Unexpectedly accepted: " << context << '\n';
    assert(rejected);
}

void roundtrip(const PortValue &value, const TypeSpec &type)
{
    assert(ValueJson::decodeType(ValueJson::encodeType(type)) == type);
    const auto encoded = ValueJson::text(value, type);
    assert(equalValues(value, ValueJson::parse(encoded, type)));
}

template<class Element>
void arrays(DType dtype, std::span<const Element> elements)
{
    for (const auto layout : {Layout::Dense, Layout::BatchRagged})
    {
        const TypeSpec type{Type::NumericArray, dtype, 2, layout};
        const auto shape = layout == Layout::Dense ? std::vector<std::int64_t>{1, 2}
                                                   : std::vector<std::int64_t>{2};
        auto buffer = NumericBuffer::copy(dtype, shape, std::as_bytes(elements));
        std::vector<NumericBuffer> buffers{buffer};
        if (layout == Layout::BatchRagged)
            buffers.push_back(NumericBuffer::copy(dtype, {0}, {}));
        roundtrip(NumericArray(dtype, 2, layout, std::move(buffers)), type);
    }
}
}

int main()
{
    for (const auto number : {std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max(),
                              std::int64_t{9007199254740991}, std::int64_t{9007199254740993}})
    {
        roundtrip(number, {Type::Int});
        assert(ValueJson::text(number, {Type::Int}) == QString::number(number));
    }
    roundtrip(Missing{}, {Type::String});
    roundtrip(std::int64_t{0}, {Type::Int});
    roundtrip(false, {Type::Bool});
    roundtrip(Utf8(""), {Type::String});
    roundtrip(1.25, {Type::Float});
    roundtrip(Utf8("中文 \"引号\"\nC:\\Images\\null"), {Type::String});
    roundtrip(ImageBatch{}, {Type::ImageBatch});
    roundtrip(NumericArray(DType::Int64, 2, Layout::BatchRagged, {}),
              {Type::NumericArray, DType::Int64, 2, Layout::BatchRagged});
    roundtrip(NumericArray(DType::Float64, 1, Layout::Dense,
              {NumericBuffer::copy(DType::Float64, {0}, {})}), {Type::NumericArray, DType::Float64, 1});
    roundtrip(Roi::rectangle(1.25F, -2.5F, 3.75F, 4.5F, 17.5F), {Type::Rect});
    roundtrip(RoiBatch({{}, {Roi::circle(2, 3, 1), Roi::rectangle(1, 2, 3, 4, 5)}}), {Type::RoiBatch});
    roundtrip(StringArray(1, Layout::Dense, {StringBuffer({2}, {"中文", "\"\\\n"})}),
              {Type::StringArray, {}, 1});
    roundtrip(StringArray(2, Layout::BatchRagged, {StringBuffer({0}, {}), StringBuffer({1}, {"null"})}),
              {Type::StringArray, {}, 2, Layout::BatchRagged});
    const std::array<std::uint8_t, 2> bytes{0, 255};
    const std::array<std::int32_t, 2> integers{std::numeric_limits<std::int32_t>::min(), 17};
    const std::array<std::int64_t, 2> wide{std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max()};
    const std::array<float, 2> single{1.25F, -2.5F};
    const std::array<double, 2> real{1.25, -2.5};
    const std::array<bool, 2> flags{true, false};
    arrays<std::uint8_t>(DType::UInt8, bytes);
    arrays<std::int32_t>(DType::Int32, integers);
    arrays<std::int64_t>(DType::Int64, wide);
    arrays<float>(DType::Float32, single);
    arrays<double>(DType::Float64, real);
    arrays<bool>(DType::Bool, flags);
    const TypeSpec numeric{Type::NumericArray, DType::Int64, 1};
    for (const auto invalid : {"", "1,2", "1 trailing", "~", "yes", "[1,]", "NaN", "Infinity", ".nan"})
        rejects([&] { ValueJson::parse(invalid, {Type::Float}); }, invalid);
    for (const auto invalid : {"true", "\"17\"", "1.5", "9223372036854775808", "-9223372036854775809"})
        rejects([&] { ValueJson::parse(invalid, {Type::Int}); }, invalid);
    rejects([] { ValueJson::parse("1", {Type::Bool}); });
    rejects([] { ValueJson::parse("false", {Type::String}); });
    rejects([] { ValueJson::decode(QJsonValue(QJsonValue::Undefined), {Type::Int}); });
    rejects([&] { ValueJson::parse("{\"shape\":[2],\"data\":[1]}", numeric); });
    rejects([&] { ValueJson::parse("{\"shape\":[-1],\"data\":[]}", numeric); });
    rejects([&] { ValueJson::parse("{\"shape\":[1],\"data\":[1.5]}", numeric); });
    rejects([&] { ValueJson::parse("{\"shape\":[1],\"data\":[-9223372036854775809]}", numeric); });
    rejects([&] { ValueJson::parse("{\"shape\":[0],\"data\":[],\"other\":0}", numeric); });
    rejects([] { ValueJson::parse("{\"shape\":[1],\"data\":[256]}", {Type::NumericArray, DType::UInt8, 1}); });
    rejects([] { ValueJson::parse("{\"shape\":[1],\"data\":[1]}", {Type::NumericArray, DType::Bool, 1}); });
    rejects([] { ValueJson::parse("{\"shape\":[1],\"data\":[1e100]}", {Type::NumericArray, DType::Float32, 1}); });
    rejects([] { ValueJson::parse("[3,1,2,3]", {Type::Rect}); });
    rejects([] { ValueJson::parse("[1]", {Type::ImageBatch}); });
    rejects([] { ValueJson::decodeType({{"type", "Unknown"}}); });
    rejects([] { ValueJson::decodeType({{"type", "Int"}, {"rank", 1}}); });
    rejects([] { ValueJson::decodeType({{"type", "NumericArray"}, {"dtype", "float64"}, {"rank", 1.5}}); });
    rejects([] { ValueJson::decodeType({{"type", "StringArray"}, {"rank", 1}, {"layout", "unknown"}}); });
    for (const auto number : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                              -std::numeric_limits<double>::infinity()})
    {
        rejects([&] { ValueJson::text(number, {Type::Float}); });
        const auto display = ValueJson::encode(number, {Type::Float}, ValueJson::Purpose::Display).toObject();
        assert(display.value("type") == "Float" && display.value("non_finite").isString());
        rejects([&] { ValueJson::decode(display, {Type::Float}); });
    }
    const std::array<double, 3> mixed{0, std::numeric_limits<double>::infinity(), 2};
    const PortValue mixedArray = NumericArray(DType::Float64, 1, Layout::Dense,
        {NumericBuffer::copy(DType::Float64, {3}, std::as_bytes(std::span(mixed)))});
    rejects([&] { ValueJson::text(mixedArray, {Type::NumericArray, DType::Float64, 1}); });
    const auto data = ValueJson::encode(mixedArray, {Type::NumericArray, DType::Float64, 1},
                                       ValueJson::Purpose::Display).toObject().value("data").toArray();
    assert(data.size() == 3 && data[0].toInteger() == 0 && data[1].toObject().value("type") == "float64"
           && data[1].toObject().value("non_finite") == "Infinity" && data[2].toInteger() == 2);
    const PortValue images = ImageBatch({OwnedImage::take(cv::Mat(2, 3, CV_8UC1, cv::Scalar(7)))});
    rejects([&] { ValueJson::text(images, {Type::ImageBatch}); });
    const auto description = ValueJson::encode(images, {Type::ImageBatch}, ValueJson::Purpose::Display).toObject();
    assert(description.value("type") == "ImageBatch" && description.value("count").toInteger() == 1);
    assert(description.value("images").toArray()[0].toObject().value("rows").toInteger() == 2);
    std::cout << "Qt JSON values preserve types, int64 precision and explicit output descriptions\n";
}
