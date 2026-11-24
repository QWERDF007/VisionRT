// The workbench and the tool-side trainer are two halves of one contract. The declaration names the
// trainer, the record fields and the model target; the host turns a draft into a typed TrainingSet;
// the trainer turns that set into the bytes RecognizeColor decodes. This contract runs both halves
// on the real recognition module instead of a probe, so a change on either side is caught before a
// user meets it: field ids that no longer match, a trainer port that moved, or a model the node
// cannot read all fail here.
#include "ToolResultAssertions.h"
#include "core/DefinitionJson.h"
#include "core/ToolPackage.h"
#include "core/ToolRegistry.h"
#include "plugins/PluginLoader.h"
#include "ui/TrainingSession.h"

#include <opencv2/imgproc.hpp>

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace visionrt;

namespace {
void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

const PortDefinition *port(const std::vector<PortDefinition> &ports, PortId id)
{
    for (const auto &entry : ports)
        if (entry.id == id)
            return &entry;
    return nullptr;
}

// One patch per declared class on one frame: the frame a user would add four times, which the draft
// stores once and every record names by the same image number.
struct Patch
{
    const char *label;
    int         left;
    cv::Scalar  color;
};

const std::array<Patch, 4> kPatches{
    {
     {"purple", 20, {128, 0, 128}},
     {"red", 120, {0, 0, 255}},
     {"blue", 220, {255, 0, 0}},
     {"yellow", 320, {0, 255, 255}},
     }
};

cv::Mat patchFrame()
{
    cv::Mat frame(400, 400, CV_8UC3, cv::Scalar{64, 64, 64});
    for (const auto &patch : kPatches) cv::rectangle(frame, cv::Rect{patch.left, 20, 60, 60}, patch.color, cv::FILLED);
    return frame;
}

std::unique_ptr<IToolInstance> createTrainer(ToolRegistry &registry)
{
    auto trainer = registry.create("RecognizeColor");
    require(trainer != nullptr, "the color business instance must support training before initialization");
    return trainer;
}

// One draft record per class, all pointing at the same frame. This is what the training page holds
// after a user adds one picture and names four regions on it.
void fillDraft(TrainingSession &session)
{
    const auto frame = patchFrame();
    for (const auto &patch : kPatches)
    {
        const auto index = session.addImage(frame, ColorOrder::BGR, QStringLiteral("inputs:1"));
        require(index.has_value(), "the draft refused the published frame");
        require(session.setField(*index, 1, std::string(patch.label)),
                "the draft refused the colour name for " + std::string(patch.label));
        require(session.setRegions(*index, {makeRectangle(patch.left + 5, 25, 50, 50, 0.0)}),
                "the draft refused the sampling region for " + std::string(patch.label));
    }
    require(session.images().size() == 1, "four records added from one frame must share one stored frame");
    require(session.records().size() == kPatches.size(), "every added record must survive");
    require(session.savable(), "a named record with a region is an applicable draft");
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    try
    {
        const auto package = std::filesystem::path(u8"" VISIONRT_DEV_TOOLS_PACKAGE);
        require(std::filesystem::is_directory(package),
                "the packaged tools are missing: " + package.string() + " (run package_tools.py)");
        const auto module = package / "bin" / "vst_recognition.dll";
        require(std::filesystem::is_regular_file(module), "the recognition module is missing from the package");

        const auto color   = parseToolDefinitionFile(package / u8"tools/builtin/处理/识别/识别颜色.json");

        // Only the recognition module is installed here: the workbench half of this contract never
        // runs a pipeline, and loading the inference runtime would need the GPU package to exist.
        QTemporaryDir installation;
        require(installation.isValid(), "temporary installation is unavailable");
        const auto libraryDirectory = std::filesystem::path(installation.path().toStdU16String()) / "bin";
        std::filesystem::create_directories(libraryDirectory);
        std::filesystem::copy_file(module, libraryDirectory / module.filename());

        ToolsPackage packaged;
        packaged.root = std::filesystem::path(installation.path().toStdU16String());
        packaged.tools.push_back({color, libraryDirectory / module.filename()});

        ToolRegistry registry;
        registry.setNativeProvider(std::make_shared<PluginLoader>());
        registry.registerPackage(packaged);
        registry.loadNativeImplementations();
        const auto status = registry.availability("RecognizeColor");
        require(status.state == ToolAvailability::Available,
                "the trainer factory did not register: " + status.status.message);

        TrainingSession        session;
        TrainingSession::Begin begin;
        begin.node  = 1;
        begin.spec  = *color.training;
        begin.trainParams = color.trainParams;
        begin.asset = std::filesystem::path(installation.path().toStdU16String()) / "colors.json";
        session.begin(begin);
        fillDraft(session);

        // An empty draft is refused by the trainer, not silently published as an empty model.
        {
            TrainingSession empty;
            auto            blank = begin;
            blank.spec            = *color.training;
            empty.begin(blank);
            auto   trainerTool = createTrainer(registry);
            Values inputs;
            inputs.emplace(3, std::string{});
            const auto result = trainerTool->train(empty.trainingSet().get(), inputs);
            require(!testview::status(result), "the trainer must refuse a draft without records");
            require(!testview::status(result).message.empty(), "a refused draft needs a reason");
            require(trainerTool->release().code == 0, "the trainer must release after a refusal");
        }

        // The trainer reads the draft the host would hand it and answers with the model the node
        // loads. Both field numbers travel as declared values, never as assumptions about order.
        auto   trainerTool = createTrainer(registry);
        Values inputs;
        inputs.emplace(3, std::string{});
        const auto trained = trainerTool->train(session.trainingSet().get(), inputs);
        require(testview::status(trained).code == 0,
                "the real trainer refused the draft: " + testview::status(trained).message);
        require(trainerTool->release().code == 0, "the trainer must release after training");

        const auto &model = trained.artifacts.at("color-model").bytes;
        const auto modelFile = packaged.root / "trained-color.bin";
        QFile      file(QString::fromStdWString(modelFile.wstring()));
        require(file.open(QIODevice::WriteOnly), "model file must be writable");
        require(file.write(reinterpret_cast<const char *>(model.data()), static_cast<qint64>(model.size()))
                    == static_cast<qint64>(model.size()),
                "model bytes must be written completely");
        file.close();
        auto consumer = registry.create(color);
        require(consumer != nullptr, "color consumer is available");
        require(consumer->initialize(ToolConfiguration{{}, {{"color-model", modelFile}}})
                        .code
                    == 0,
                "recognition loads the trained model");
        RegionGroup regions;
        for (const auto &patch : kPatches) regions.push_back(makeCircle({patch.left + 30.0, 50}, 15));
        const Values detection{
            {1, DataRef<Image>(Image{patchFrame()})},
            {2, Array::empty(DType::Float64, 3)},
            {3, DataRef<ROI>(ROI{regions})},
            {4, 0.05},
            {5, 0.5}
        };
        const auto classified = consumer->execute(detection);
        require(testview::status(classified).code == 0, "real recognition accepts the published model");
        const auto labels = nestedOf<NestedVector<std::string, 2>>(std::get<Array>(classified.outputs.at(4)));
        require(labels.size() == 1 && labels.front().size() == kPatches.size(), "one classified label per station");
        for (size_t index = 0; index < kPatches.size(); ++index)
            require(labels.front()[index] == kPatches[index].label, "trained class is recognized at its station");
        require(consumer->release().code == 0, "recognition releases");
        consumer = registry.create(color);
        require(consumer->initialize(ToolConfiguration{{}, {{"color-model", modelFile}}})
                        .code
                    == 0,
                "recognition reopens the same published model");
        const auto reopened = consumer->execute(detection);
        for (const PortId resultPort : {4U, 5U, 6U})
            require(equalValues(classified.outputs.at(resultPort), reopened.outputs.at(resultPort)),
                    "labels, float32 scores and ratios survive reopening");
        require(consumer->release().code == 0, "recognition releases after reopening");

        std::cout << "the workbench draft reaches the real colour trainer and comes back as a model the node decodes\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
