#ifdef NDEBUG
#undef NDEBUG
#endif

#include "common/Value.h"

#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

using namespace visionrt;

namespace {
template<class F> void rejects(F&& operation) {
    bool rejected = false;
    try { operation(); } catch (const std::exception&) { rejected = true; }
    assert(rejected);
}

template<class T, std::size_t N>
NumericBuffer numeric(DType dtype, std::vector<std::int64_t> shape,
                      const std::array<T, N>& values) {
    return NumericBuffer::copy(dtype, std::move(shape), std::as_bytes(std::span(values)));
}
}

int main() {
    const TypeSpec scoresType{Type::NumericArray, DType::Float32, 2, Layout::BatchRagged};
    assert(!compatible(scoresType, {Type::NumericArray, DType::Float32, 2, Layout::Dense}));
    assert(!compatible(scoresType, {Type::NumericArray, DType::Float32, 3, Layout::BatchRagged}));
    assert(!compatible(scoresType, {Type::NumericArray, DType::Int64, 2, Layout::BatchRagged}));
    assert(!compatible({Type::Int}, {Type::Bool}));
    rejects([] { validateTypeSpec({Type::NumericArray}); });
    rejects([] { validateTypeSpec({Type::StringArray, {}, 1, Layout::BatchRagged}); });
    validateValue(scoresType, Missing{});
    const NumericArray scores(DType::Float32, 2, Layout::BatchRagged, {
        numeric(DType::Float32, {0}, std::array<float, 0>{}),
        numeric(DType::Float32, {3}, std::array{0.8F, 0.95F, 0.7F})});
    validateValue(scoresType, scores);
    assert(scores.buffers().size() == 2);
    assert(scores.buffers()[0].shape()[0] == 0);
    assert(scores.buffers()[1].values<float>()[1] == 0.95F);
    const StringArray names(2, Layout::BatchRagged, {
        StringBuffer({0}, {}), StringBuffer({3}, {"one", "two", "one"})});
    assert(names.buffers()[0].values().empty());
    assert(names.buffers()[1].values()[1] == "two");
    const NumericArray empty(DType::Float32, 2, Layout::BatchRagged, {});
    assert(!isMissing(PortValue{empty}));
    assert(!isMissing(PortValue{ImageBatch{}}));
    validateValue({Type::ImageBatch}, ImageBatch{});
    rejects([&] { validateValue({Type::Int}, PortValue{true}); });
    rejects([&] { NumericArray(DType::Float64, 2, Layout::BatchRagged,
                               {scores.buffers()[1]}); });
    rejects([] { numeric(DType::Int32, {2}, std::array<std::int32_t, 1>{7}); });
    rejects([] { checkedElementCount(std::array<std::int64_t, 2>{-1, 0}); });
    rejects([] { checkedElementCount(std::array<std::int64_t, 2>{
        std::numeric_limits<std::int64_t>::max(), 3}); });
    rejects([] { checkedByteCount(std::array<std::int64_t, 1>{
        std::numeric_limits<std::int64_t>::max()}, sizeof(double)); });
    assert(checkedElementCount(std::array<std::int64_t, 3>{
        std::numeric_limits<std::int64_t>::max(), 3, 0}) == 0);
    const auto scalar = numeric(DType::Int64, {}, std::array<std::int64_t, 1>{42});
    assert(scalar.values<std::int64_t>()[0] == 42);
    rejects([&] { scalar.values<double>(); });
    rejects([] { StringBuffer({2}, {"one"}); });
    rejects([] { Utf8(std::string("\xc0\x80", 2)); });
    rejects([] { numeric(DType::Bool, {1}, std::array<std::uint8_t, 1>{2}); });

    const auto rectangle = Roi::rectangle(10, 20, 8, 4, 90);
    const auto corners = rectangle.rectangleCorners();
    assert(std::abs(corners[0].x - 16) < 0.0001);
    assert(std::abs(corners[0].y - 18) < 0.0001);
    assert(std::abs(corners[2].x - 12) < 0.0001);
    assert(std::abs(corners[2].y - 26) < 0.0001);
    const auto line = Roi::segment({-1, 2}, {3, 4});
    assert(line.encoded()[0] == 1 && line.encoded()[1] == -1);
    assert(Roi::circle(2, 3, 4).encoded()[3] == 4);
    const std::array polygon{cv::Point2f{0, 0}, cv::Point2f{2, 0}, cv::Point2f{1, 1}};
    assert(Roi::polygon(polygon).encoded()[0] == 4);
    assert(Roi::points(std::span(polygon).first(1)).encoded()[0] == 5);
    for (const auto& data : std::vector<std::vector<float>>{
             {}, {1, 0, 0}, {2, 0, 0, 0, 1, 0}, {2, 0, 0, 1, -1, 0},
             {3, 0, 0, 0}, {4, 0, 0, 1, 1}, {5}, {5, 1, 2, 3},
             {1.5F, 0, 0, 1, 1}, {6, 0, 0},
             {3, 0, 0, std::numeric_limits<float>::infinity()}}) {
        rejects([&] { Roi::decode(data); });
    }
    const RoiBatch detections({{}, {Roi::rectangle(0, 0, 10, 10, 0),
        Roi::rectangle(15, 0, 4, 4, 0), Roi::rectangle(25, 0, 8, 8, 0)}});
    assert(detections.images()[0].empty());
    assert(detections.images()[1][2].encoded()[3] == 8);

    OwnedImage image;
    {
        cv::Mat source(4, 5, CV_16UC1, cv::Scalar(123));
        image = OwnedImage::copyOf(source);
        source.setTo(999);
    }
    assert(image.readOnlyMat().at<std::uint16_t>(0, 0) == 123);
    const auto cropped = image.region({1, 1, 2, 2});
    assert(cropped.rowStride() == image.rowStride());
    assert(!cropped.readOnlyMat().isContinuous());
    auto retainedImage = image;
    std::weak_ptr<const void> weakImage = image.owner();
    image = {};
    assert(!weakImage.expired());
    retainedImage = {};
    assert(weakImage.expired());
    assert(cropped.readOnlyMat().at<std::uint16_t>(1, 1) == 123);
    auto editable = cropped.clone();
    editable.setTo(7);
    assert(cropped.readOnlyMat().at<std::uint16_t>(0, 0) == 123);
    cv::Mat aliased(1, 1, CV_8UC1, cv::Scalar(21));
    cv::Mat alias = aliased;
    const auto published = OwnedImage::take(std::move(aliased));
    alias.setTo(99);
    assert(published.readOnlyMat().at<std::uint8_t>(0, 0) == 21);
    OwnedImage externalImage;
    {
        std::array<std::uint8_t, 2> external{31, 32};
        cv::Mat header(1, 2, CV_8UC1, external.data());
        externalImage = OwnedImage::take(std::move(header));
        external[1] = 99;
    }
    assert(externalImage.readOnlyMat().at<std::uint8_t>(0, 1) == 32);
    std::weak_ptr<const void> numericLifetime;
    {
        auto retained = scalar;
        numericLifetime = retained.owner();
        assert(retained.data() == scalar.data());
    }
    assert(!numericLifetime.expired());
    {
        auto temporary = numeric(DType::Int32, {1}, std::array<std::int32_t, 1>{7});
        numericLifetime = temporary.owner();
    }
    assert(numericLifetime.expired());
    const Utf8 text("共享 owner");
    const Utf8 textAlias = text;
    assert(text.owner() == textAlias.owner());
    const auto roiAlias = rectangle;
    assert(roiAlias.encoded().data() == rectangle.encoded().data());
    assert(equalValues(Utf8("same"), Utf8("same")));
    assert(!equalValues(PortValue{std::int64_t{1}}, PortValue{true}));
    const NumericArray sameScores(DType::Float32, 2, Layout::BatchRagged, {
        numeric(DType::Float32, {0}, std::array<float, 0>{}),
        numeric(DType::Float32, {3}, std::array{0.8F, 0.95F, 0.7F})});
    assert(equalValues(scores, sameScores));
    const NumericArray changedScores(DType::Float32, 2, Layout::BatchRagged, {
        numeric(DType::Float32, {0}, std::array<float, 0>{}),
        numeric(DType::Float32, {3}, std::array{0.8F, 0.96F, 0.7F})});
    assert(!equalValues(scores, changedScores));
    const StringArray changedNames(2, Layout::BatchRagged, {
        StringBuffer({0}, {}), StringBuffer({3}, {"one", "three", "one"})});
    assert(!equalValues(names, changedNames));
    assert(equalValues(RoiBatch({{Roi::circle(2, 3, 4)}}),
                       RoiBatch({{Roi::circle(2, 3, 4)}})));
    assert(!equalValues(RoiBatch({{Roi::circle(2, 3, 4)}}),
                        RoiBatch({{Roi::circle(2, 3, 5)}})));
    assert(equalValues(ImageBatch({cropped}), ImageBatch({cropped})));
    assert(!equalValues(ImageBatch({cropped}), ImageBatch({OwnedImage::copyOf(cropped.readOnlyMat())})));

    for (const std::string literal : {"9007199254740993", "9223372036854775807",
                                      "-9223372036854775808", "0", "-1"}) {
        assert(formatInt64(parseInt64(literal)) == literal);
    }
    assert(parseInt64("9007199254740993") == 9007199254740993LL);
    assert(parseInt64("-9223372036854775808") == std::numeric_limits<std::int64_t>::min());
    for (const auto* bad : {"", "+1", " 1", "1 ", "1.0", "1e2", "--1",
                            "9223372036854775808", "-9223372036854775809"}) {
        rejects([&] { parseInt64(bad); });
    }
}
