#include "RuntimeTestSupport.h"
#include "TestPythonEnvironment.h"
#include "core/NodeContracts.h"
#include "core/ProjectStorage.h"
#include "core/ToolPackage.h"
#include "core/ToolValueValidation.h"
#include "plugins/PluginLoader.h"
#include "visionrt/contracts/TrainingArchive.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace visionrt;

namespace {
void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}

std::string u8Str(const std::filesystem::path &path)
{
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}

void verifyDistortedRulerExample(ToolRegistry &registry,
                                 ProjectStorage &storage,
                                 const std::filesystem::path &sourceRoot)
{
    const auto file = sourceRoot / u8"examples/综合示例/畸变标尺校正测量/畸变标尺校正测量.vrtapp";
    auto loaded = storage.load(file);
    for (const auto &diagnostic : loaded.diagnostics)
        std::cerr << diagnostic.field << ": " << diagnostic.message << '\n';
    require(loaded.diagnostics.empty(), "distorted ruler example needs no schema reconciliation");
    require(loaded.project.flows.front().nodes.size() == 5, "distorted ruler example has 5 nodes");

    // 1. Hydrate and retrain CalibrateCamera (node 2).
    const auto &calNode = loaded.project.flows.front().nodes.at(1);
    const auto *calDecl = registry.find(calNode.toolId);
    require(calDecl != nullptr, "CalibrateCamera declaration missing");
    std::string hydrateError;
    auto hydrated = hydrateNodeTrainingSets(calNode, *calDecl, sourceRoot, &hydrateError);
    require(hydrateError.empty(), "hydration of distorted calibration training archive failed");

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temp directory unavailable");
    const auto tempRoot = std::filesystem::path(tempDir.path().toStdWString());
    const auto tempCalZip = tempRoot / "retrained-distorted-cal.zip";
    hydrated[2] = u8Str(tempCalZip);
    hydrated[1] = u8Str(sourceRoot / u8"examples/综合示例/畸变标尺校正测量/assets/calplate_80mm.cpd");

    auto calTrainer = registry.create(calNode.toolId);
    auto calTrained = calTrainer->train(hydrated);
    require(static_cast<bool>(validateToolExecution(calDecl->outputs, calTrained.outputs, calTrained.detail,
                                                    ProducedBy::Train)),
            "distorted CalibrateCamera re-train succeeded");
    require(TrainingArchive::readImages(tempCalZip).size() == 1, "retrained calibration archive preserves image");
    require(std::get<double>(calTrained.outputs.at(5)) < 0.3, "distorted calibration reprojection error < 0.3 px");

    // 2. Run full 5-node pipeline.
    RuntimeEngine engine(2, sourceRoot);
    require(static_cast<bool>(engine.start(loaded.project, registry, RunMode::Once)), "distorted ruler pipeline starts");
    const auto update = testsupport::finish(engine);
    for (NodeId id = 1; id <= 5; ++id)
    {
        const auto snap = testsupport::snapshot(update, id);
        if (!snap->status)
            std::cerr << "node " << id << ": " << snap->status.message << " | " << snap->status.detail << '\n';
        require(static_cast<bool>(snap->status), "distorted ruler pipeline node failed");
    }

    const auto rectSnap = testsupport::snapshot(update, 3);
    const auto &rectImgs = std::get<DataRef<Image>>(rectSnap->outputs.at(2)).get();
    require(rectImgs.size() == 1 && rectImgs.front().cols > 1296 && rectImgs.front().rows > 972,
            "RectifyImage expanded rectified canvas beyond distorted input");
    const auto pixelSizes = std::get<Array>(rectSnap->outputs.at(4)).elements<double>();
    require(pixelSizes.size() == 1 && pixelSizes[0] > 0.045 && pixelSizes[0] < 0.050,
            "RectifyImage pixel size matches ~0.0474 mm/px");
    require(std::get<CoordinateFrame2D>(rectSnap->outputs.at(5)).count(0) == 1,
            "RectifyImage published rectified coordinate frame");

    const auto rowSnap = testsupport::snapshot(update, 4);
    const auto pitches = nestedOf<NestedVector<double, 3>>(std::get<Array>(rowSnap->outputs.at(14))).front().front();
    require(pitches.size() >= 50, "rectified ruler detected >= 50 tick pitches");

    const auto exprSnap = testsupport::snapshot(update, 5);
    const double mean = std::get<double>(exprSnap->outputs.at(3));
    const double stdUm = std::get<double>(exprSnap->outputs.at(4));
    std::cout << "Distorted ruler: pitches=" << pitches.size() << " mean=" << mean << " mm; std=" << stdUm
              << " um; pixelSize=" << pixelSizes[0] << " mm/px\n";
    require(mean >= 0.99 && mean <= 1.01, "distorted ruler mean within 1% of 1 mm");
    require(stdUm < 20.0, "distorted ruler sample std < 20 um");

    // 3. Save-as and reload round-trip.
    const auto savedPath = tempRoot / "distorted_roundtrip.vrtapp";
    storage.save(loaded.project, savedPath);
    const auto reloaded = storage.load(savedPath);
    require(reloaded.diagnostics.empty(), "reloaded distorted ruler example has zero diagnostics");
}

