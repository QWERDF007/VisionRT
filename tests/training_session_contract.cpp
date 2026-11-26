#include "ui/TrainingSession.h"
#include "visionrt/contracts/ValueJson.h"

#include <QDir>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

TrainingSession::Begin declaration()
{
    TrainingSession::Begin start;
    start.node = 1;
    start.trainParams = {{0, "说明", TypeSpec{Type::String}, std::string{}},
                         {1, "样本", TypeSpec{Type::TrainingSet}, DataRef<TrainingSet>(TrainingSet{})},
                         {2, "另一组", TypeSpec{Type::TrainingSet}, DataRef<TrainingSet>(TrainingSet{})},
                         {3, "方向", TypeSpec{Type::Direction}, Direction{}}};
    PortDefinition first{0, "区域", TypeSpec{Type::Circle}, Circle{}};
    PortDefinition second{4, "另一区域", TypeSpec{Type::Line}, Line{}};
    start.spec.datasets = {{1, PortRef{PortStage::Inputs, 1}, 0, {first, second}},
                           {2, std::nullopt, 1, {{0, "数量", TypeSpec{Type::Int}, std::int64_t{0}}}}};
    return start;
}
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir temporary;
    try
    {
        require(temporary.isValid(), "temporary directory");
        TrainingSession session;
        session.begin(declaration());
        require(session.savable(), "empty datasets and empty strings are valid generic values");
        require(!session.roiParameter(), "ROI editing requires an explicit field selection");
        require(session.selectRoiParameter(0), "zero is a valid ROI field number");
        cv::Mat frame(6, 7, CV_16UC3, cv::Scalar(1000, 2000, 3000));
        require(session.addRecord(frame, ColorOrder::BGR, "frame-1").has_value(), "add original frame");
        frame.at<cv::Vec3w>(0, 0)[0] = 0;
        require(session.images().front().pixels.at<cv::Vec3w>(0, 0)[0] == 1000, "draft owns copied original pixels");
        require(session.images().front().pixels.type() == CV_16UC3
                    && session.images().front().colorOrder == ColorOrder::BGR, "depth and colour order preserved");
        require(!session.images().front().thumbnailImage.isNull(), "thumbnail derives from original frame");
        require(session.setRegions(0, {makeCircle({3, 4}, 2)}), "circle is a valid ROI field value");
        require(session.selectRoiParameter(4), "second ROI field is independently selectable");
        require(session.setRegions(0, {makeSegment({1, 2}, {3, 4})}), "segment is a valid second field");
        require(session.addRecord(frame, ColorOrder::BGR, "frame-1").has_value(), "reuse published source");
        require(session.images().size() == 1 && session.records().size() == 2, "records share one original frame");
        require(session.records()[0].image == session.records()[1].image, "image identities shared");
        require(session.records()[0].number != session.records()[1].number, "record identities distinct");
        const auto first = session.trainingSet();
        require(std::get<Circle>(first.get().records.begin()->second.fields.at(0)).get().front().size() == 1,
                "first ROI remains independently stored");
        require(session.selectDataset(2), "multiple dataset parameters are independent");
        require(session.records().empty() && !session.roiParameter(), "another dataset has no implicit canvas ROI");
        const auto blank = session.addRecord();
        require(blank && !session.records().front().image && session.images().empty(), "records can be edited without a source image");
        require(session.setField(*blank, 0, std::int64_t{7}), "an imageless record exposes ordinary fields");
        TrainingSet numeric;
        numeric.records.emplace(9, TrainingRecord{9, std::nullopt, {{0, std::int64_t{12}}}});
        require(session.setParameter(2, DataRef<TrainingSet>(numeric)), "accept imageless records");
        require(session.records().size() == 1 && !session.records().front().image, "record does not invent an image");
        require(session.setField(0, 0, std::int64_t{15}), "typed record field editable");
        const auto direction = Direction::fromNested({{{10, 20, 30, 90}}});
        require(session.setParameter(3, direction), "train parameter uses ordinary value types");
        const auto parameters = session.parameters();
        require(std::get<Direction>(parameters.at(3)) == direction, "direction reaches train values");
        require(std::get<DataRef<TrainingSet>>(parameters.at(1)).get().records.size() == 2, "first dataset reaches its number");
        require(std::get<std::int64_t>(std::get<DataRef<TrainingSet>>(parameters.at(2)).get().records.at(9).fields.at(0)) == 15,
                "second dataset reaches its number");
        require(QDir(temporary.path()).entryList(QDir::Files).empty(), "editing writes no files");
        auto restored = declaration();
        for (const auto &[id, value] : session.parameters())
            restored.parameters[id] = readValueJson(writeValueJson(value, *typeOf(value)), *typeOf(value));
        TrainingSession reopened;
        reopened.begin(restored);
        require(reopened.error().isEmpty() && reopened.savable(), "multiple datasets reload from persisted values");
        require(reopened.records().size() == 2 && reopened.images().size() == 1, "shared frame reloads once");
        require(reopened.selectRoiParameter(0) && reopened.regions(0).size() == 1, "circle field reloads");
        require(reopened.selectDataset(2) && reopened.records().size() == 1 && !reopened.records()[0].image,
                "imageless dataset reloads");
        require(std::get<std::int64_t>(*reopened.value(0, 0)) == 15, "record number and type persist");
        require(QDir(temporary.path()).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).empty(),
                "training values round trip without external storage");
        TrainingSession valuesOnly;
        TrainingSession::Begin simple;
        simple.node = 2;
        simple.trainParams = {{0, "参数", TypeSpec{Type::Float}, 1.25}};
        valuesOnly.begin(simple);
        require(valuesOnly.savable() && valuesOnly.records().empty(), "parameter-only training needs no image or ROI");
        require(std::get<double>(valuesOnly.parameters().at(0)) == 1.25, "zero parameter reaches training");
        auto bounded = declaration();
        bounded.spec.datasets[0].fields[0].minRegions = 2;
        bounded.spec.datasets[0].fields[0].maxRegions = 2;
        TrainingSession boundedSession;
        boundedSession.begin(bounded);
        boundedSession.addRecord(frame, ColorOrder::BGR, "frame");
        boundedSession.selectRoiParameter(0);
        require(boundedSession.setRegions(0, {makeCircle({1, 2}, 1)}), "draft permits drawing one region at a time");
        require(!boundedSession.savable(), "declared minimum applies to the complete training value");
        require(boundedSession.setRegions(0, {makeCircle({1, 2}, 1), makeCircle({3, 4}, 1)}), "multiple circles share the same typed field");
        require(boundedSession.savable(), "complete regions satisfy the declared count");
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
