#include "core/ProjectStorage.h"
#include "RuntimeTestSupport.h"
#include "ToolResultAssertions.h"
#include "core/ToolRegistry.h"
#include "plugins/PluginLoader.h"
#include "python/PythonRuntime.h"
#include "TestPythonEnvironment.h"
#include "runtime/Receiver.h"
#include "runtime/RuntimeEngine.h"

#include <opencv2/core.hpp>
#include <core/ToolPackage.h>

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QTemporaryDir>
#include <cassert>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>

using namespace visionrt;
using namespace std::chrono_literals;

namespace {

using namespace testsupport;

std::filesystem::path resolveToolsPackage(const std::filesystem::path &installRoot)
{
    if (installRoot.empty() || !std::filesystem::is_regular_file(installRoot / "module_index.json"))
        throw std::runtime_error("--install-root must name an installed tools package");
    readToolsPackage(installRoot);
    return std::filesystem::canonical(installRoot);
}

void testRealToolsCpuPythonPipeline(const std::filesystem::path &toolsPkg, const std::filesystem::path &repoRoot,
                                    PythonRuntime &python)
{
    QTemporaryDir tmpDir;
    assert((tmpDir.isValid()));
    const auto tmpPath         = std::filesystem::u8path(tmpDir.path().toUtf8().toStdString());
    const auto projectPath     = std::filesystem::path(u8"04_扩展示例") / u8"01_Python图像统计";
    const auto tempProjectFile = tmpPath / u8"Python图像统计.vrt.json";
    std::filesystem::copy_file(repoRoot / "examples" / projectPath / u8"Python图像统计.vrt.json", tempProjectFile);
    std::filesystem::copy(repoRoot / "examples" / projectPath / "images", tmpPath / "images",
                          std::filesystem::copy_options::recursive);

    const auto package = visionrt::readToolsPackage(toolsPkg);
    auto       loader  = std::make_shared<PluginLoader>();
    ToolRegistry registry;
    registry.setPluginLoader(loader);
    registry.registerPackage(package);
    registry.loadNativeImplementations();
    setupPython(registry, python);

    ProjectStorage storage(registry);
    auto           loaded = storage.load(tempProjectFile);
    for (const auto &d : loaded.diagnostics)
    {
        std::cerr << "LOAD DIAGNOSTIC: node=" << (d.node ? *d.node : 0) << " code=" << static_cast<int>(d.code)
                  << " field=" << d.field << " msg=" << d.message << std::endl;
    }
    assert((loaded.diagnostics.empty()));
    assert((loaded.project.flows.size() == 1));
    assert((loaded.project.flows[0].nodes.size() == 5));

    RuntimeEngine engine(4);
    assert((engine.start(loaded.project, registry, RunMode::Once).code == 0));
    auto update = finish(engine);

    auto s1 = snapshot(update, 1);
    assert((s1 && s1->status.code == 0));
    const auto &images1 = std::get<DataRef<Image>>(s1->outputs.at(2));
    assert((images1.get().size() >= 2));

    auto s2 = snapshot(update, 2);
    assert((s2 && s2->status.code == 0));
    const auto &rois2 = std::get<DataRef<ROI>>(s2->outputs.at(3));
    assert((rois2.get().size() == images1.get().size()));

    auto s3 = snapshot(update, 3);
    assert((s3 && s3->status.code == 0));
    const auto &croppedImages = std::get<DataRef<Image>>(s3->outputs.at(2));
    assert((croppedImages.get().size() == images1.get().size()));
    for (const auto &image : croppedImages.get()) assert((image.rows == 40 && image.cols == 20));
    assert((s3->outputs.contains(2) && s3->outputs.contains(3) && !s3->outputs.contains(4)));

    auto s4 = snapshot(update, 4);
    assert((s4 && s4->status.code == 0));
    const auto &preprocessed = std::get<DataRef<Image>>(s4->outputs.at(2));
    assert((preprocessed.get().size() == croppedImages.get().size()));

    // Node 5: Python mean over node 1's original images, not over the cropped and binarised branch.
    // The fixture keeps the two paths distinguishable: nodes 3/4 would yield 95.625 and 191.25.
    auto s5 = snapshot(update, 5);
    assert((s5 && s5->status.code == 0));
    const auto &means = std::get<Array>(s5->outputs.at(2));
    assert(((means.type() == TypeSpec{Type::Array, DType::Float64, 1})));
    assert((nestedOf<std::vector<double>>(means).size() == static_cast<std::int64_t>(images1.get().size())));
    const auto values = nestedOf<std::vector<double>>(means);
    assert((values.size() == 2));
    assert((std::abs(values[0] - 51.0) < 0.5));
    assert((std::abs(values[1] - 22.95) < 0.5));

    // Test Save As / Reopen consistency & re-execution output equality
    const auto saveTarget = std::filesystem::u8path(tmpDir.path().toUtf8().toStdString()) / "reopened.vrt.json";
    storage.save(loaded.project, saveTarget);
    auto reloaded = storage.load(saveTarget);
    assert((reloaded.diagnostics.empty()));
    assert((reloaded.project.flows[0].nodes.size() == 5));
    for (size_t i = 0; i < 5; ++i)
    {
        assert((reloaded.project.flows[0].nodes[i].toolId == loaded.project.flows[0].nodes[i].toolId));
    }
    assert((engine.start(reloaded.project, registry, RunMode::Once).code == 0));
    auto reloadedUpdate = finish(engine);
    auto reloadedS5     = snapshot(reloadedUpdate, 5);
    assert((reloadedS5 && reloadedS5->status.code == 0));
    const auto &reloadedMeans  = std::get<Array>(reloadedS5->outputs.at(2));
    const auto  reloadedValues = nestedOf<std::vector<double>>(reloadedMeans);
    assert((reloadedValues.size() == 2));
    assert((std::abs(reloadedValues[0] - values[0]) < 1e-6));
    assert((std::abs(reloadedValues[1] - values[1]) < 1e-6));

    // Test corrupted input on a temp copy: must fail (exception or diagnostics) and not corrupt source file
    const auto corruptTarget = std::filesystem::u8path(tmpDir.path().toUtf8().toStdString()) / "corrupted.vrt.json";
    std::filesystem::copy_file(tempProjectFile, corruptTarget);
    {
        std::ofstream corruptOut(corruptTarget, std::ios::app);
        corruptOut << "\nINVALID_JSON_TAIL\n";
    }
    bool corruptFailed = false;
    try
    {
        auto corruptLoaded = storage.load(corruptTarget);
        corruptFailed      = !corruptLoaded.diagnostics.empty();
    }
    catch (const std::exception &e)
    {
        std::cout << "Corrupted project loading correctly rejected with exception: " << e.what() << std::endl;
        corruptFailed = true;
    }
    assert((corruptFailed));
}

void testShippedExamples(const std::filesystem::path &toolsPkg, const std::filesystem::path &repoRoot)
{
    // The shipped examples are the product: every project under examples/ that is offered as a
    // first-batch sample must load without a diagnostic and produce real results from its own
    // images. A copy in a Chinese, space-containing folder keeps that claim independent of the
    // source tree and of the process working directory.
    QTemporaryDir tmpDir;
    assert((tmpDir.isValid()));
    const auto root = std::filesystem::u8path(tmpDir.path().toUtf8().toStdString()) / u8"示例 副本";
    std::filesystem::copy(repoRoot / "examples", root, std::filesystem::copy_options::recursive);

    const auto package = visionrt::readToolsPackage(toolsPkg);
    auto       loader  = std::make_shared<PluginLoader>();
    ToolRegistry registry;
    registry.setPluginLoader(loader);
    registry.registerPackage(package);
    registry.loadNativeImplementations();

    ProjectStorage storage(registry);
    const auto     run = [&](const std::filesystem::path &project, std::size_t nodeCount)
    {
        auto loaded = storage.load(project);
        for (const auto &diagnostic : loaded.diagnostics)
            std::cerr << "LOAD DIAGNOSTIC " << project.filename().string() << ": node="
                      << (diagnostic.node ? *diagnostic.node : 0) << " field=" << diagnostic.field
                      << " msg=" << diagnostic.message << std::endl;
        assert((loaded.diagnostics.empty()));
        assert((loaded.project.flows.size() == 1));
        assert((loaded.project.flows[0].nodes.size() == nodeCount));
        RuntimeEngine engine(4);
        const auto    started = engine.start(loaded.project, registry, RunMode::Once);
        assert((started.code == 0));
        auto update = finish(engine);
        for (const auto &node : loaded.project.flows[0].nodes)
        {
            auto result = snapshot(update, node.id);
            if (!result || result->status.code != 0)
                std::cerr << "NODE FAILURE " << project.filename().string() << " node=" << node.id
                          << " tool=" << node.toolId
                          << " code=" << (result ? result->status.code : -1)
                          << " message=" << (result ? result->status.message : std::string{"missing"})
                          << " detail=" << (result ? result->status.detail : std::string{}) << std::endl;
            assert((result && result->status.code == 0));
        }
        return update;
    };

    const auto firstBatch = root / u8"01_基础入门" / u8"01_图像读取" / u8"图像读取.vrt.json";
    const auto read       = run(firstBatch, 1);
    const auto readNode   = snapshot(read, 1);
    assert((std::get<DataRef<Image>>(readNode->outputs.at(2)).get().size() == 2));
    assert((std::get<std::int64_t>(readNode->outputs.at(4)) == 0));
    assert((std::get<std::int64_t>(readNode->outputs.at(5)) == 2));

    const auto preprocess = run(root / u8"02_工具示例" / u8"02_图像预处理" / u8"图像预处理.vrt.json", 3);
    const auto binarised  = std::get<DataRef<Image>>(snapshot(preprocess, 3)->outputs.at(2)).get();
    assert((binarised.size() == 1 && binarised[0].rows == 300 && binarised[0].cols == 400));

    const auto located = run(root / u8"02_工具示例" / u8"01_ROI定位与裁剪" / u8"ROI定位与裁剪.vrt.json", 3);
    const auto &rois   = std::get<DataRef<ROI>>(snapshot(located, 2)->outputs.at(3)).get();
    assert((rois.size() == 1 && rois[0].size() == 1));
    const auto &cropped = std::get<DataRef<Image>>(snapshot(located, 3)->outputs.at(2)).get();
    assert((cropped.size() == 1 && cropped[0].rows == 300 && cropped[0].cols == 60));

    const auto circle       = run(root / u8"02_工具示例" / u8"03_圆测量" / u8"圆测量.vrt.json", 2);
    const auto circleResult = snapshot(circle, 2);
    const auto radius       = nestedOf<std::vector<std::vector<double>>>(std::get<Array>(circleResult->outputs.at(9)));
    const auto centers = nestedOf<std::vector<std::vector<std::vector<double>>>>(
        std::get<Array>(circleResult->outputs.at(8)));
    assert((radius.size() == 1 && radius[0].size() == 1));
    assert((std::abs(radius[0][0] - 60.0) < 0.5));
    assert((std::abs(centers[0][0][0] - 160.0) < 0.5 && std::abs(centers[0][0][1] - 120.0) < 0.5));

    const auto spacing = run(root / u8"02_工具示例" / u8"04_行间距测量" / u8"行间距测量.vrt.json", 2);
    const auto spacingResult = snapshot(spacing, 2);
    const auto widths = nestedOf<std::vector<std::vector<std::vector<double>>>>(
        std::get<Array>(spacingResult->outputs.at(9)));
    const auto gaps = nestedOf<std::vector<std::vector<std::vector<double>>>>(
        std::get<Array>(spacingResult->outputs.at(10)));
    assert((widths.size() == 1 && widths[0].size() == 1 && widths[0][0].size() == 5));
    assert((gaps[0][0].size() == 4));
    for (const auto &width : widths[0][0])
        assert((std::abs(width - 20.0) < 0.5));
    for (const auto &gap : gaps[0][0])
        assert((std::abs(gap - 20.0) < 0.5));

    const auto branch = run(root / u8"03_综合示例" / u8"01_多分支视觉检测" / u8"多分支视觉检测.vrt.json", 5);
    const auto &branchRois = std::get<DataRef<ROI>>(snapshot(branch, 2)->outputs.at(3)).get();
    assert((branchRois.size() == 1 && branchRois[0].size() == 1));
    const auto &branchCrop = std::get<DataRef<Image>>(snapshot(branch, 3)->outputs.at(2)).get();
    assert((branchCrop.size() == 1 && branchCrop[0].rows == 300 && branchCrop[0].cols == 60));
    assert((!std::get<DataRef<Image>>(snapshot(branch, 4)->outputs.at(2)).get().empty()));
    const auto branchRadius
        = nestedOf<std::vector<std::vector<double>>>(std::get<Array>(snapshot(branch, 5)->outputs.at(9)));
    assert((branchRadius.size() == 1 && branchRadius[0].size() == 1));
    assert((std::abs(branchRadius[0][0] - 50.0) < 0.5));

    std::cout << "[SHIPPED EXAMPLES] six projects loaded and executed with their own images\n";
}

void testLogicExpressionExample(const std::filesystem::path &toolsPkg, const std::filesystem::path &repoRoot,
                                PythonRuntime &python)
{
    // The extension examples are shipped too, but they state a prerequisite: this one borrows the
    // Python runtime for its mean node, and the expression/branch nodes judge that number.
    QTemporaryDir tmpDir;
    assert((tmpDir.isValid()));
    const auto tmpPath     = std::filesystem::u8path(tmpDir.path().toUtf8().toStdString());
    const auto projectPath = std::filesystem::path(u8"04_扩展示例") / u8"02_逻辑表达式与条件分支";
    const auto projectFile = tmpPath / u8"逻辑表达式与条件分支.vrt.json";
    std::filesystem::copy_file(repoRoot / "examples" / projectPath / u8"逻辑表达式与条件分支.vrt.json", projectFile);
    std::filesystem::copy(repoRoot / "examples" / projectPath / "images", tmpPath / "images",
                          std::filesystem::copy_options::recursive);

    const auto package = visionrt::readToolsPackage(toolsPkg);
    auto       loader  = std::make_shared<PluginLoader>();
    ToolRegistry registry;
    registry.setPluginLoader(loader);
    registry.registerPackage(package);
    registry.loadNativeImplementations();
    setupPython(registry, python);

    ProjectStorage storage(registry);
    auto           loaded = storage.load(projectFile);
    for (const auto &diagnostic : loaded.diagnostics)
        std::cerr << "LOAD DIAGNOSTIC node=" << (diagnostic.node ? *diagnostic.node : 0)
                  << " field=" << diagnostic.field << " msg=" << diagnostic.message << std::endl;
    assert((loaded.diagnostics.empty()));
    assert((loaded.project.flows[0].nodes.size() == 4));

    RuntimeEngine engine(4);
    assert((engine.start(loaded.project, registry, RunMode::Once).code == 0));
    const auto update = finish(engine);

    // The first image averages 51.0, so the expression result is true and one unit away from 50.
    const auto expression = snapshot(update, 3);
    assert((expression && expression->status.code == 0));
    assert((std::get<bool>(expression->outputs.at(3))));
    assert((std::abs(std::get<double>(expression->outputs.at(4)) - 1.0) < 1e-6));

    // The branch reads that Bool upstream: the true side runs and the false side reports 1.
    const auto branch = snapshot(update, 4);
    assert((branch && branch->status.code == 0));
    assert((std::get<bool>(branch->outputs.at(2))));
    assert((!std::get<bool>(branch->outputs.at(3))));
    assert((std::get<std::int64_t>(branch->outputs.at(4)) == 0));
    assert((std::get<std::int64_t>(branch->outputs.at(5)) == 1));

    // The authored file must also be what the editor itself would write: saving it again and
    // reloading has to stay free of schema diagnostics.
    const auto saved = tmpPath / u8"重新保存.vrt.json";
    storage.save(loaded.project, saved);
    const auto reloaded = storage.load(saved);
    for (const auto &diagnostic : reloaded.diagnostics)
        std::cerr << "RELOAD DIAGNOSTIC field=" << diagnostic.field << " msg=" << diagnostic.message << std::endl;
    assert((reloaded.diagnostics.empty()));
    assert((reloaded.project.flows[0].nodes.size() == 4));

    std::cout << "[EXTENSION EXAMPLES] expression and branch example executed its Python prerequisites\n";
}

void testRaggedDetectionFilter(const std::filesystem::path &toolsPkg)
{
    const auto package = visionrt::readToolsPackage(toolsPkg);
    auto       loader  = std::make_shared<PluginLoader>();
    loader->registerPackage(package);

    PlatformToolServices services{PathResolver(toolsPkg), nullptr};
    auto                 inst = loader->createAdapter("visionrt.detection.filter", services);
    assert((inst != nullptr));
    assert((inst->initialize({}).code == 0));

    // 1. Scenario: 0 targets (empty rois/scores/classes)
    {
        cv::Mat      mat(100, 100, CV_8UC3, cv::Scalar(0, 0, 0));
        DataRef<Image> imgBatch(Image{(mat)});
        DataRef<ROI>   emptyRois(ROI{{}});
        Array          emptyScores  = Array::fromNested(NestedVector<float, 2>{{}});
        Array          emptyClasses = Array::fromNested(NestedVector<std::int64_t, 2>{{}});
        Array          emptyNames   = Array::fromNested(NestedVector<std::string, 2>{std::vector<std::string>{}});
        Array          allowed      = Array::fromNested(std::vector<std::int64_t>{});

        Values inputs;
        inputs.emplace(1, std::move(imgBatch));
        inputs.emplace(2, std::move(emptyRois));
        inputs.emplace(3, std::move(emptyScores));
        inputs.emplace(4, std::move(emptyClasses));
        inputs.emplace(5, std::move(emptyNames));
        inputs.emplace(8, std::move(allowed));
        inputs.emplace(6, 0.5);
        inputs.emplace(7, 0.0);

        auto res = inst->execute(inputs);
        assert((testview::status(res).code == 0));
        const auto *outCounts = std::get_if<Array>(&res.outputs[7]);
        assert((outCounts && !nestedOf<NestedVector<std::int64_t, 1>>(*outCounts).empty()));
        assert((nestedOf<NestedVector<std::int64_t, 1>>(*outCounts)[0] == 0));

        const auto *outRois = std::get_if<DataRef<ROI>>(&res.outputs[3]);
        assert((outRois && outRois->get().size() == 1 && outRois->get()[0].empty()));

        const auto *outScores = std::get_if<Array>(&res.outputs[4]);
        assert((outScores && !nestedOf<NestedVector<float, 2>>(*outScores).empty()));
        assert((nestedOf<NestedVector<float, 2>>(*outScores)[0].empty()));

        const auto *outClasses = std::get_if<Array>(&res.outputs[5]);
        assert((outClasses && !nestedOf<NestedVector<std::int64_t, 2>>(*outClasses).empty()));
        assert((nestedOf<NestedVector<std::int64_t, 2>>(*outClasses)[0].empty()));

        const auto *outNames = std::get_if<Array>(&res.outputs[6]);
        assert((outNames && !nestedOf<NestedVector<std::string, 2>>(*outNames).empty()));
        assert((nestedOf<NestedVector<std::string, 2>>(*outNames)[0].empty()));
    }

    // 2. Scenario: 3 distinct targets (different class, name, score, coords, angle)
    // Target 0: (10, 10, 20, 20, 5),  score=0.20, class=10, name="scratch" -> filtered out (< 0.5)
    // Target 1: (30, 30, 40, 40, 15), score=0.70, class=20, name="crack"   -> passes
    // Target 2: (50, 50, 60, 60, 30), score=0.95, class=30, name="dent"    -> passes
    {
        cv::Mat          mat(100, 100, CV_8UC3, cv::Scalar(0, 0, 0));
        DataRef<Image>   imgBatch(Image{(mat)});
        RegionGroup  rois3;
        rois3.push_back(std::vector<double>{2, 10.0f, 10.0f, 20.0f, 20.0f, 5.0f});
        rois3.push_back(std::vector<double>{2, 30.0f, 30.0f, 40.0f, 40.0f, 15.0f});
        rois3.push_back(std::vector<double>{2, 50.0f, 50.0f, 60.0f, 60.0f, 30.0f});
        DataRef<ROI> roiBatch(ROI{rois3});

        std::vector<float>        scoreVals = {0.20f, 0.70f, 0.95f};
        std::vector<std::int64_t> classVals = {10, 20, 30};
        std::vector<std::string>  nameVals  = {"scratch", "crack", "dent"};

        Array scores  = Array::fromNested(NestedVector<float, 2>{scoreVals});
        Array classes = Array::fromNested(NestedVector<std::int64_t, 2>{classVals});
        Array names   = Array::fromNested(NestedVector<std::string, 2>{nameVals});
        Array allowed = Array::fromNested(std::vector<std::int64_t>{});

        Values inputs;
        inputs.emplace(1, std::move(imgBatch));
        inputs.emplace(2, std::move(roiBatch));
        inputs.emplace(3, std::move(scores));
        inputs.emplace(4, std::move(classes));
        inputs.emplace(5, std::move(names));
        inputs.emplace(8, std::move(allowed));
        inputs.emplace(6, 0.5);
        inputs.emplace(7, 0.0);

        auto res = inst->execute(inputs);
        assert((testview::status(res).code == 0));
        const auto *outCounts = std::get_if<Array>(&res.outputs[7]);
        assert((outCounts && !nestedOf<NestedVector<std::int64_t, 1>>(*outCounts).empty()));
        assert((nestedOf<NestedVector<std::int64_t, 1>>(*outCounts)[0] == 2));

        const auto *outRois = std::get_if<DataRef<ROI>>(&res.outputs[3]);
        assert((outRois && outRois->get().size() == 1 && outRois->get()[0].size() == 2));

        // Target 1 ROI geometric fields
        const auto &r1 = outRois->get()[0][0];
        assert((roiKind(r1) == RoiKind::Rectangle));
        assert((r1.size() == 6));
        assert((r1[0] == static_cast<double>(RoiKind::Rectangle)));
        assert((std::abs(r1[1] - 30.0) < 1e-4)); // x
        assert((std::abs(r1[2] - 30.0) < 1e-4)); // y
        assert((std::abs(r1[3] - 40.0) < 1e-4)); // width
        assert((std::abs(r1[4] - 40.0) < 1e-4)); // height
        assert((std::abs(r1[5] - 15.0) < 1e-4)); // angle
        const auto corners1 = rectangleCorners(r1);
        assert((corners1.size() == 4));

        // Target 2 ROI geometric fields
        const auto &r2 = outRois->get()[0][1];
        assert((roiKind(r2) == RoiKind::Rectangle));
        assert((r2.size() == 6));
        assert((r2[0] == static_cast<double>(RoiKind::Rectangle)));
        assert((std::abs(r2[1] - 50.0) < 1e-4)); // x
        assert((std::abs(r2[2] - 50.0) < 1e-4)); // y
        assert((std::abs(r2[3] - 60.0) < 1e-4)); // width
        assert((std::abs(r2[4] - 60.0) < 1e-4)); // height
        assert((std::abs(r2[5] - 30.0) < 1e-4)); // angle
        const auto corners2 = rectangleCorners(r2);
        assert((corners2.size() == 4));

        const auto *outScores = std::get_if<Array>(&res.outputs[4]);
        assert((outScores && !nestedOf<NestedVector<float, 2>>(*outScores).empty()));
        const auto outScoreVals = nestedOf<NestedVector<float, 2>>(*outScores)[0];
        assert((outScoreVals.size() == 2));
        assert((std::abs(outScoreVals[0] - 0.70f) < 1e-4f));
        assert((std::abs(outScoreVals[1] - 0.95f) < 1e-4f));

        const auto *outClasses = std::get_if<Array>(&res.outputs[5]);
        assert((outClasses && !nestedOf<NestedVector<std::int64_t, 2>>(*outClasses).empty()));
        const auto outClassVals = nestedOf<NestedVector<std::int64_t, 2>>(*outClasses)[0];
        assert((outClassVals.size() == 2));
        assert((outClassVals[0] == 20));
        assert((outClassVals[1] == 30));

        const auto *outNames = std::get_if<Array>(&res.outputs[6]);
        const auto  nameRows = nestedOf<NestedVector<std::string, 2>>(*outNames);
        assert((outNames && !nameRows.empty()));
        const auto &outNameVals = nameRows[0];
        assert((outNameVals.size() == 2));
        assert((outNameVals[0] == "crack"));
        assert((outNameVals[1] == "dent"));
    }

    assert((inst->release().code == 0));
}

void testAdaptiveBinary(const std::filesystem::path &toolsPkg)
{
    const auto package = visionrt::readToolsPackage(toolsPkg);
    auto       loader  = std::make_shared<PluginLoader>();
    ToolRegistry registry;
    registry.setPluginLoader(loader);
    registry.registerPackage(package);
    registry.loadNativeImplementations();

    const auto *def = registry.find("visiontools.preprocess.adaptive_binary");
    assert((def != nullptr));
    assert((def->nameEn == "visiontools.preprocess.adaptive_binary"));
    assert((def->kind == ToolKind::Cpp));
    assert((registry.availability("visiontools.preprocess.adaptive_binary").state == ToolAvailability::Available));

    PlatformToolServices services;
    auto                 inst = registry.create("visiontools.preprocess.adaptive_binary", services);
    assert((inst != nullptr));
    assert((inst->initialize({}).code == 0));

    // Fixed input: a grayscale 10x10 image with half 50 and half 200
    cv::Mat mat(10, 10, CV_8UC1);
    mat(cv::Rect(0, 0, 5, 10)).setTo(50);
    mat(cv::Rect(5, 0, 5, 10)).setTo(200);

    DataRef<Image> imgBatch(Image{(mat)});
    Values     inputs;
    inputs.emplace(1, std::move(imgBatch));
    inputs.emplace(2, std::int64_t{3});
    inputs.emplace(3, 5.0);

    auto res = inst->execute(inputs);
    assert((testview::status(res).code == 0));
    const auto *outImages = std::get_if<DataRef<Image>>(&res.outputs[2]);
    assert((outImages && !outImages->get().empty()));
    const auto &outImg = outImages->get()[0];
    assert((outImg.rows == 10 && outImg.cols == 10 && outImg.type() == CV_8UC1));

    assert((inst->release().code == 0));
}

void testRoiProjection(const std::filesystem::path &toolsPkg)
{
    const auto package = visionrt::readToolsPackage(toolsPkg);
    auto       loader  = std::make_shared<PluginLoader>();
    ToolRegistry registry;
    registry.setPluginLoader(loader);
    registry.registerPackage(package);
    registry.loadNativeImplementations();

    const auto *def = registry.find("visionrt.roi.projection");
    assert((def != nullptr));
    assert((registry.availability("visionrt.roi.projection").state == ToolAvailability::Available));

    PlatformToolServices services;
    auto                 inst = registry.create("visionrt.roi.projection", services);
    assert((inst != nullptr));
    assert((inst->initialize({}).code == 0));

    // Fixed input: 100x100 black image with a 20x20 bright square (255) from x=40..60, y=30..50
    cv::Mat mat(100, 100, CV_8UC3, cv::Scalar(0, 0, 0));
    mat(cv::Rect(40, 30, 20, 20)).setTo(cv::Scalar(255, 255, 255));
    DataRef<Image> imgBatch(Image{(mat)});

    Values inputs;
    inputs.emplace(1, std::move(imgBatch));
    inputs.emplace(2, std::string{"x"});
    inputs.emplace(3, std::string{"bright"});
    inputs.emplace(4, std::int64_t{128});
    inputs.emplace(5, std::int64_t{1});
    inputs.emplace(6, std::int64_t{5});
    inputs.emplace(7, std::int64_t{0});
    inputs.emplace(8, std::int64_t{0});

    auto res = inst->execute(inputs);
    if (testview::status(res).code != 0)
    {
        std::cerr << "ROI projection execute failed: " << testview::status(res).message << std::endl;
    }
    assert((testview::status(res).code == 0));
    const auto *outRois = std::get_if<DataRef<ROI>>(&res.outputs[3]);
    assert((outRois && outRois->get().size() == 1 && !outRois->get()[0].empty()));
    const auto &r = outRois->get()[0][0];
    assert((roiKind(r) == RoiKind::Rectangle));
    assert((std::abs(r[1] - 40.0f) < 1.0f));
    assert((std::abs(r[3] - 20.0f) < 1.0f));

    assert((inst->release().code == 0));
}

void testMissingDependencyHandling(const std::filesystem::path &repoRoot, const std::filesystem::path &toolsPkg)
{
    QTemporaryDir temp;
    assert((temp.isValid()));
    const auto testPkg = std::filesystem::u8path(temp.path().toUtf8().toStdString()) / "package_missing_dep";
    std::filesystem::create_directories(testPkg / "bin");
    std::filesystem::create_directories(testPkg / "tools" / "builtin");
    std::filesystem::copy_file(toolsPkg / "module_index.json", testPkg / "module_index.json");
    std::filesystem::copy(toolsPkg / "tools", testPkg / "tools", std::filesystem::copy_options::recursive);
    for (const auto &entry : std::filesystem::directory_iterator(toolsPkg / "bin"))
    {
        std::filesystem::copy_file(entry.path(), testPkg / "bin" / entry.path().filename());
    }

    const auto appDir     = std::filesystem::u8path(QCoreApplication::applicationDirPath().toUtf8().toStdString());
    auto       pluginADll = appDir.parent_path() / "fixtures" / "test_plugin_a.dll";
    auto       depADll    = appDir.parent_path() / "fixtures" / "test_dep_a.dll";
    if (!std::filesystem::exists(pluginADll))
    {
        pluginADll = repoRoot / "build" / "fixtures" / "test_plugin_a.dll";
        depADll    = repoRoot / "build" / "fixtures" / "test_dep_a.dll";
    }
    if (!std::filesystem::exists(pluginADll))
    {
        std::cout << "[INFO] Skipping plugin missing dependency test (fixtures not found)\n";
        return;
    }

    // Copy test_plugin_a.dll to testPkg/bin, but DO NOT copy its required dependency test_dep_a.dll!
    const auto targetPluginA = testPkg / "bin" / "test_plugin_a.dll";
    std::filesystem::copy_file(pluginADll, targetPluginA);

    const auto package = visionrt::readToolsPackage(testPkg);
    auto       loader  = std::make_shared<PluginLoader>();
    loader->registerPackage(package);

    // 1. Verify valid real Tools package plugin loads successfully
    ToolStatus statusOk;
    const auto cropModPath = loader->resolveModulePath("visionrt.image.crop");
    assert((!cropModPath.empty()));
    auto cropMod = loader->loadModule(cropModPath, statusOk);
    assert((cropMod != nullptr));
    assert((statusOk.code == 0));

    // 2. Loading test_plugin_a.dll must fail because test_dep_a.dll is missing!
    ToolStatus statusMissing;
    auto       failMod = loader->loadModule(targetPluginA, statusMissing);
    assert((failMod == nullptr));
    assert((statusMissing.code != 0));
    // Diagnostic must contain the module path and the system error
    assert((statusMissing.message.find("test_plugin_a.dll") != std::string::npos));
#ifdef _WIN32
    assert(statusMissing.message.find("126") != std::string::npos
           || statusMissing.message.find("Win32 error") != std::string::npos);
#endif

    // 3. Now copy the missing dependency test_dep_a.dll into testPkg/bin
    const auto targetDepA = testPkg / "bin" / "test_dep_a.dll";
    std::filesystem::copy_file(depADll, targetDepA);

    // 4. Create fresh loader simulating restart: now test_plugin_a.dll loads successfully!
    auto loaderRecovered = std::make_shared<PluginLoader>();
    loaderRecovered->registerPackage(package);
    ToolStatus statusRecovered;
    auto       recoveredMod = loaderRecovered->loadModule(targetPluginA, statusRecovered);
    assert((recoveredMod != nullptr));
    assert((statusRecovered.code == 0));
}

void testRealToolsConcurrencyAndLifecycle(const std::filesystem::path &toolsPkg, const std::filesystem::path &repoRoot,
                                          PythonRuntime &python)
{
    const auto package = visionrt::readToolsPackage(toolsPkg);
    auto       loader  = std::make_shared<PluginLoader>();
    ToolRegistry registry;
    registry.setPluginLoader(loader);
    registry.registerPackage(package);
    registry.loadNativeImplementations();
    setupPython(registry, python);

    QTemporaryDir tmpDir;
    assert((tmpDir.isValid()));
    const auto tmpPath     = std::filesystem::u8path(tmpDir.path().toUtf8().toStdString());
    const auto projectPath = std::filesystem::path(u8"04_扩展示例") / u8"01_Python图像统计";
    const auto projectFile = tmpPath / u8"Python图像统计.vrt.json";
    std::filesystem::copy_file(repoRoot / "examples" / projectPath / u8"Python图像统计.vrt.json", projectFile);
    std::filesystem::copy(repoRoot / "examples" / projectPath / "images", tmpPath / "images",
                          std::filesystem::copy_options::recursive);

    ProjectStorage storage(registry);
    auto           loaded = storage.load(projectFile);
    assert((loaded.diagnostics.empty()));

    // Prepare 2 projects with different inputs (Engine 1: 2 images; Engine 2: 1 image via count=1)
    ProjectDefinition project1                 = loaded.project;
    ProjectDefinition project2                 = loaded.project;
    project2.flows[0].nodes[0].inputs[5] = {BindingMode::Literal, std::int64_t{1}, {}};

    // Concurrent execution of 2 independent engines sharing registry & plugin loader
    RuntimeEngine engine1(2);
    RuntimeEngine engine2(2);

    auto f1 = std::async(std::launch::async,
                         [&]
                         {
                             assert((engine1.start(project1, registry, RunMode::Once).code == 0));
                             return finish(engine1);
                         });
    auto f2 = std::async(std::launch::async,
                         [&]
                         {
                             assert((engine2.start(project2, registry, RunMode::Once).code == 0));
                             return finish(engine2);
                         });

    auto u1 = f1.get();
    auto u2 = f2.get();

    auto s1_node5 = snapshot(u1, 5);
    auto s2_node5 = snapshot(u2, 5);
    assert((s1_node5 && s1_node5->status.code == 0));
    assert((s2_node5 && s2_node5->status.code == 0));

    // Verify output isolation: Engine 1 produced 2 mean values, Engine 2 produced 1 mean value
    const auto v1 = nestedOf<std::vector<double>>(std::get<Array>(s1_node5->outputs.at(2)));
    const auto v2 = nestedOf<std::vector<double>>(std::get<Array>(s2_node5->outputs.at(2)));
    assert((v1.size() == 2));
    assert((v2.size() == 1));
    assert((std::abs(v1[0] - v2[0]) < 1e-6));

    // Cross-release snapshot check: engine resets, snapshot retains historical output & pixel memory
    auto s1_node1 = snapshot(u1, 1);
    assert((s1_node1 && s1_node1->status.code == 0));
    assert((engine1.requestReset().code == 0));
    finish(engine1);
    assert((s1_node5->status.code == 0));
    const auto &retainedImg = std::get<DataRef<Image>>(s1_node1->outputs.at(2)).get()[0];
    assert((retainedImg.cols > 0 && retainedImg.rows > 0));
    const auto *pxPtr = retainedImg.data;
    assert((pxPtr != nullptr));
    volatile std::uint8_t pxVal = *pxPtr;
    assert((pxVal == *pxPtr));

    // Success-then-failure isolation: the 2nd run with an invalid path never exposes the previous
    // run's images. A failed read has no result to publish, so the business port is structurally
    // Missing rather than an empty batch that downstream tools could mistake for "zero images".
    project1.flows[0].nodes[0].inputs[1] = {BindingMode::Literal, std::string{"nonexistent_images_dir"}, {}};
    assert((engine1.start(project1, registry, RunMode::Once).code == 0));
    auto u1_fail       = finish(engine1);
    auto s1_fail_node1 = snapshot(u1_fail, 1);
    assert((s1_fail_node1 && s1_fail_node1->status.code != 0));
    assert((std::holds_alternative<Missing>(s1_fail_node1->outputs.at(2))));
    assert((std::get_if<DataRef<Image>>(&s1_fail_node1->outputs.at(2)) == nullptr));
}

void testDiagnosticPipeline(const std::filesystem::path &toolsPkg, const std::filesystem::path &repoRoot,
                            PythonRuntime &python)
{
    QTemporaryDir tmpDir;
    assert((tmpDir.isValid()));
    const auto tmpPath         = std::filesystem::u8path(tmpDir.path().toUtf8().toStdString());
    const auto projectPath     = std::filesystem::path(u8"04_扩展示例") / u8"04_错误诊断";
    const auto tempProjectFile = tmpPath / u8"错误诊断.vrt.json";
    std::filesystem::copy_file(repoRoot / "examples" / projectPath / u8"错误诊断.vrt.json", tempProjectFile);
    std::filesystem::copy(repoRoot / "examples" / projectPath / "images", tmpPath / "images",
                          std::filesystem::copy_options::recursive);

    const auto package = visionrt::readToolsPackage(toolsPkg);
    auto       loader  = std::make_shared<PluginLoader>();
    ToolRegistry registry;
    registry.setPluginLoader(loader);
    registry.registerPackage(package);
    registry.loadNativeImplementations();
    setupPython(registry, python);

    ProjectStorage storage(registry);
    auto           loaded = storage.load(tempProjectFile);
    // Node 4 is DefinitionMissing
    bool           foundMissingDef = false;
    for (const auto &d : loaded.diagnostics)
    {
        if (d.node == 4 && d.code == ProjectStorageCode::DefinitionMissing)
        {
            foundMissingDef = true;
            break;
        }
    }
    assert((foundMissingDef));

    RuntimeEngine engine(2);
    const bool inferenceIncluded = registry.find("visionrt.dl.yolo") != nullptr;
    if (!inferenceIncluded)
    {
        assert(std::any_of(loaded.diagnostics.begin(), loaded.diagnostics.end(), [](const auto &diagnostic)
                          { return diagnostic.node == 3 && diagnostic.code == ProjectStorageCode::DefinitionMissing; }));
        assert((!engine.start(loaded.project, registry, RunMode::Once)));
        std::erase_if(loaded.project.flows[0].nodes, [](const auto &node) { return node.id == 3; });
        std::cout << "[NOT_RUN] Model-file negative execution: visionrt.dl.yolo is not included in this package.\n";
    }
    const auto    startRes = engine.start(loaded.project, registry, RunMode::Once);
    if (startRes.code != 0)
    {
        std::cerr << "Diagnostic pipeline engine.start failed: code=" << startRes.code << " msg=" << startRes.message
                  << std::endl;
    }
    assert((startRes.code == 0));
    auto update = finish(engine);

    // Node 1: read succeeds
    auto s1 = snapshot(update, 1);
    assert((s1 && s1->status.code == 0));

    auto s2 = snapshot(update, 2);
    assert((s2));
    assert((!s2->status));
    // The failed read publishes no batch at all: the port is Missing, so nothing downstream can
    // mistake the failure for a valid empty result.
    assert((std::holds_alternative<Missing>(s2->outputs.at(2))));

    // Node 3: yolo fails because nonexistent model weights
    if (inferenceIncluded)
    {
        auto s3 = snapshot(update, 3);
        assert((s3));
        assert((s3->status.code != 0));
        assert((!s3->status.message.empty()));
    }

    // Try starting the flow containing Node 4 (definition missing)
    loaded.project.flows[0].runSelected = false;
    loaded.project.flows[1].runSelected = true;
    const auto missingStartStatus       = engine.start(loaded.project, registry, RunMode::Once);
    assert((!missingStartStatus));
    assert((missingStartStatus.code == -2));
    assert((missingStartStatus.message == "执行节点的工具定义缺失：unavailable.custom.plugin"));
    assert((missingStartStatus.detail.empty()));
}

void testRealPluginModulePipeline(PythonRuntime &python)
{
    const auto appDir    = std::filesystem::u8path(QCoreApplication::applicationDirPath().toUtf8().toStdString());
    auto       pluginDll = appDir / "test_plugin_module.dll";
    if (!std::filesystem::exists(pluginDll))
    {
        pluginDll = appDir / "bin" / "test_plugin_module.dll";
    }
    assert((std::filesystem::exists(pluginDll)));

    auto loader = std::make_shared<PluginLoader>();
    loader->registerModule("test.plugin.counter", pluginDll);
    loader->registerModule("test.plugin.function", pluginDll);
    loader->registerModule("test.plugin.fail_init", pluginDll);

    ToolRegistry registry;
    registry.setPluginLoader(loader);
    setupPython(registry, python);

    // Register plugin counter tool
    ToolDefinition counterDef;
    counterDef.schemaVersion    = 6;
    counterDef.nameEn           = "test.plugin.counter";
    counterDef.nameZh           = "Plugin Counter";
    counterDef.version          = "1.0.0";
    counterDef.kind             = ToolKind::Cpp;
    counterDef.nextInitPortId   = 1;
    counterDef.nextInputPortId  = 1;
    counterDef.nextOutputPortId = 3;

    PortDefinition initParam;
    initParam.id           = 0;
    initParam.nameEn       = "initial";
    initParam.nameZh       = "Initial";
    initParam.type         = {Type::Int};
    initParam.defaultValue = std::int64_t{10};
    counterDef.initParams.push_back(initParam);
    PortDefinition outCount;
    outCount.id     = 2;
    outCount.nameEn = "count";
    outCount.nameZh = "Count";
    outCount.type   = {Type::Int};
    counterDef.outputs.push_back(outCount);
    normalizeToolDefinition(counterDef);
    registry.addDefinition(counterDef);

    // Register plugin function tool
    ToolDefinition funcDef;
    funcDef.schemaVersion    = 6;
    funcDef.nameEn           = "test.plugin.function";
    funcDef.nameZh           = "Plugin Function";
    funcDef.version          = "1.0.0";
    funcDef.kind             = ToolKind::Cpp;
    funcDef.nextInitPortId   = 0;
    funcDef.nextInputPortId  = 2;
    funcDef.nextOutputPortId = 3;

    PortDefinition inX;
    inX.id           = 1;
    inX.nameEn       = "x";
    inX.nameZh       = "X";
    inX.type         = {Type::Int};
    inX.defaultValue = std::int64_t{25};
    funcDef.inputs.push_back(inX);
    PortDefinition outY;
    outY.id     = 2;
    outY.nameEn = "y";
    outY.nameZh = "Y";
    outY.type   = {Type::Int};
    funcDef.outputs.push_back(outY);
    normalizeToolDefinition(funcDef);
    registry.addDefinition(funcDef);
    registry.loadNativeImplementations();

    // Build test pipeline: Node 1 (function, x=25 -> y=50), Node 2 (counter, initial=10 -> count=11)
    ProjectDefinition project;
    FlowDefinition    flow;
    flow.id   = "flow_1";
    flow.name = "Real Plugin Flow";

    NodeDefinition n1;
    n1.id               = 1;
    n1.toolId           = "test.plugin.function";
    n1.toolNameSnapshot = "Plugin Function";
    n1.position         = {100, 100};
    InputBinding b1;
    b1.mode    = BindingMode::Literal;
    b1.literal = std::int64_t{25};
    n1.inputs.emplace(1, b1);
    n1.signatures.inputs[1]  = {Type::Int};
    n1.signatures.outputs[2] = {Type::Int};
    flow.nodes.push_back(n1);

    NodeDefinition n2;
    n2.id               = 2;
    n2.toolId           = "test.plugin.counter";
    n2.toolNameSnapshot = "Plugin Counter";
    n2.position         = {300, 100};
    n2.initParams.emplace(0, std::int64_t{10});
    n2.signatures.initParams[0] = {Type::Int};
    n2.signatures.outputs[2]    = {Type::Int};
    flow.nodes.push_back(n2);

    project.flows.clear();
    project.flows.push_back(flow);
    project.nextNodeId = 3;

    RuntimeEngine engine(2);
    const auto    startRes = engine.start(project, registry, RunMode::Once);
    assert((startRes.code == 0));
    const auto update = finish(engine);

    auto s1 = snapshot(update, 1);
    assert((s1 && s1->status.code == 0));
    assert((std::get<std::int64_t>(s1->outputs.at(2)) == 50));

    auto s2 = snapshot(update, 2);
    assert((s2 && s2->status.code == 0));
    assert((std::get<std::int64_t>(s2->outputs.at(2)) == 11));

    // Test Save / Reopen consistency
    QTemporaryDir tmpDir;
    assert((tmpDir.isValid()));
    const auto     savePath = std::filesystem::u8path(tmpDir.path().toUtf8().toStdString()) / "plugin_project.vrt.json";
    ProjectStorage storage(registry);
    auto           reconciled = storage.reconcile(project);
    assert((reconciled.diagnostics.empty()));
    project = std::move(reconciled.project);
    storage.save(project, savePath);
    const auto reloaded = storage.load(savePath);
    assert((reloaded.diagnostics.empty()));
    assert((engine.start(reloaded.project, registry, RunMode::Once).code == 0));
    const auto reloadedUpdate = finish(engine);
    auto       rs1            = snapshot(reloadedUpdate, 1);
    assert((rs1 && std::get<std::int64_t>(rs1->outputs.at(2)) == std::get<std::int64_t>(s1->outputs.at(2))));
    auto rs2 = snapshot(reloadedUpdate, 2);
    assert(
        (rs2 && std::get<std::int64_t>(rs2->outputs.at(2)) == std::get<std::int64_t>(s2->outputs.at(2))));
}

} // namespace