void verifyBentLeadsExample(ToolRegistry &registry,
                            ProjectStorage &storage,
                            const std::filesystem::path &sourceRoot)
{
    const auto file = sourceRoot / u8"examples/综合示例/弯曲引脚检测/弯曲引脚检测.vrtapp";
    auto loaded = storage.load(file);
    for (const auto &diagnostic : loaded.diagnostics)
        std::cerr << diagnostic.field << ": " << diagnostic.message << '\n';
    require(loaded.diagnostics.empty(), "bent leads example needs no schema reconciliation");
    require(loaded.project.flows.front().nodes.size() == 4, "bent leads example has 4 nodes");

    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temp directory unavailable");
    const auto tempRoot = std::filesystem::path(tempDir.path().toStdWString());

    // 1. Hydrate & retrain DetermineAlignmentWithMatching (node 2).
    {
        const auto &alignNode = loaded.project.flows.front().nodes.at(1);
        const auto *alignDecl = registry.find(alignNode.toolId);
        require(alignDecl != nullptr, "DetermineAlignmentWithMatching declaration missing");
        std::string err;
        auto hydrated = hydrateNodeTrainingSets(alignNode, *alignDecl, sourceRoot, &err);
        require(err.empty(), "hydration of alignment-model.zip failed");
        const auto outZip = tempRoot / "retrained-align.zip";
        hydrated[1] = u8Str(outZip);
        auto trainer = registry.create(alignNode.toolId);
        auto trained = trainer->train(hydrated);
        require(static_cast<bool>(validateToolExecution(alignDecl->outputs, trained.outputs, trained.detail,
                                                        ProducedBy::Train)),
                "retrain DetermineAlignmentWithMatching succeeded");
        require(TrainingArchive::readImages(outZip).size() == 1, "alignment zip preserves training image");
    }

    // 2. Hydrate & retrain CheckPresenceWithMatching (node 4, dual datasets).
    {
        const auto &presNode = loaded.project.flows.front().nodes.at(3);
        const auto *presDecl = registry.find(presNode.toolId);
        require(presDecl != nullptr, "CheckPresenceWithMatching declaration missing");
        std::string err;
        auto hydrated = hydrateNodeTrainingSets(presNode, *presDecl, sourceRoot, &err);
        require(err.empty(), "hydration of presence-model.zip failed");
        const auto outZip = tempRoot / "retrained-presence.zip";
        hydrated[1] = u8Str(outZip);
        auto trainer = registry.create(presNode.toolId);
        auto trained = trainer->train(hydrated);
        require(static_cast<bool>(validateToolExecution(presDecl->outputs, trained.outputs, trained.detail,
                                                        ProducedBy::Train)),
                "retrain CheckPresenceWithMatching succeeded");
        require(TrainingArchive::readImages(outZip).size() == 2, "presence zip preserves both datasets' images");
        require(std::get<Point>(trained.outputs.at(9)).get().front().size() == 25,
                "presence training publishes 25 reference points");
    }

    // 3. Execute default directory project (frame 1 = bent_leads_01.png -> OK 25/25).
    {
        RuntimeEngine engine(2, sourceRoot);
        require(static_cast<bool>(engine.start(loaded.project, registry, RunMode::Once)), "bent leads pipeline starts");
        const auto update = testsupport::finish(engine);
        for (NodeId id = 1; id <= 4; ++id)
        {
            const auto snap = testsupport::snapshot(update, id);
            if (!snap->status)
                std::cerr << "bent node " << id << ": " << snap->status.message << " | " << snap->status.detail << '\n';
            require(static_cast<bool>(snap->status), "bent leads pipeline node failed");
        }
        const auto presSnap = testsupport::snapshot(update, 4);
        const bool allOk = nestedOf<NestedVector<bool, 1>>(std::get<Array>(presSnap->outputs.at(4))).front();
        const auto acc = nestedOf<NestedVector<bool, 2>>(std::get<Array>(presSnap->outputs.at(3))).front();
        require(allOk && acc.size() == 25 && std::all_of(acc.begin(), acc.end(), [](bool v) { return v; }),
                "bent_leads_01 passes all 25 pins");
    }

    // 4. Execute across all 9 bent_leads frames and verify OK vs NG verdicts.
    const bool expectedAllOk[9] = {true, false, false, false, true, false, true, true, false};
    for (int i = 1; i <= 9; ++i)
    {
        auto projectCopy = loaded.project;
        projectCopy.flows.front().nodes.at(0).inputs[1].literal
            = std::string("examples/images/弯曲引脚检测/bent_leads_0") + std::to_string(i) + ".png";
        RuntimeEngine engine(2, sourceRoot);
        require(static_cast<bool>(engine.start(projectCopy, registry, RunMode::Once)), "bent leads frame run starts");
        const auto update = testsupport::finish(engine);
        for (NodeId id = 1; id <= 4; ++id)
            require(static_cast<bool>(testsupport::snapshot(update, id)->status), "bent leads frame node succeeded");
        const auto presSnap = testsupport::snapshot(update, 4);
        const bool allOk = nestedOf<NestedVector<bool, 1>>(std::get<Array>(presSnap->outputs.at(4))).front();
        const auto acc = nestedOf<NestedVector<bool, 2>>(std::get<Array>(presSnap->outputs.at(3))).front();
        const auto dists = nestedOf<NestedVector<double, 2>>(std::get<Array>(presSnap->outputs.at(5))).front();
        const auto okCount = std::count(acc.begin(), acc.end(), true);
        std::cout << "bent_leads_0" << i << ": accepted=" << okCount << "/25 allOk=" << (allOk ? "true" : "false") << '\n';
        require(allOk == expectedAllOk[i - 1], "bent leads frame OK/NG verdict matches ground truth");
        if (i == 3)
            require(okCount == 24 && !acc[15] && dists.size() == 24, "bent_leads_03 rejects pin 15 in ignore mode");
    }

    // 5. Save-as and reload round-trip.
    const auto savedPath = tempRoot / "bent_roundtrip.vrtapp";
    storage.save(loaded.project, savedPath);
    const auto reloaded = storage.load(savedPath);
    require(reloaded.diagnostics.empty(), "reloaded bent leads example has zero diagnostics");
}

void verifyCableImagesComplete(const std::filesystem::path &sourceRoot)
{
    const auto dir = sourceRoot / u8"examples/images/识别线缆颜色";
    for (int i = 1; i <= 11; ++i)
    {
        const std::string name = (i < 10 ? "cable_0" : "cable_") + std::to_string(i) + ".png";
        require(std::filesystem::is_regular_file(dir / name), "missing cable example image");
    }
}
} // namespace

int main(int argc, char **argv)
try
{
    QCoreApplication application(argc, argv);
    PythonRuntime python(testsupport::pythonEnvironment());
    auto loader = std::make_shared<PluginLoader>();
    ToolRegistry registry;
    registry.setNativeProvider(loader);
    registry.registerPackage(readToolsPackage(std::filesystem::path(u8"" VISIONRT_TEST_TOOLS_PACKAGE)));
    registry.loadNativeImplementations();
    testsupport::setupPython(registry, python);
    ProjectStorage storage(registry);
    const auto sourceRoot = std::filesystem::path(u8"" VISIONRT_SOURCE_DIR);

    verifyDistortedRulerExample(registry, storage, sourceRoot);
    verifyBentLeadsExample(registry, storage, sourceRoot);
    verifyCableImagesComplete(sourceRoot);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
