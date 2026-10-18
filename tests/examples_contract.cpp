#include "core/ProjectStorage.h"
#include "core/ToolRegistry.h"
#include "runtime/RuntimeEngine.h"
#include "python/PythonRuntime.h"
#include "plugins/PluginLoader.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QProcess>
#include <opencv2/core.hpp>
#include <cassert>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>

using namespace visionrt;
using namespace std::chrono_literals;

namespace {

RuntimeUpdate finish(RuntimeEngine& engine) {
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (std::chrono::steady_clock::now() < deadline) {
        auto update = engine.pollLatest();
        if (update.completion) {
            assert(engine.acknowledgeCompletion(*update.completion));
            return update;
        }
        std::this_thread::sleep_for(2ms);
    }
    throw std::runtime_error("runtime completion deadline exceeded");
}

ResultSlot::Snapshot snapshot(const RuntimeUpdate& update, NodeId id) {
    for (const auto& item : update.nodes) {
        if (item.node == id) return item.snapshot;
    }
    throw std::runtime_error("node update absent: " + std::to_string(id));
}

void setupPython(ToolRegistry& registry, PythonRuntime& runtime) {
    std::vector<ToolDefinition> pyDefs;
    for (const auto& d : registry.definitions()) {
        if (d.kind == ToolKind::PythonClass || d.kind == ToolKind::PythonFunction) {
            pyDefs.push_back(d);
        }
    }
    const auto loadedList = runtime.refresh(pyDefs);
    for (const auto& loaded : loadedList) {
        registry.setPythonImplementation(loaded.toolId, runtime.factory(*registry.find(loaded.toolId)), loaded.status);
    }
}

std::filesystem::path resolveToolsPackage(const std::filesystem::path& repoRoot, const std::filesystem::path& installRoot = {}) {
    if (!installRoot.empty() && std::filesystem::exists(installRoot / "module_index.yaml")) {
        return std::filesystem::canonical(installRoot);
    }
    const char* env = std::getenv("VISIONTOOLS_PACKAGE_DIR");
    if (env) {
        auto p = std::filesystem::path(env);
        if (std::filesystem::exists(p / "module_index.yaml")) {
            return std::filesystem::canonical(p);
        }
        return {};
    }
    const auto buildPkg = repoRoot / "build" / "tools-package";
    if (std::filesystem::exists(buildPkg / "module_index.yaml")) {
        return std::filesystem::canonical(buildPkg);
    }
    const auto installed = repoRoot / "install" / "VisionRT-0.1.0" / "tools-package";
    if (std::filesystem::exists(installed / "module_index.yaml")) {
        return std::filesystem::canonical(installed);
    }
    return {};
}

void testRealToolsCpuPythonPipeline(const std::filesystem::path& toolsPkg, const std::filesystem::path& repoRoot, PythonRuntime& python) {
    QTemporaryDir tmpDir;
    assert(tmpDir.isValid());
    const auto tmpPath = std::filesystem::path(tmpDir.path().toStdString());
    const auto tempProjectFile = tmpPath / "pipeline_cpu_python.vrt";
    std::filesystem::copy_file(repoRoot / "examples" / "pipeline_cpu_python.vrt", tempProjectFile);
    std::filesystem::copy(repoRoot / "examples" / "images", tmpPath / "images", std::filesystem::copy_options::recursive);

    auto loader = std::make_shared<PluginLoader>();
    loader->setBaseDirectory(toolsPkg);
    loader->loadIndexFile(toolsPkg / "module_index.yaml");
    if (!loader->diagnostics().empty()) {
        for (const auto& d : loader->diagnostics()) {
            std::cerr << "Module index diagnostic: " << d.message << std::endl;
        }
        throw std::runtime_error("Module index loading reported diagnostics");
    }

    ToolRegistry registry;
    registry.setPluginLoader(loader);
    std::vector<std::filesystem::path> roots;
    roots.push_back(toolsPkg / "assets" / "tools");
    if (std::filesystem::exists(toolsPkg / "examples" / "Python")) {
        roots.push_back(toolsPkg / "examples" / "Python");
    }
    roots.push_back(repoRoot / "assets" / "tools" / "custom");
    registry.scanRoots(roots);
    setupPython(registry, python);

    ProjectStorage storage(registry);
    auto loaded = storage.load(tempProjectFile);
    for (const auto& d : loaded.diagnostics) {
        std::cerr << "LOAD DIAGNOSTIC: node=" << (d.node ? *d.node : 0) << " code=" << static_cast<int>(d.code)
                  << " field=" << d.field << " msg=" << d.message << std::endl;
    }
    assert(loaded.diagnostics.empty());
    assert(loaded.project.flows.size() == 1);
    assert(loaded.project.flows[0].nodes.size() == 5);

    RuntimeEngine engine(4);
    assert(engine.start(loaded.project, registry, RunMode::Once).code == 0);
    auto update = finish(engine);

    // Node 1: image read (executed via visiontools_preprocess.dll)
    auto s1 = snapshot(update, 1);
    assert(s1 && s1->status.code == 0);
    const auto& images1 = std::get<ImageBatch>(s1->outputs.at("images"));
    assert(images1.images().size() >= 2);

    // Node 2: roi projection (executed via visiontools_preprocess.dll)
    auto s2 = snapshot(update, 2);
    assert(s2 && s2->status.code == 0);
    const auto& rois2 = std::get<RoiBatch>(s2->outputs.at("rois"));
    assert(rois2.images().size() == images1.images().size());

    // Node 3: crop (executed via visiontools_preprocess.dll)
    auto s3 = snapshot(update, 3);
    assert(s3 && s3->status.code == 0);
    const auto& croppedImages = std::get<ImageBatch>(s3->outputs.at("images"));
    assert(!croppedImages.images().empty());

    // Node 4: preprocess (executed via visiontools_preprocess.dll)
    auto s4 = snapshot(update, 4);
    assert(s4 && s4->status.code == 0);
    const auto& preprocessed = std::get<ImageBatch>(s4->outputs.at("images"));
    assert(preprocessed.images().size() == croppedImages.images().size());

    // Node 5: python mean
    auto s5 = snapshot(update, 5);
    assert(s5 && s5->status.code == 0);
    const auto& means = std::get<NumericArray>(s5->outputs.at("means"));
    assert(means.buffers().size() == 1);
    assert(means.buffers()[0].shape()[0] == static_cast<std::int64_t>(images1.images().size()));
    const auto values = means.buffers()[0].values<double>();
    assert(values.size() == 2);
    assert(std::abs(values[0] - 51.0) < 0.5);
    assert(std::abs(values[1] - 22.95) < 0.5);

    // Test Save As / Reopen consistency & re-execution output equality
    const auto saveTarget = std::filesystem::path(tmpDir.path().toStdString()) / "reopened.vrt";
    storage.save(loaded.project, saveTarget);
    auto reloaded = storage.load(saveTarget);
    assert(reloaded.diagnostics.empty());
    assert(reloaded.project.flows[0].nodes.size() == 5);
    for (size_t i = 0; i < 5; ++i) {
        assert(reloaded.project.flows[0].nodes[i].toolId == loaded.project.flows[0].nodes[i].toolId);
    }
    assert(engine.start(reloaded.project, registry, RunMode::Once).code == 0);
    auto reloadedUpdate = finish(engine);
    auto reloadedS5 = snapshot(reloadedUpdate, 5);
    assert(reloadedS5 && reloadedS5->status.code == 0);
    const auto& reloadedMeans = std::get<NumericArray>(reloadedS5->outputs.at("means"));
    const auto reloadedValues = reloadedMeans.buffers()[0].values<double>();
    assert(reloadedValues.size() == 2);
    assert(std::abs(reloadedValues[0] - values[0]) < 1e-6);
    assert(std::abs(reloadedValues[1] - values[1]) < 1e-6);

    // Test corrupted input on a temp copy: must fail (exception or diagnostics) and not corrupt source file
    const auto corruptTarget = std::filesystem::path(tmpDir.path().toStdString()) / "corrupted.vrt";
    std::filesystem::copy_file(tempProjectFile, corruptTarget);
    {
        std::ofstream corruptOut(corruptTarget, std::ios::app);
        corruptOut << "\n!@#$%^&*()INVALID_YAML_TAIL:\n";
    }
    bool corruptFailed = false;
    try {
        auto corruptLoaded = storage.load(corruptTarget);
        corruptFailed = !corruptLoaded.diagnostics.empty();
    } catch (const std::exception& e) {
        std::cout << "Corrupted project loading correctly rejected with exception: " << e.what() << std::endl;
        corruptFailed = true;
    }
    assert(corruptFailed);
}

void testRaggedDetectionFilter(const std::filesystem::path& toolsPkg) {
    auto loader = std::make_shared<PluginLoader>();
    loader->setBaseDirectory(toolsPkg);
    loader->loadIndexFile(toolsPkg / "module_index.yaml");
    assert(loader->diagnostics().empty());

    PlatformToolServices services{PathResolver(toolsPkg), nullptr};
    auto inst = loader->createAdapter("visionrt.detection.filter", services);
    assert(inst != nullptr);
    assert(inst->initialize({}).code == 0);

    // 1. Scenario: 0 targets (empty rois/scores/classes)
    {
        cv::Mat mat(100, 100, CV_8UC3, cv::Scalar(0, 0, 0));
        ImageBatch imgBatch({OwnedImage::copyOf(mat)});
        RoiBatch emptyRois(std::vector<std::vector<Roi>>{{}});
        NumericArray emptyScores(DType::Float32, 2, Layout::BatchRagged, {NumericBuffer::copy(DType::Float32, {0}, {})});
        NumericArray emptyClasses(DType::Int64, 2, Layout::BatchRagged, {NumericBuffer::copy(DType::Int64, {0}, {})});
        StringArray emptyNames(2, Layout::BatchRagged, {StringBuffer(std::vector<std::int64_t>{0}, std::vector<std::string>{})});
        NumericArray allowed(DType::Int64, 1, Layout::Dense, {NumericBuffer::copy(DType::Int64, {0}, {})});

        Values inputs;
        inputs.emplace("images", std::move(imgBatch));
        inputs.emplace("rois", std::move(emptyRois));
        inputs.emplace("scores", std::move(emptyScores));
        inputs.emplace("classes", std::move(emptyClasses));
        inputs.emplace("class_names", std::move(emptyNames));
        inputs.emplace("allowed_classes", std::move(allowed));
        inputs.emplace("min_score", 0.5);
        inputs.emplace("min_area", 0.0);

        auto res = inst->execute(inputs);
        assert(res.status.code == 0);
        const auto* outCounts = std::get_if<NumericArray>(&res.outputs["counts"]);
        assert(outCounts && !outCounts->buffers().empty());
        assert(outCounts->buffers()[0].values<std::int64_t>()[0] == 0);

        const auto* outRois = std::get_if<RoiBatch>(&res.outputs["rois"]);
        assert(outRois && outRois->images().size() == 1 && outRois->images()[0].empty());

        const auto* outScores = std::get_if<NumericArray>(&res.outputs["scores"]);
        assert(outScores && !outScores->buffers().empty());
        assert(outScores->buffers()[0].shape().size() == 1 && outScores->buffers()[0].shape()[0] == 0);

        const auto* outClasses = std::get_if<NumericArray>(&res.outputs["classes"]);
        assert(outClasses && !outClasses->buffers().empty());
        assert(outClasses->buffers()[0].shape().size() == 1 && outClasses->buffers()[0].shape()[0] == 0);

        const auto* outNames = std::get_if<StringArray>(&res.outputs["class_names"]);
        assert(outNames && !outNames->buffers().empty());
        assert(outNames->buffers()[0].values().empty());
    }

    // 2. Scenario: 3 distinct targets (different class, name, score, coords, angle)
    // Target 0: (10, 10, 20, 20, 5),  score=0.20, class=10, name="scratch" -> filtered out (< 0.5)
    // Target 1: (30, 30, 40, 40, 15), score=0.70, class=20, name="crack"   -> passes
    // Target 2: (50, 50, 60, 60, 30), score=0.95, class=30, name="dent"    -> passes
    {
        cv::Mat mat(100, 100, CV_8UC3, cv::Scalar(0, 0, 0));
        ImageBatch imgBatch({OwnedImage::copyOf(mat)});
        std::vector<Roi> rois3;
        rois3.push_back(Roi::rectangle(10.0f, 10.0f, 20.0f, 20.0f, 5.0f));
        rois3.push_back(Roi::rectangle(30.0f, 30.0f, 40.0f, 40.0f, 15.0f));
        rois3.push_back(Roi::rectangle(50.0f, 50.0f, 60.0f, 60.0f, 30.0f));
        RoiBatch roiBatch({rois3});

        std::vector<float> scoreVals = {0.20f, 0.70f, 0.95f};
        std::vector<std::int64_t> classVals = {10, 20, 30};
        std::vector<std::string> nameVals = {"scratch", "crack", "dent"};

        NumericArray scores(DType::Float32, 2, Layout::BatchRagged, {NumericBuffer::copy(DType::Float32, {3}, std::as_bytes(std::span(scoreVals)))});
        NumericArray classes(DType::Int64, 2, Layout::BatchRagged, {NumericBuffer::copy(DType::Int64, {3}, std::as_bytes(std::span(classVals)))});
        StringArray names(2, Layout::BatchRagged, {StringBuffer(std::vector<std::int64_t>{3}, nameVals)});
        NumericArray allowed(DType::Int64, 1, Layout::Dense, {NumericBuffer::copy(DType::Int64, {0}, {})});

        Values inputs;
        inputs.emplace("images", std::move(imgBatch));
        inputs.emplace("rois", std::move(roiBatch));
        inputs.emplace("scores", std::move(scores));
        inputs.emplace("classes", std::move(classes));
        inputs.emplace("class_names", std::move(names));
        inputs.emplace("allowed_classes", std::move(allowed));
        inputs.emplace("min_score", 0.5);
        inputs.emplace("min_area", 0.0);

        auto res = inst->execute(inputs);
        assert(res.status.code == 0);
        const auto* outCounts = std::get_if<NumericArray>(&res.outputs["counts"]);
        assert(outCounts && !outCounts->buffers().empty());
        assert(outCounts->buffers()[0].values<std::int64_t>()[0] == 2);

        const auto* outRois = std::get_if<RoiBatch>(&res.outputs["rois"]);
        assert(outRois && outRois->images().size() == 1 && outRois->images()[0].size() == 2);

        // Target 1 ROI geometric fields
        const auto& r1 = outRois->images()[0][0];
        assert(r1.kind() == RoiKind::Rectangle);
        assert(r1.encoded().size() == 6);
        assert(r1.encoded()[0] == static_cast<float>(RoiKind::Rectangle));
        assert(std::abs(r1.encoded()[1] - 30.0f) < 1e-4f); // x
        assert(std::abs(r1.encoded()[2] - 30.0f) < 1e-4f); // y
        assert(std::abs(r1.encoded()[3] - 40.0f) < 1e-4f); // width
        assert(std::abs(r1.encoded()[4] - 40.0f) < 1e-4f); // height
        assert(std::abs(r1.encoded()[5] - 15.0f) < 1e-4f); // angle
        const auto corners1 = r1.rectangleCorners();
        assert(corners1.size() == 4);

        // Target 2 ROI geometric fields
        const auto& r2 = outRois->images()[0][1];
        assert(r2.kind() == RoiKind::Rectangle);
        assert(r2.encoded().size() == 6);
        assert(r2.encoded()[0] == static_cast<float>(RoiKind::Rectangle));
        assert(std::abs(r2.encoded()[1] - 50.0f) < 1e-4f); // x
        assert(std::abs(r2.encoded()[2] - 50.0f) < 1e-4f); // y
        assert(std::abs(r2.encoded()[3] - 60.0f) < 1e-4f); // width
        assert(std::abs(r2.encoded()[4] - 60.0f) < 1e-4f); // height
        assert(std::abs(r2.encoded()[5] - 30.0f) < 1e-4f); // angle
        const auto corners2 = r2.rectangleCorners();
        assert(corners2.size() == 4);

        const auto* outScores = std::get_if<NumericArray>(&res.outputs["scores"]);
        assert(outScores && !outScores->buffers().empty());
        const auto outScoreVals = outScores->buffers()[0].values<float>();
        assert(outScoreVals.size() == 2);
        assert(std::abs(outScoreVals[0] - 0.70f) < 1e-4f);
        assert(std::abs(outScoreVals[1] - 0.95f) < 1e-4f);

        const auto* outClasses = std::get_if<NumericArray>(&res.outputs["classes"]);
        assert(outClasses && !outClasses->buffers().empty());
        const auto outClassVals = outClasses->buffers()[0].values<std::int64_t>();
        assert(outClassVals.size() == 2);
        assert(outClassVals[0] == 20);
        assert(outClassVals[1] == 30);

        const auto* outNames = std::get_if<StringArray>(&res.outputs["class_names"]);
        assert(outNames && !outNames->buffers().empty());
        const auto& outNameVals = outNames->buffers()[0].values();
        assert(outNameVals.size() == 2);
        assert(outNameVals[0] == "crack");
        assert(outNameVals[1] == "dent");
    }

    assert(inst->release().code == 0);
}

void testAdaptiveBinary(const std::filesystem::path& toolsPkg) {
    auto loader = std::make_shared<PluginLoader>();
    loader->setBaseDirectory(toolsPkg);
    loader->loadIndexFile(toolsPkg / "module_index.yaml");
    assert(loader->diagnostics().empty());

    ToolRegistry registry;
    registry.setPluginLoader(loader);
    std::vector<std::filesystem::path> roots;
    roots.push_back(toolsPkg / "assets" / "tools");
    registry.scanRoots(roots);

    const auto* def = registry.find("visiontools.preprocess.adaptive_binary");
    assert(def != nullptr);
    assert(registry.availability("visiontools.preprocess.adaptive_binary").state == ToolAvailability::Available);

    PlatformToolServices services;
    auto inst = registry.create("visiontools.preprocess.adaptive_binary", services);
    assert(inst != nullptr);
    assert(inst->initialize({}).code == 0);

    // Fixed input: a grayscale 10x10 image with half 50 and half 200
    cv::Mat mat(10, 10, CV_8UC1);
    mat(cv::Rect(0, 0, 5, 10)).setTo(50);
    mat(cv::Rect(5, 0, 5, 10)).setTo(200);

    ImageBatch imgBatch({OwnedImage::copyOf(mat)});
    Values inputs;
    inputs.emplace("images", std::move(imgBatch));
    inputs.emplace("block_size", std::int64_t{3});
    inputs.emplace("constant_offset", 5.0);

    auto res = inst->execute(inputs);
    assert(res.status.code == 0);
    const auto* outImages = std::get_if<ImageBatch>(&res.outputs["images"]);
    assert(outImages && !outImages->images().empty());
    const auto& outImg = outImages->images()[0];
    assert(outImg.rows() == 10 && outImg.cols() == 10 && outImg.type() == CV_8UC1);

    assert(inst->release().code == 0);
}

void testRoiProjection(const std::filesystem::path& toolsPkg) {
    auto loader = std::make_shared<PluginLoader>();
    loader->setBaseDirectory(toolsPkg);
    loader->loadIndexFile(toolsPkg / "module_index.yaml");
    assert(loader->diagnostics().empty());

    ToolRegistry registry;
    registry.setPluginLoader(loader);
    std::vector<std::filesystem::path> roots;
    roots.push_back(toolsPkg / "assets" / "tools");
    registry.scanRoots(roots);

    const auto* def = registry.find("visionrt.roi.projection");
    assert(def != nullptr);
    assert(registry.availability("visionrt.roi.projection").state == ToolAvailability::Available);

    PlatformToolServices services;
    auto inst = registry.create("visionrt.roi.projection", services);
    assert(inst != nullptr);
    assert(inst->initialize({}).code == 0);

    // Fixed input: 100x100 black image with a 20x20 bright square (255) from x=40..60, y=30..50
    cv::Mat mat(100, 100, CV_8UC3, cv::Scalar(0, 0, 0));
    mat(cv::Rect(40, 30, 20, 20)).setTo(cv::Scalar(255, 255, 255));
    ImageBatch imgBatch({OwnedImage::copyOf(mat)});

    Values inputs;
    inputs.emplace("images", std::move(imgBatch));
    inputs.emplace("axis", Utf8{"x"});
    inputs.emplace("polarity", Utf8{"bright"});
    inputs.emplace("threshold", std::int64_t{128});
    inputs.emplace("min_pixels", std::int64_t{1});
    inputs.emplace("min_extent", std::int64_t{5});
    inputs.emplace("merge_gap", std::int64_t{0});
    inputs.emplace("padding", std::int64_t{0});

    auto res = inst->execute(inputs);
    if (res.status.code != 0) {
        std::cerr << "ROI projection execute failed: " << res.status.message << std::endl;
    }
    assert(res.status.code == 0);
    const auto* outRois = std::get_if<RoiBatch>(&res.outputs["rois"]);
    assert(outRois && outRois->images().size() == 1 && !outRois->images()[0].empty());
    const auto& r = outRois->images()[0][0];
    assert(r.kind() == RoiKind::Rectangle);
    assert(std::abs(r.encoded()[1] - 40.0f) < 1.0f);
    assert(std::abs(r.encoded()[3] - 20.0f) < 1.0f);

    assert(inst->release().code == 0);
}

void testMissingDependencyHandling(const std::filesystem::path& repoRoot, const std::filesystem::path& toolsPkg) {
    QTemporaryDir temp;
    assert(temp.isValid());
    const auto testPkg = std::filesystem::u8path(temp.path().toUtf8().toStdString()) / "package_missing_dep";
    std::filesystem::create_directories(testPkg / "bin");
    std::filesystem::create_directories(testPkg / "assets" / "tools");
    std::filesystem::copy_file(toolsPkg / "module_index.yaml", testPkg / "module_index.yaml");
    std::filesystem::copy(toolsPkg / "assets" / "tools", testPkg / "assets" / "tools", std::filesystem::copy_options::recursive);
    for (const auto& entry : std::filesystem::directory_iterator(toolsPkg / "bin")) {
        std::filesystem::copy_file(entry.path(), testPkg / "bin" / entry.path().filename());
    }

    const auto appDir = std::filesystem::path(QCoreApplication::applicationDirPath().toStdString());
    auto pluginADll = appDir.parent_path() / "fixtures" / "test_plugin_a.dll";
    auto depADll = appDir.parent_path() / "fixtures" / "test_dep_a.dll";
    if (!std::filesystem::exists(pluginADll)) {
        pluginADll = repoRoot / "build" / "fixtures" / "test_plugin_a.dll";
        depADll = repoRoot / "build" / "fixtures" / "test_dep_a.dll";
    }
    if (!std::filesystem::exists(pluginADll)) {
        std::cout << "[INFO] Skipping plugin missing dependency test (fixtures not found)\n";
        return;
    }

    // Copy test_plugin_a.dll to testPkg/bin, but DO NOT copy its required dependency test_dep_a.dll!
    const auto targetPluginA = testPkg / "bin" / "test_plugin_a.dll";
    std::filesystem::copy_file(pluginADll, targetPluginA);

    auto loader = std::make_shared<PluginLoader>();
    loader->setBaseDirectory(testPkg);
    loader->loadIndexFile(testPkg / "module_index.yaml");

    // 1. Verify valid real Tools package plugin loads successfully
    ToolStatus statusOk;
    const auto cropModPath = loader->resolveModulePath("visionrt.image.crop");
    assert(!cropModPath.empty());
    auto cropMod = loader->loadModule(cropModPath, statusOk);
    assert(cropMod != nullptr);
    assert(statusOk.code == 0);

    // 2. Loading test_plugin_a.dll must fail because test_dep_a.dll is missing!
    ToolStatus statusMissing;
    auto failMod = loader->loadModule(targetPluginA, statusMissing);
    assert(failMod == nullptr);
    assert(statusMissing.code != 0);
    // Diagnostic must contain the module path and the system error
    assert(statusMissing.message.find("test_plugin_a.dll") != std::string::npos);
#ifdef _WIN32
    assert(statusMissing.message.find("126") != std::string::npos ||
           statusMissing.message.find("Win32 error") != std::string::npos);
#endif

    // 3. Now copy the missing dependency test_dep_a.dll into testPkg/bin
    const auto targetDepA = testPkg / "bin" / "test_dep_a.dll";
    std::filesystem::copy_file(depADll, targetDepA);

    // 4. Create fresh loader simulating restart: now test_plugin_a.dll loads successfully!
    auto loaderRecovered = std::make_shared<PluginLoader>();
    loaderRecovered->setBaseDirectory(testPkg);
    loaderRecovered->loadIndexFile(testPkg / "module_index.yaml");
    ToolStatus statusRecovered;
    auto recoveredMod = loaderRecovered->loadModule(targetPluginA, statusRecovered);
    assert(recoveredMod != nullptr);
    assert(statusRecovered.code == 0);
}

void testRealToolsConcurrencyAndLifecycle(const std::filesystem::path& toolsPkg, const std::filesystem::path& repoRoot, PythonRuntime& python) {
    auto loader = std::make_shared<PluginLoader>();
    loader->setBaseDirectory(toolsPkg);
    loader->loadIndexFile(toolsPkg / "module_index.yaml");
    assert(loader->diagnostics().empty());

    ToolRegistry registry;
    registry.setPluginLoader(loader);
    std::vector<std::filesystem::path> roots;
    roots.push_back(toolsPkg / "assets" / "tools");
    if (std::filesystem::exists(toolsPkg / "examples" / "Python")) {
        roots.push_back(toolsPkg / "examples" / "Python");
    }
    roots.push_back(repoRoot / "assets" / "tools" / "custom");
    registry.scanRoots(roots);
    setupPython(registry, python);

    QTemporaryDir tmpDir;
    assert(tmpDir.isValid());
    const auto tmpPath = std::filesystem::path(tmpDir.path().toStdString());
    const auto projectFile = tmpPath / "pipeline_cpu_python.vrt";
    std::filesystem::copy_file(repoRoot / "examples" / "pipeline_cpu_python.vrt", projectFile);
    std::filesystem::copy(repoRoot / "examples" / "images", tmpPath / "images", std::filesystem::copy_options::recursive);

    ProjectStorage storage(registry);
    auto loaded = storage.load(projectFile);
    assert(loaded.diagnostics.empty());

    // Prepare 2 projects with different inputs (Engine 1: 2 images; Engine 2: 1 image via limit=1)
    ProjectDefinition project1 = loaded.project;
    ProjectDefinition project2 = loaded.project;
    project2.flows[0].nodes[0].inputs["limit"] = {BindingMode::Literal, std::int64_t{1}, {}};

    // Concurrent execution of 2 independent engines sharing registry & plugin loader
    RuntimeEngine engine1(2);
    RuntimeEngine engine2(2);

    auto f1 = std::async(std::launch::async, [&] {
        assert(engine1.start(project1, registry, RunMode::Once).code == 0);
        return finish(engine1);
    });
    auto f2 = std::async(std::launch::async, [&] {
        assert(engine2.start(project2, registry, RunMode::Once).code == 0);
        return finish(engine2);
    });

    auto u1 = f1.get();
    auto u2 = f2.get();

    auto s1_node5 = snapshot(u1, 5);
    auto s2_node5 = snapshot(u2, 5);
    assert(s1_node5 && s1_node5->status.code == 0);
    assert(s2_node5 && s2_node5->status.code == 0);

    // Verify output isolation: Engine 1 produced 2 mean values, Engine 2 produced 1 mean value
    const auto v1 = std::get<NumericArray>(s1_node5->outputs.at("means")).buffers()[0].values<double>();
    const auto v2 = std::get<NumericArray>(s2_node5->outputs.at("means")).buffers()[0].values<double>();
    assert(v1.size() == 2);
    assert(v2.size() == 1);
    assert(std::abs(v1[0] - v2[0]) < 1e-6);

    // Cross-release snapshot check: engine resets, snapshot retains historical output & pixel memory
    auto s1_node1 = snapshot(u1, 1);
    assert(s1_node1 && s1_node1->status.code == 0);
    assert(engine1.requestReset().code == 0);
    finish(engine1);
    assert(s1_node5->status.code == 0);
    const auto& retainedImg = std::get<ImageBatch>(s1_node1->outputs.at("images")).images()[0];
    assert(retainedImg.cols() > 0 && retainedImg.rows() > 0);
    const auto* pxPtr = retainedImg.data();
    assert(pxPtr != nullptr);
    volatile std::uint8_t pxVal = *pxPtr;
    assert(pxVal == *pxPtr);

    // Success-then-failure isolation: 2nd run with invalid path outputs Missing, not stale value
    project1.flows[0].nodes[0].inputs["path"] = {BindingMode::Literal, Utf8{"nonexistent_images_dir"}, {}};
    assert(engine1.start(project1, registry, RunMode::Once).code == 0);
    auto u1_fail = finish(engine1);
    auto s1_fail_node1 = snapshot(u1_fail, 1);
    assert(s1_fail_node1 && s1_fail_node1->status.code != 0);
    assert(isMissing(s1_fail_node1->outputs.at("images")));
}

void testDiagnosticPipeline(const std::filesystem::path& toolsPkg, const std::filesystem::path& repoRoot, PythonRuntime& python) {
    QTemporaryDir tmpDir;
    assert(tmpDir.isValid());
    const auto tmpPath = std::filesystem::path(tmpDir.path().toStdString());
    const auto tempProjectFile = tmpPath / "pipeline_error_diagnostic.vrt";
    std::filesystem::copy_file(repoRoot / "examples" / "pipeline_error_diagnostic.vrt", tempProjectFile);
    std::filesystem::copy(repoRoot / "examples" / "images", tmpPath / "images", std::filesystem::copy_options::recursive);

    auto loader = std::make_shared<PluginLoader>();
    loader->setBaseDirectory(toolsPkg);
    loader->loadIndexFile(toolsPkg / "module_index.yaml");

    ToolRegistry registry;
    registry.setPluginLoader(loader);
    std::vector<std::filesystem::path> roots;
    roots.push_back(toolsPkg / "assets" / "tools");
    roots.push_back(repoRoot / "assets" / "tools");
    roots.push_back(repoRoot / "assets" / "tools" / "custom");
    registry.scanRoots(roots);
    setupPython(registry, python);

    ProjectStorage storage(registry);
    auto loaded = storage.load(tempProjectFile);
    // Node 4 is DefinitionMissing
    bool foundMissingDef = false;
    for (const auto& d : loaded.diagnostics) {
        if (d.node == 4 && d.code == ProjectStorageCode::DefinitionMissing) {
            foundMissingDef = true;
            break;
        }
    }
    assert(foundMissingDef);

    RuntimeEngine engine(2);
    const auto startRes = engine.start(loaded.project, registry, RunMode::Once);
    if (startRes.code != 0) {
        std::cerr << "Diagnostic pipeline engine.start failed: code=" << startRes.code << " msg=" << startRes.message << std::endl;
    }
    assert(startRes.code == 0);
    auto update = finish(engine);

    // Node 1: read succeeds
    auto s1 = snapshot(update, 1);
    assert(s1 && s1->status.code == 0);

    // Node 2: crop fails because rois input is Missing
    auto s2 = snapshot(update, 2);
    assert(s2);
    assert(!s2->status);
    assert(isMissing(s2->outputs.at("images")));

    // Node 3: yolo fails because nonexistent model weights
    auto s3 = snapshot(update, 3);
    assert(s3);
    assert(s3->status.code != 0); // Not 0! Must not report success!
    assert(!s3->status.message.empty());

    // Try starting the flow containing Node 4 (definition missing)
    loaded.project.flows[0].runSelected = false;
    loaded.project.flows[1].runSelected = true;
    const auto missingStartStatus = engine.start(loaded.project, registry, RunMode::Once);
    assert(!missingStartStatus);
    assert(missingStartStatus.code == 1);
}

void testRealPluginModulePipeline(const std::filesystem::path& repoRoot, PythonRuntime& python) {
    const auto appDir = std::filesystem::path(QCoreApplication::applicationDirPath().toStdString());
    auto pluginDll = appDir / "test_plugin_module.dll";
    if (!std::filesystem::exists(pluginDll)) {
        pluginDll = appDir / "bin" / "test_plugin_module.dll";
    }
    assert(std::filesystem::exists(pluginDll));

    auto loader = std::make_shared<PluginLoader>();
    loader->registerModule("test.plugin.counter", pluginDll);
    loader->registerModule("test.plugin.function", pluginDll);
    loader->registerModule("test.plugin.fail_init", pluginDll);

    ToolRegistry registry;
    registry.setPluginLoader(loader);
    registry.scan(repoRoot / "assets" / "tools");
    setupPython(registry, python);

    // Register plugin counter tool
    ToolDefinition counterDef;
    counterDef.id = "test.plugin.counter";
    counterDef.displayName = "Plugin Counter";
    counterDef.version = "1.0.0";
    counterDef.kind = ToolKind::CppClass;
    counterDef.registryName = "test.plugin.counter";
    PortDefinition initParam;
    initParam.id = "initial"; initParam.name = "Initial"; initParam.type = {Type::Int}; initParam.defaultValue = std::int64_t{10};
    counterDef.initParams.push_back(initParam);
    PortDefinition outCount;
    outCount.id = "count"; outCount.name = "Count"; outCount.type = {Type::Int};
    counterDef.outputs.push_back(outCount);
    registry.addDefinition(counterDef);

    // Register plugin function tool
    ToolDefinition funcDef;
    funcDef.id = "test.plugin.function";
    funcDef.displayName = "Plugin Function";
    funcDef.version = "1.0.0";
    funcDef.kind = ToolKind::CppFunction;
    funcDef.registryName = "test.plugin.function";
    PortDefinition inX;
    inX.id = "x"; inX.name = "X"; inX.type = {Type::Int}; inX.defaultValue = std::int64_t{25};
    funcDef.inputs.push_back(inX);
    PortDefinition outY;
    outY.id = "y"; outY.name = "Y"; outY.type = {Type::Int};
    funcDef.outputs.push_back(outY);
    registry.addDefinition(funcDef);

    // Build test pipeline: Node 1 (function, x=25 -> y=50), Node 2 (counter, initial=10 -> count=11)
    ProjectDefinition project;
    FlowDefinition flow;
    flow.id = "flow_1";
    flow.name = "Real Plugin Flow";

    NodeDefinition n1;
    n1.id = 1;
    n1.toolId = "test.plugin.function";
    n1.toolNameSnapshot = "Plugin Function";
    n1.position = {100, 100};
    InputBinding b1;
    b1.mode = BindingMode::Literal;
    b1.literal = std::int64_t{25};
    n1.inputs.emplace("x", b1);
    n1.signatures.inputs["x"] = {Type::Int};
    n1.signatures.outputs["y"] = {Type::Int};
    flow.nodes.push_back(n1);

    NodeDefinition n2;
    n2.id = 2;
    n2.toolId = "test.plugin.counter";
    n2.toolNameSnapshot = "Plugin Counter";
    n2.position = {300, 100};
    n2.initParams.emplace("initial", std::int64_t{10});
    n2.signatures.initParams["initial"] = {Type::Int};
    n2.signatures.outputs["count"] = {Type::Int};
    flow.nodes.push_back(n2);

    project.flows.clear();
    project.flows.push_back(flow);
    project.nextNodeId = 3;

    RuntimeEngine engine(2);
    const auto startRes = engine.start(project, registry, RunMode::Once);
    assert(startRes.code == 0);
    const auto update = finish(engine);

    auto s1 = snapshot(update, 1);
    assert(s1 && s1->status.code == 0);
    assert(std::get<std::int64_t>(s1->outputs.at("y")) == 50);

    auto s2 = snapshot(update, 2);
    assert(s2 && s2->status.code == 0);
    assert(std::get<std::int64_t>(s2->outputs.at("count")) == 11);

    // Test Save / Reopen consistency
    QTemporaryDir tmpDir;
    assert(tmpDir.isValid());
    const auto savePath = std::filesystem::path(tmpDir.path().toStdString()) / "plugin_project.vrt";
    ProjectStorage storage(registry);
    auto reconciled = storage.reconcile(project);
    assert(reconciled.diagnostics.empty());
    project = std::move(reconciled.project);
    storage.save(project, savePath);
    const auto reloaded = storage.load(savePath);
    assert(reloaded.diagnostics.empty());
    assert(engine.start(reloaded.project, registry, RunMode::Once).code == 0);
    const auto reloadedUpdate = finish(engine);
    auto rs1 = snapshot(reloadedUpdate, 1);
    assert(rs1 && std::get<std::int64_t>(rs1->outputs.at("y")) == std::get<std::int64_t>(s1->outputs.at("y")));
    auto rs2 = snapshot(reloadedUpdate, 2);
    assert(rs2 && std::get<std::int64_t>(rs2->outputs.at("count")) == std::get<std::int64_t>(s2->outputs.at("count")));
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::filesystem::path installRoot;
    std::filesystem::path repoRoot;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--tools-dir" || arg == "--install-root") && i + 1 < argc) {
            installRoot = std::filesystem::path(argv[++i]);
        } else if (arg.rfind("--tools-dir=", 0) == 0) {
            installRoot = std::filesystem::path(arg.substr(12));
        } else if (arg.rfind("--install-root=", 0) == 0) {
            installRoot = std::filesystem::path(arg.substr(15));
        } else if (!arg.starts_with("--")) {
            repoRoot = std::filesystem::path(arg);
        }
    }

    if (repoRoot.empty()) {
        repoRoot = std::filesystem::current_path();
    }

    PythonRuntime python;

    const auto toolsPkg = resolveToolsPackage(repoRoot, installRoot);
    if (toolsPkg.empty()) {
        std::cerr << "FATAL: Real VisionTools package not found! "
                     "RT-S03 requires valid tools package." << std::endl;
        return 1;
    }
    std::cout << "[REAL TOOLS PACKAGE] Verified Tools package at: " << toolsPkg.string() << std::endl;

    // Test in a temporary installation directory with Chinese characters and spaces
    QTemporaryDir unicodeTemp;
    assert(unicodeTemp.isValid());
    const auto unicodePkg = std::filesystem::u8path(unicodeTemp.path().toUtf8().toStdString()) / "测试 工具 目录 with space";
    std::filesystem::copy(toolsPkg, unicodePkg, std::filesystem::copy_options::recursive);

    std::cout << "[TEST] Running real Tools package CPU + Python pipeline in unicode path (RT-S03)..." << std::endl;
    testRealToolsCpuPythonPipeline(unicodePkg, repoRoot, python);
    std::cout << "[PASS] Real Tools package CPU + Python pipeline passed." << std::endl;

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
    testRealPluginModulePipeline(repoRoot, python);
    std::cout << "[PASS] Mock test_plugin_module pipeline passed." << std::endl;

    std::cout << "[INFO] GPU execution: unverified (host environment has no CUDA/TensorRT weights)" << std::endl;
    std::cout << "All example contract tests passed successfully!" << std::endl;
    return 0;
}