namespace {

void copyWithoutGpuRuntime(const std::filesystem::path &source, const std::filesystem::path &target)
{
    std::filesystem::create_directories(target);
    for (const auto &entry : std::filesystem::directory_iterator(source))
    {
        const auto name = entry.path().filename().string();
        const auto dest = target / entry.path().filename();
        if (entry.is_directory())
            copyWithoutGpuRuntime(entry.path(), dest);
        else if (name.rfind("nvinfer", 0) != 0)
            std::filesystem::copy_file(entry.path(), dest, std::filesystem::copy_options::overwrite_existing);
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication      app(argc, argv);
    std::filesystem::path installRoot;
    std::filesystem::path repoRoot;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--tools-package" && i + 1 < argc)
        {
            installRoot = std::filesystem::path(argv[++i]);
        }
        else if (arg.rfind("--tools-package=", 0) == 0)
        {
            installRoot = std::filesystem::path(arg.substr(16));
        }
        else if (!arg.starts_with("--"))
        {
            repoRoot = std::filesystem::path(arg);
        }
    }

    if (repoRoot.empty())
    {
        repoRoot = std::filesystem::current_path();
    }

    PythonRuntime python(testsupport::pythonEnvironment());

    const auto toolsPkg = resolveToolsPackage(installRoot);
    if (toolsPkg.empty())
    {
        std::cerr << "FATAL: Real VisionTools package not found! "
                     "RT-S03 requires valid tools package."
                  << std::endl;
        return 1;
    }
    std::cout << "[REAL TOOLS PACKAGE] Verified Tools package at: " << toolsPkg.string() << std::endl;

    // Test in a temporary installation directory with Chinese characters and spaces
    QTemporaryDir unicodeTemp;
    assert((unicodeTemp.isValid()));
    const auto unicodePkg
        = std::filesystem::u8path(unicodeTemp.path().toUtf8().toStdString()) / "测试 工具 目录 with space";
    // Only the unicode-path contract is under test, so the heavyweight GPU inference runtime is
    // skipped: it is never loaded by these CPU/Python examples and would cost gigabytes per run.
    copyWithoutGpuRuntime(toolsPkg, unicodePkg);

    std::cout << "[TEST] Running real Tools package CPU + Python pipeline in unicode path (RT-S03)..." << std::endl;
    testRealToolsCpuPythonPipeline(unicodePkg, repoRoot, python);
    std::cout << "[PASS] Real Tools package CPU + Python pipeline passed." << std::endl;

    std::cout << "[TEST] Running the shipped example projects..." << std::endl;
    testShippedExamples(unicodePkg, repoRoot);
    std::cout << "[PASS] Shipped example projects passed." << std::endl;

    std::cout << "[TEST] Running the expression and branch example..." << std::endl;
    testLogicExpressionExample(unicodePkg, repoRoot, python);
    std::cout << "[PASS] Expression and branch example passed." << std::endl;

    std::cout << "[TEST] Running ragged detection filter 0/3 targets test (RT-S03)..." << std::endl;
    testRaggedDetectionFilter(unicodePkg);
    std::cout << "[PASS] Ragged detection filter 0/3 targets test passed." << std::endl;

    std::cout << "[TEST] Running adaptive binarization fixed input test (RT-S03)..." << std::endl;
    testAdaptiveBinary(unicodePkg);
    std::cout << "[PASS] Adaptive binarization test passed." << std::endl;

    std::cout << "[TEST] Running ROI projection fixed input test (RT-S03)..." << std::endl;
    testRoiProjection(unicodePkg);
    std::cout << "[PASS] ROI projection test passed." << std::endl;

    std::cout << "[TEST] Running concurrent execution & lifecycle test (RT-S03)..." << std::endl;
    testRealToolsConcurrencyAndLifecycle(unicodePkg, repoRoot, python);
    std::cout << "[PASS] Concurrent execution & lifecycle test passed." << std::endl;

    std::cout << "[TEST] Running error diagnostic pipeline example (RT-S03)..." << std::endl;
    testDiagnosticPipeline(unicodePkg, repoRoot, python);
    std::cout << "[PASS] Error diagnostic pipeline passed." << std::endl;

    std::cout << "[TEST] Running missing transitive dependency handling test (RT-S03)..." << std::endl;
    testMissingDependencyHandling(repoRoot, toolsPkg);
    std::cout << "[PASS] Missing transitive dependency handling test passed." << std::endl;

    std::cout << "[TEST DLL] Running mock test_plugin_module pipeline..." << std::endl;
    testRealPluginModulePipeline(python);
    std::cout << "[PASS] Mock test_plugin_module pipeline passed." << std::endl;

    std::cout << "All example contract tests passed successfully!" << std::endl;
    return 0;
}
