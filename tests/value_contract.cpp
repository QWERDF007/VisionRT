#ifdef NDEBUG
#    undef NDEBUG
#endif

#include <visionrt/contracts/Value.h>

#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

using namespace visionrt;

namespace {
template<class Action>
void rejects(Action action)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::exception &)
    {
        rejected = true;
    }
    assert((rejected));
}
}

int main()
{
    const TypeSpec scoresType{Type::Array, DType::Float32, 2};
    assert((!compatible(scoresType, {Type::Array, DType::Float32, 3})));
    assert((!compatible(scoresType, {Type::Array, DType::Int64, 2})));
    assert((!compatible({Type::Int}, {Type::Bool})));
    rejects([] { validateTypeSpec({Type::Array}); });
    rejects([] { validateTypeSpec({Type::Array, DType::String, 0}); });
    rejects([] { validateTypeSpec({Type::Array, DType::Bool, 5}); });
    validateValue(scoresType, Missing{});
    const Array scores(NestedVector<float, 2>{
        {},
        {0.8F, 0.95F, 0.7F}
    });
    validateValue(scoresType, scores);
    assert((scores.get<NestedVector<float, 2>>().size() == 2));
    assert((scores.get<NestedVector<float, 2>>()[0].empty()));
    assert((scores.get<NestedVector<float, 2>>()[1][1] == 0.95F));
    const Array names(NestedVector<std::string, 2>{
        {},
        {"one", "two", "one"}
    });
    assert((names.get<NestedVector<std::string, 2>>()[0].empty()));
    assert((names.get<NestedVector<std::string, 2>>()[1][1] == "two"));
    for (const auto &type : Array::supportedTypes())
    {
        const auto empty = Array::empty(*type.dtype, type.rank);
        assert((typeOf(empty) == type));
        validateValue(type, empty);
        assert((!isMissing(empty)));
    }
    assert((!isMissing(DataRef<Image>(Image{}))));
    validateValue({Type::Image}, DataRef<Image>(Image{}));
    rejects([&] { validateValue({Type::Int}, true); });
    rejects([&] { validateValue({Type::Array, DType::Float64, 2}, scores); });
    rejects([&] { (void)scores.get<NestedVector<double, 2>>(); });
    const auto scoreAlias                          = scores;
    scoreAlias.get<NestedVector<float, 2>>()[1][1] = 0.5F;
    assert((scores.get<NestedVector<float, 2>>()[1][1] == 0.5F));
    assert((equalValues(scores, Array(NestedVector<float, 2>{
                                    {},
                                    {0.8F, 0.5F, 0.7F}
    }))));
    assert((!equalValues(scores, names)));
    assert((equalValues(std::string("same"), std::string("same"))));
    assert((!equalValues(std::int64_t{1}, true)));

    const std::vector<double> rectangle{2, 10, 20, 8, 4, 90};
    const auto                corners = rectangleCorners(rectangle);
    assert((std::abs(corners[0].x - 16) < 0.0001));
    assert((std::abs(corners[0].y - 18) < 0.0001));
    assert((std::abs(corners[2].x - 12) < 0.0001));
    assert((std::abs(corners[2].y - 26) < 0.0001));
    for (const auto &record : std::vector<std::vector<double>>{
             {0, 1, 2},
             {1, -1, 2, 3, 4},
             rectangle,
             {3, 2, 3, 4},
             {4, 0, 0, 2, 0, 1, 1},
             {5, 1, 2}
    })
        validateRoi(record);
    for (const auto &record : std::vector<std::vector<double>>{
             {},
             {0, 1},
             {1, 0, 0},
             {2, 0, 0, 0, 1, 0},
             {2, 0, 0, 1, -1, 0},
             {3, 0, 0, 0},
             {4, 0, 0, 1, 1},
             {5},
             {5, 1, 2, 3},
             {1.5, 0, 0, 1, 1},
             {6, 0, 0},
             {3, 0, 0, std::numeric_limits<double>::infinity()}
    })
        rejects([&] { validateRoi(record); });
    const DataRef<ROI> detections(ROI{
        {},
        {{2, 0, 0, 10, 10, 0}, {2, 15, 0, 4, 4, 0}}
    });
    assert((detections.get()[0].empty()));
    validateValue({Type::ROI}, detections);
    const auto roiAlias     = detections;
    roiAlias.get()[1][1][3] = 8;
    assert((detections.get()[1][1][3] == 8));
    assert((equalValues(detections, DataRef<ROI>(detections.get()))));

    cv::Mat              source(4, 5, CV_16UC1, cv::Scalar(123));
    const DataRef<Image> image(Image{source(cv::Rect(1, 1, 2, 2))});
    assert((image.get()[0].step == source.step));
    assert((!image.get()[0].isContinuous()));
    assert((image.get()[0].data == reinterpret_cast<std::uint8_t *>(source.ptr<std::uint16_t>(1) + 1)));
    source.setTo(7);
    assert((image.get()[0].at<std::uint16_t>(1, 1) == 7));
    image.get()[0].setTo(19);
    assert((source.at<std::uint16_t>(1, 1) == 19));
    const auto imageAlias = image;
    assert((imageAlias.owner() == image.owner()));
    assert((imageAlias.get()[0].data == image.get()[0].data));
    auto                external = std::make_shared<std::vector<std::uint8_t>>(2, 31);
    std::weak_ptr<void> lifetime = external;
    {
        const DataRef<Image> pinned(Image{cv::Mat(1, 2, CV_8UC1, external->data())}, {external});
        external.reset();
        assert((!lifetime.expired()));
        pinned.get()[0].at<std::uint8_t>(0, 1) = 99;
        assert((pinned.get()[0].at<std::uint8_t>(0, 1) == 99));
    }
    assert((lifetime.expired()));
}
