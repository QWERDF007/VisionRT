#include "core/ProjectStorage.h"
#include "core/ToolRegistry.h"
#include "plugins/PluginLoader.h"
#include "python/PythonRuntime.h"
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

RuntimeUpdate finish(RuntimeEngine &engine)
{
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto update = engine.pollLatest();
        if (update.completion)
        {
            assert((engine.acknowledgeCompletion(*update.completion)));
            return update;
        }
        std::this_thread::sleep_for(2ms);
    }
    throw std::runtime_error("runtime completion deadline exceeded");
}

ResultSlot::Snapshot snapshot(const RuntimeUpdate &update, NodeId id)
{
    for (const auto &item : update.nodes)
    {
        if (item.node == id)
            return item.snapshot;
    }
    throw std::runtime_error("node update absent: " + std::to_string(id));
}

void setupPython(ToolRegistry &registry, PythonRuntime &runtime)
{
    std::vector<ToolDefinition> pyDefs;
    for (const auto &d : registry.definitions())
    {
        if (d.kind == ToolKind::PythonClass || d.kind == ToolKind::PythonFunction)
        {
            pyDefs.push_back(d);
        }
    }
    const auto loadedList = runtime.refresh(pyDefs);
    for (const auto &loaded : loadedList)
    {
        registry.setPythonImplementation(loaded.toolId, runtime.factory(*registry.find(loaded.toolId)), loaded.status);
    }
}

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
    const auto tempProjectFile = tmpPath / "pipeline_cpu_python.vrt.json";
    std::filesystem::copy_file(repoRoot / "examples" / "pipeline_cpu_python.vrt.json", tempProjectFile);
    std::filesystem::copy(repoRoot / "examples" / "images", tmpPath / "images",
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
    const auto &images1 = std::get<DataRef<Image>>(s1->outputs.at("images"));
    assert((images1.get().size() >= 2));

    auto s2 = snapshot(update, 2);
    assert((s2 && s2->status.code == 0));
    const auto &rois2 = std::get<DataRef<ROI>>(s2->outputs.at("rois"));
    assert((rois2.get().size() == images1.get().size()));

    auto s3 = snapshot(update, 3);
    assert((s3 && s3->status.code == 0));
    const auto &croppedImages = std::get<DataRef<Image>>(s3->outputs.at("images"));
    assert((croppedImages.get().size() == images1.get().size()));
    for (const auto &image : croppedImages.get()) assert((image.rows == 40 && image.cols == 20));
    assert((!s3->outputs.contains("source_indices")));

    auto s4 = snapshot(update, 4);
    assert((s4 && s4->status.code == 0));
    const auto &preprocessed = std::get<DataRef<Image>>(s4->outputs.at("images"));
    assert((preprocessed.get().size() == croppedImages.get().size()));

    // Node 5: python mean
    auto s5 = snapshot(update, 5);
    assert((s5 && s5->status.code == 0));
    const auto &means = std::get<Array>(s5->outputs.at("means"));
    assert(((means.type() == TypeSpec{Type::Array, DType::Float64, 1})));
    assert((means.get<std::vector<double>>().size() == static_cast<std::int64_t>(images1.get().size())));
    const auto values = means.get<std::vector<double>>();
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
    const auto &reloadedMeans  = std::get<Array>(reloadedS5->outputs.at("means"));
    const auto  reloadedValues = reloadedMeans.get<std::vector<double>>();
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
        Array          emptyScores(NestedVector<float, 2>{{}});
        Array          emptyClasses(NestedVector<std::int64_t, 2>{{}});
        Array          emptyNames(NestedVector<std::string, 2>{std::vector<std::string>{}});
        Array          allowed(std::vector<std::int64_t>{});

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
        assert((res.status.code == 0));
        const auto *outCounts = std::get_if<Array>(&res.outputs["counts"]);
        assert((outCounts && !outCounts->get<NestedVector<std::int64_t, 1>>().empty()));
        assert((outCounts->get<NestedVector<std::int64_t, 1>>()[0] == 0));

        const auto *outRois = std::get_if<DataRef<ROI>>(&res.outputs["rois"]);
        assert((outRois && outRois->get().size() == 1 && outRois->get()[0].empty()));

        const auto *outScores = std::get_if<Array>(&res.outputs["scores"]);
        assert((outScores && !outScores->get<NestedVector<float, 2>>().empty()));
        assert((outScores->get<NestedVector<float, 2>>()[0].empty()));

        const auto *outClasses = std::get_if<Array>(&res.outputs["classes"]);
        assert((outClasses && !outClasses->get<NestedVector<std::int64_t, 2>>().empty()));
        assert((outClasses->get<NestedVector<std::int64_t, 2>>()[0].empty()));

        const auto *outNames = std::get_if<Array>(&res.outputs["class_names"]);
        assert((outNames && !outNames->get<NestedVector<std::string, 2>>().empty()));
        assert((outNames->get<NestedVector<std::string, 2>>()[0].empty()));
    }

    // 2. Scenario: 3 distinct targets (different class, name, score, coords, angle)
    // Target 0: (10, 10, 20, 20, 5),  score=0.20, class=10, name="scratch" -> filtered out (< 0.5)
    // Target 1: (30, 30, 40, 40, 15), score=0.70, class=20, name="crack"   -> passes
    // Target 2: (50, 50, 60, 60, 30), score=0.95, class=30, name="dent"    -> passes
    {
        cv::Mat          mat(100, 100, CV_8UC3, cv::Scalar(0, 0, 0));
        DataRef<Image>   imgBatch(Image{(mat)});
        ROI::value_type  rois3;
        rois3.push_back(std::vector<double>{2, 10.0f, 10.0f, 20.0f, 20.0f, 5.0f});
        rois3.push_back(std::vector<double>{2, 30.0f, 30.0f, 40.0f, 40.0f, 15.0f});
        rois3.push_back(std::vector<double>{2, 50.0f, 50.0f, 60.0f, 60.0f, 30.0f});
        DataRef<ROI> roiBatch(ROI{rois3});

        std::vector<float>        scoreVals = {0.20f, 0.70f, 0.95f};
        std::vector<std::int64_t> classVals = {10, 20, 30};
        std::vector<std::string>  nameVals  = {"scratch", "crack", "dent"};

        Array scores(NestedVector<float, 2>{scoreVals});
        Array classes(NestedVector<std::int64_t, 2>{classVals});
        Array names(NestedVector<std::string, 2>{nameVals});
        Array allowed(std::vector<std::int64_t>{});

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
        assert((res.status.code == 0));
        const auto *outCounts = std::get_if<Array>(&res.outputs["counts"]);
        assert((outCounts && !outCounts->get<NestedVector<std::int64_t, 1>>().empty()));
        assert((outCounts->get<NestedVector<std::int64_t, 1>>()[0] == 2));

        const auto *outRois = std::get_if<DataRef<ROI>>(&res.outputs["rois"]);
        assert((outRois && outRois->get().size() == 1 && outRois->get()[0].size() == 2));

        // Target 1 ROI geometric fields
        const auto &r1 = outRois->get()[0][0];
        assert((roiKind(r1) == RoiKind::Rectangle));
        assert((r1.size() == 6));
        assert((r1[0] == static_cast<float>(RoiKind::Rectangle)));
        assert((std::abs(r1[1] - 30.0f) < 1e-4f)); // x
        assert((std::abs(r1[2] - 30.0f) < 1e-4f)); // y
        assert((std::abs(r1[3] - 40.0f) < 1e-4f)); // width
        assert((std::abs(r1[4] - 40.0f) < 1e-4f)); // height
        assert((std::abs(r1[5] - 15.0f) < 1e-4f)); // angle
        const auto corners1 = rectangleCorners(r1);
        assert((corners1.size() == 4));

        // Target 2 ROI geometric fields
        const auto &r2 = outRois->get()[0][1];
        assert((roiKind(r2) == RoiKind::Rectangle));
        assert((r2.size() == 6));
        assert((r2[0] == static_cast<float>(RoiKind::Rectangle)));
        assert((std::abs(r2[1] - 50.0f) < 1e-4f)); // x
        assert((std::abs(r2[2] - 50.0f) < 1e-4f)); // y
        assert((std::abs(r2[3] - 60.0f) < 1e-4f)); // width
        assert((std::abs(r2[4] - 60.0f) < 1e-4f)); // height
        assert((std::abs(r2[5] - 30.0f) < 1e-4f)); // angle
        const auto corners2 = rectangleCorners(r2);
        assert((corners2.size() == 4));

        const auto *outScores = std::get_if<Array>(&res.outputs["scores"]);
        assert((outScores && !outScores->get<NestedVector<float, 2>>().empty()));
        const auto outScoreVals = outScores->get<NestedVector<float, 2>>()[0];
        assert((outScoreVals.size() == 2));
        assert((std::abs(outScoreVals[0] - 0.70f) < 1e-4f));
        assert((std::abs(outScoreVals[1] - 0.95f) < 1e-4f));

        const auto *outClasses = std::get_if<Array>(&res.outputs["classes"]);
        assert((outClasses && !outClasses->get<NestedVector<std::int64_t, 2>>().empty()));
        const auto outClassVals = outClasses->get<NestedVector<std::int64_t, 2>>()[0];
        assert((outClassVals.size() == 2));
        assert((outClassVals[0] == 20));
        assert((outClassVals[1] == 30));

        const auto *outNames = std::get_if<Array>(&res.outputs["class_names"]);
        assert((outNames && !outNames->get<NestedVector<std::string, 2>>().empty()));
        const auto &outNameVals = outNames->get<NestedVector<std::string, 2>>()[0];
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
    assert((def->id == "visiontools.preprocess.adaptive_binary"));
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
    inputs.emplace("images", std::move(imgBatch));
    inputs.emplace("block_size", std::int64_t{3});
    inputs.emplace("constant_offset", 5.0);

    auto res = inst->execute(inputs);
    assert((res.status.code == 0));
    const auto *outImages = std::get_if<DataRef<Image>>(&res.outputs["images"]);
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
    inputs.emplace("images", std::move(imgBatch));
    inputs.emplace("axis", std::string{"x"});
    inputs.emplace("polarity", std::string{"bright"});
    inputs.emplace("threshold", std::int64_t{128});
    inputs.emplace("min_pixels", std::int64_t{1});
    inputs.emplace("min_extent", std::int64_t{5});
    inputs.emplace("merge_gap", std::int64_t{0});
    inputs.emplace("padding", std::int64_t{0});

    auto res = inst->execute(inputs);
    if (res.status.code != 0)
    {
        std::cerr << "ROI projection execute failed: " << res.status.message << std::endl;
    }
    assert((res.status.code == 0));
    const auto *outRois = std::get_if<DataRef<ROI>>(&res.outputs["rois"]);
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
    const auto projectFile = tmpPath / "pipeline_cpu_python.vrt.json";
    std::filesystem::copy_file(repoRoot / "examples" / "pipeline_cpu_python.vrt.json", projectFile);
    std::filesystem::copy(repoRoot / "examples" / "images", tmpPath / "images",
                          std::filesystem::copy_options::recursive);

    ProjectStorage storage(registry);
    auto           loaded = storage.load(projectFile);
    assert((loaded.diagnostics.empty()));

    // Prepare 2 projects with different inputs (Engine 1: 2 images; Engine 2: 1 image via limit=1)
    ProjectDefinition project1                 = loaded.project;
    ProjectDefinition project2                 = loaded.project;
    project2.flows[0].nodes[0].inputs["limit"] = {BindingMode::Literal, std::int64_t{1}, {}};

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
    const auto v1 = std::get<Array>(s1_node5->outputs.at("means")).get<std::vector<double>>();
    const auto v2 = std::get<Array>(s2_node5->outputs.at("means")).get<std::vector<double>>();
    assert((v1.size() == 2));
    assert((v2.size() == 1));
    assert((std::abs(v1[0] - v2[0]) < 1e-6));

    // Cross-release snapshot check: engine resets, snapshot retains historical output & pixel memory
    auto s1_node1 = snapshot(u1, 1);
    assert((s1_node1 && s1_node1->status.code == 0));
    assert((engine1.requestReset().code == 0));
    finish(engine1);
    assert((s1_node5->status.code == 0));
    const auto &retainedImg = std::get<DataRef<Image>>(s1_node1->outputs.at("images")).get()[0];
    assert((retainedImg.cols > 0 && retainedImg.rows > 0));
    const auto *pxPtr = retainedImg.data;
    assert((pxPtr != nullptr));
    volatile std::uint8_t pxVal = *pxPtr;
    assert((pxVal == *pxPtr));

    // Success-then-failure isolation: 2nd run with invalid path outputs Missing, not stale value
    project1.flows[0].nodes[0].inputs["path"] = {BindingMode::Literal, std::string{"nonexistent_images_dir"}, {}};
    assert((engine1.start(project1, registry, RunMode::Once).code == 0));
    auto u1_fail       = finish(engine1);
    auto s1_fail_node1 = snapshot(u1_fail, 1);
    assert((s1_fail_node1 && s1_fail_node1->status.code != 0));
    assert((isMissing(s1_fail_node1->outputs.at("images"))));
}

void testDiagnosticPipeline(const std::filesystem::path &toolsPkg, const std::filesystem::path &repoRoot,
                            PythonRuntime &python)
{
    QTemporaryDir tmpDir;
    assert((tmpDir.isValid()));
    const auto tmpPath         = std::filesystem::u8path(tmpDir.path().toUtf8().toStdString());
    const auto tempProjectFile = tmpPath / "pipeline_error_diagnostic.vrt.json";
    std::filesystem::copy_file(repoRoot / "examples" / "pipeline_error_diagnostic.vrt.json", tempProjectFile);
    std::filesystem::copy(repoRoot / "examples" / "images", tmpPath / "images",
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
    assert((isMissing(s2->outputs.at("images"))));

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
    counterDef.id           = "test.plugin.counter";
    counterDef.displayName  = "Plugin Counter";
    counterDef.version      = "1.0.0";
    counterDef.kind         = ToolKind::Cpp;

    PortDefinition initParam;
    initParam.id           = "initial";
    initParam.name         = "Initial";
    initParam.type         = {Type::Int};
    initParam.defaultValue = std::int64_t{10};
    counterDef.initParams.push_back(initParam);
    PortDefinition outCount;
    outCount.id   = "count";
    outCount.name = "Count";
    outCount.type = {Type::Int};
    counterDef.outputs.push_back(outCount);
    registry.addDefinition(counterDef);

    // Register plugin function tool
    ToolDefinition funcDef;
    funcDef.id           = "test.plugin.function";
    funcDef.displayName  = "Plugin Function";
    funcDef.version      = "1.0.0";
    funcDef.kind         = ToolKind::Cpp;

    PortDefinition inX;
    inX.id           = "x";
    inX.name         = "X";
    inX.type         = {Type::Int};
    inX.defaultValue = std::int64_t{25};
    funcDef.inputs.push_back(inX);
    PortDefinition outY;
    outY.id   = "y";
    outY.name = "Y";
    outY.type = {Type::Int};
    funcDef.outputs.push_back(outY);
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
    n1.inputs.emplace("x", b1);
    n1.signatures.inputs["x"]  = {Type::Int};
    n1.signatures.outputs["y"] = {Type::Int};
    flow.nodes.push_back(n1);

    NodeDefinition n2;
    n2.id               = 2;
    n2.toolId           = "test.plugin.counter";
    n2.toolNameSnapshot = "Plugin Counter";
    n2.position         = {300, 100};
    n2.initParams.emplace("initial", std::int64_t{10});
    n2.signatures.initParams["initial"] = {Type::Int};
    n2.signatures.outputs["count"]      = {Type::Int};
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
    assert((std::get<std::int64_t>(s1->outputs.at("y")) == 50));

    auto s2 = snapshot(update, 2);
    assert((s2 && s2->status.code == 0));
    assert((std::get<std::int64_t>(s2->outputs.at("count")) == 11));

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
    assert((rs1 && std::get<std::int64_t>(rs1->outputs.at("y")) == std::get<std::int64_t>(s1->outputs.at("y"))));
    auto rs2 = snapshot(reloadedUpdate, 2);
    assert(
        (rs2 && std::get<std::int64_t>(rs2->outputs.at("count")) == std::get<std::int64_t>(s2->outputs.at("count"))));
}

void testOfflinePackageReplacementAndProtection(const std::filesystem::path & /*repoRoot*/, PythonRuntime &python)
{
    std::cout << "[TEST] Running offline package replacement and failure protection test (Ticket 06 / T-08 / T-09)..."
              << std::endl;

    const auto appDir    = std::filesystem::u8path(QCoreApplication::applicationDirPath().toUtf8().toStdString());
    auto       pluginDll = appDir / "test_plugin_module.dll";
    if (!std::filesystem::exists(pluginDll))
    {
        pluginDll = appDir / "bin" / "test_plugin_module.dll";
    }
    assert((std::filesystem::exists(pluginDll)));

    // 1. Create temporary deployment root with Chinese characters and spaces
    QTemporaryDir deployTemp;
    assert((deployTemp.isValid()));
    const auto installRoot
        = std::filesystem::u8path(deployTemp.path().toUtf8().toStdString()) / "测试 部署 路径 with space";
    const auto toolsPackageDir = installRoot / "tools-package";
    std::filesystem::create_directories(toolsPackageDir / "bin");
    std::filesystem::create_directories(toolsPackageDir / "tools" / "builtin");

    // Copy plugin DLL to bin
    std::filesystem::copy_file(pluginDll, toolsPackageDir / "bin" / "test_plugin_module.dll");

    // Write C++ tool declaration (func.json)
    {
        std::ofstream ofs(toolsPackageDir / "tools" / "builtin" / "func.json");
        ofs << R"json({
  "schema_version": 3,
  "tool_id": "test.plugin.function",
  "display_name": "Plugin Function",
  "version": "1.0.0",
  "kind": "cpp",
  "init_params": [],
  "inputs": [
    {"id": "x", "name": "X", "type": "Int", "default": 10}
  ],
  "outputs": [
    {"id": "y", "name": "Y", "type": "Int"}
  ]
})json";
    }

    // Write Python tool declaration and script (calc.py, calc.json)
    {
        std::filesystem::create_directories(toolsPackageDir / "python" / "builtin" / "calc");
        std::ofstream py(toolsPackageDir / "python" / "builtin" / "calc" / "calc.py");
        py << "def execute(inputs):\n"
           << "    return {'res': int(inputs['val']) + 5, '__status_code': 0, '__status_message': 'ok'}\n";

        std::ofstream ofs(toolsPackageDir / "tools" / "builtin" / "calc.json");
        ofs << R"json({
  "schema_version": 3,
  "tool_id": "custom.py_math",
  "display_name": "Python Calc",
  "version": "1.0.0",
  "kind": "python_function",
  "entry": {"package": "calc", "file": "calc.py", "symbol": "execute"},
  "init_params": [],
  "inputs": [
    {"id": "val", "name": "Val", "type": "Int"}
  ],
  "outputs": [
    {"id": "res", "name": "Res", "type": "Int"}
  ]
})json";
    }

    // Write Platform Receiver node declaration (recv.json)
    {
        std::ofstream ofs(toolsPackageDir / "tools" / "builtin" / "recv.json");
        ofs << R"json({
  "schema_version": 3,
  "tool_id": "platform.test_recv",
  "display_name": "Test Receiver",
  "version": "1.0.0",
  "kind": "host",
  "init_params": [
    {"id": "source_flow_id", "name": "Source Flow", "type": "String", "default": ""},
    {"id": "source_node_seq", "name": "Source Node", "type": "String", "default": ""},
    {"id": "source_port_id", "name": "Source Port", "type": "String", "default": ""},
    {"id": "timeout_ms", "name": "Timeout", "type": "Int", "default": 1000}
  ],
  "inputs": [],
  "outputs": [
    {"id": "value", "name": "Value", "type": "Int"},
    {"id": "source_status_code", "name": "Source Status Code", "type": "Int"},
    {"id": "source_status_message", "name": "Source Status Message", "type": "String"}
  ],
  "entry": {"binding": "visionrt.receiver"}
})json";
    }

    // Write module_index.json for Package 1
    {
        std::ofstream ofs(toolsPackageDir / "module_index.json");
        ofs << R"json({
  "schema_version": 1,
  "variant": "cpu",
  "modules": [
    {"id": "test_plugin_module", "library": "bin/test_plugin_module.dll"}
  ],
  "tools": [
    {"tool_id": "test.plugin.function", "module": "test_plugin_module", "definition": "tools/builtin/func.json"},
    {"tool_id": "custom.py_math", "definition": "tools/builtin/calc.json"},
    {"tool_id": "platform.test_recv", "definition": "tools/builtin/recv.json"}
  ],
  "runtime_files": []
})json";
    }

    // Add unique Package 1 sentinel file
    const auto pkg1Sentinel = toolsPackageDir / "tools" / "builtin" / "pkg1_sentinel.txt";
    {
        std::ofstream ofs(pkg1Sentinel);
        ofs << "PACKAGE_1_FILE";
    }

    // Saved project path
    const auto savedProjectFile = installRoot / "test_pipeline.vrt.json";

    // 2. Package 1 execution: C++ domain DLL, Python tool, and Platform Receiver node in pipeline
    {
        const auto pkg1    = visionrt::readToolsPackage(toolsPackageDir);
        auto       loader1 = std::make_shared<PluginLoader>();
        ToolRegistry registry1;
        registerReceiveTools(registry1);
        registry1.setPluginLoader(loader1);
        registry1.registerPackage(pkg1);
        registry1.loadNativeImplementations();
        setupPython(registry1, python);

        // Build project:
        // Flow 1 ("sender_flow"):
        //   Node 1 (test.plugin.function, x=10 -> y=20)
        //   Node 2 (custom.py_math, val <- Node 1 y -> res=25)
        // Flow 2 ("receiver_flow"):
        //   Node 3 (platform.test_recv, receives "res" from Node 2 in sender_flow -> value=25)
        ProjectDefinition project;
        project.name       = "Test Pipeline Project";
        project.nextFlowId = 3;

        FlowDefinition f1;
        f1.id          = "sender_flow";
        f1.name        = "sender_flow";
        f1.runSelected = true;

        NodeDefinition n1;
        n1.id               = 1;
        n1.toolId           = "test.plugin.function";
        n1.toolNameSnapshot = "Plugin Function";
        n1.position         = {100, 100};
        InputBinding b1;
        b1.mode    = BindingMode::Literal;
        b1.literal = std::int64_t{10};
        n1.inputs.emplace("x", b1);
        n1.signatures.inputs["x"]  = {Type::Int};
        n1.signatures.outputs["y"] = {Type::Int};
        f1.nodes.push_back(n1);

        NodeDefinition n2;
        n2.id               = 2;
        n2.toolId           = "custom.py_math";
        n2.toolNameSnapshot = "Python Calc";
        n2.position         = {250, 200};
        InputBinding b2;
        b2.mode    = BindingMode::Upstream;
        b2.literal = std::int64_t{0};
        b2.source  = SourcePort{1, "y"};
        n2.inputs.emplace("val", b2);
        n2.signatures.inputs["val"]  = {Type::Int};
        n2.signatures.outputs["res"] = {Type::Int};
        f1.nodes.push_back(n2);
        project.flows.push_back(f1);

        FlowDefinition f2;
        f2.id          = "receiver_flow";
        f2.name        = "receiver_flow";
        f2.runSelected = true;

        NodeDefinition n3;
        n3.id               = 3;
        n3.toolId           = "platform.test_recv";
        n3.toolNameSnapshot = "Test Receiver";
        n3.position         = {100, 250};
        n3.initParams       = {
            { "source_flow_id",         std::string("sender_flow")},
            {"source_node_seq", std::string(std::to_string(n2.id))},
            { "source_port_id",                 std::string("res")},
            {     "timeout_ms",                 std::int64_t{1000}}
        };
        n3.signatures.initParams["source_flow_id"]   = {Type::String};
        n3.signatures.initParams["source_node_seq"]   = {Type::String};
        n3.signatures.initParams["source_port_id"]     = {Type::String};
        n3.signatures.initParams["timeout_ms"]         = {Type::Int};
        n3.signatures.outputs["value"]                 = {Type::Int};
        n3.signatures.outputs["source_status_code"]    = {Type::Int};
        n3.signatures.outputs["source_status_message"] = {Type::String};
        f2.nodes.push_back(n3);
        project.flows.push_back(f2);
        project.nextNodeId = 4;

        RuntimeEngine engine1(2);
        assert((engine1.start(project, registry1, RunMode::Once).code == 0));
        auto update1 = finish(engine1);

        auto s1 = snapshot(update1, 1);
        assert((s1 && s1->status.code == 0));
        assert((std::get<std::int64_t>(s1->outputs.at("y")) == 20));

        auto s2 = snapshot(update1, 2);
        if (!s2 || s2->status.code != 0)
        {
            std::cerr << "Node 2 status failed: code=" << (s2 ? s2->status.code : -999)
                      << " msg=" << (s2 ? s2->status.message : "null") << std::endl;
        }
        assert((s2 && s2->status.code == 0));
        assert((std::get<std::int64_t>(s2->outputs.at("res")) == 25));

        auto s3 = snapshot(update1, 3);
        if (!s3 || s3->status.code != 0)
        {
            std::cerr << "Node 3 status failed: code=" << (s3 ? s3->status.code : -999)
                      << " msg=" << (s3 ? s3->status.message : "null") << std::endl;
        }
        assert((s3 && s3->status.code == 0));
        assert((std::get<std::int64_t>(s3->outputs.at("value")) == 25));
        std::cout << "[Evidence T-08] Pipeline with C++ domain DLL, Python tool, and Receiver node executed with exact "
                     "numerical value (25) in unicode path.\n";

        // Save project
        ProjectStorage storage1(registry1);
        auto           reconciled = storage1.reconcile(project);
        for (const auto &d : reconciled.diagnostics)
        {
            std::cerr << "RECONCILE DIAGNOSTIC: node=" << (d.node ? *d.node : 0) << " code=" << static_cast<int>(d.code)
                      << " field=" << d.field << " msg=" << d.message << std::endl;
        }
        project = std::move(reconciled.project);
        storage1.save(project, savedProjectFile);

        // Reopen and re-execute to verify numerical equality on reload
        auto reloaded1 = storage1.load(savedProjectFile);
        for (const auto &d : reloaded1.diagnostics)
        {
            std::cerr << "RELOAD DIAGNOSTIC: node=" << (d.node ? *d.node : 0) << " code=" << static_cast<int>(d.code)
                      << " field=" << d.field << " msg=" << d.message << std::endl;
        }
        assert((reloaded1.diagnostics.empty()));
        RuntimeEngine engineReload(2);
        assert((engineReload.start(reloaded1.project, registry1, RunMode::Once).code == 0));
        auto updateReload = finish(engineReload);
        auto r_s3         = snapshot(updateReload, 3);
        assert((r_s3 && std::get<std::int64_t>(r_s3->outputs.at("value")) == 25));
        std::cout << "[Evidence T-08] Save / reopen consistency and numerical equality verified.\n";
    }

    // 3. Test failure protection: Attempt deploying invalid package (corrupt / missing referenced files)
    // Existing installation must not be altered or corrupted!
    QTemporaryDir badPkgTemp;
    const auto    badPkgDir = std::filesystem::u8path(badPkgTemp.path().toUtf8().toStdString()) / "bad_pkg";
    std::filesystem::create_directories(badPkgDir / "tools" / "builtin");
    {
        std::ofstream ofs(badPkgDir / "module_index.json");
        ofs << R"json({
  "schema_version": 1,
  "variant": "cpu",
  "modules": [
    {"id": "nonexistent_mod", "library": "bin/nonexistent.dll"}
  ],
  "tools": [
    {"tool_id": "broken.tool", "module": "nonexistent_mod", "definition": "tools/builtin/missing.json"}
  ],
  "runtime_files": []
})json";
    }
    bool badDeployFailed = false;
    try
    {
        visionrt::readToolsPackage(badPkgDir);
    }
    catch (const std::exception &)
    {
        badDeployFailed = true;
    }
    assert((badDeployFailed));
    // Package 1 on disk remains untouched
    assert((std::filesystem::exists(pkg1Sentinel)));
    assert((std::filesystem::exists(toolsPackageDir / "bin" / "test_plugin_module.dll")));

    // 4. Test locked-file protection:
    // When a process has an open file in tools-package, replacement must fail without deleting target
    const auto    lockedPath = toolsPackageDir / "bin" / "test_plugin_module.dll";
    std::ifstream lockFile(lockedPath, std::ios::binary);
    assert((lockFile.is_open()));

    // Attempt renaming directory while file is locked
    const auto      backupDir = installRoot / ".tools-package-backup-test";
    std::error_code ecRename;
    std::filesystem::rename(toolsPackageDir, backupDir, ecRename);
#ifdef _WIN32
    // On Windows NTFS, renaming directory with open file fails
    assert((ecRename));
    assert((std::filesystem::exists(toolsPackageDir)));
    assert((std::filesystem::exists(pkg1Sentinel)));
#endif
    lockFile.close();

    // 5. Clean Package Replacement:
    // Package 2 replaces test.plugin.function with test.plugin.counter, and removes custom.py_math
    // Deploy Package 2 to toolsPackageDir:
    const auto stagingDir = installRoot / ".tools-package-staging-test";
    std::filesystem::create_directories(stagingDir / "bin");
    std::filesystem::create_directories(stagingDir / "tools" / "builtin");
    std::filesystem::copy_file(pluginDll, stagingDir / "bin" / "test_plugin_module.dll");

    // Write counter declaration
    {
        std::ofstream ofs(stagingDir / "tools" / "builtin" / "counter.json");
        ofs << R"json({
  "schema_version": 3,
  "tool_id": "test.plugin.counter",
  "display_name": "Plugin Counter",
  "version": "1.0.0",
  "kind": "cpp",
  "init_params": [
    {"id": "initial", "name": "Initial", "type": "Int", "default": 0}
  ],
  "inputs": [],
  "outputs": [
    {"id": "count", "name": "Count", "type": "Int"}
  ]
})json";
    }
    // Write platform receiver declaration
    {
        std::ofstream ofs(stagingDir / "tools" / "builtin" / "recv.json");
        ofs << R"json({
  "schema_version": 3,
  "tool_id": "platform.test_recv",
  "display_name": "Test Receiver",
  "version": "1.0.0",
  "kind": "host",
  "init_params": [
    {"id": "source_flow_id", "name": "Source Flow", "type": "String", "default": ""},
    {"id": "source_node_seq", "name": "Source Node", "type": "String", "default": ""},
    {"id": "source_port_id", "name": "Source Port", "type": "String", "default": ""},
    {"id": "timeout_ms", "name": "Timeout", "type": "Int", "default": 1000}
  ],
  "inputs": [],
  "outputs": [
    {"id": "value", "name": "Value", "type": "Int"},
    {"id": "source_status_code", "name": "Source Status Code", "type": "Int"},
    {"id": "source_status_message", "name": "Source Status Message", "type": "String"}
  ],
  "entry": {"binding": "visionrt.receiver"}
})json";
    }
    // Write module_index.json for Package 2
    {
        std::ofstream ofs(stagingDir / "module_index.json");
        ofs << R"json({
  "schema_version": 1,
  "variant": "cpu",
  "modules": [
    {"id": "test_plugin_module", "library": "bin/test_plugin_module.dll"}
  ],
  "tools": [
    {"tool_id": "test.plugin.counter", "module": "test_plugin_module", "definition": "tools/builtin/counter.json"},
    {"tool_id": "platform.test_recv", "definition": "tools/builtin/recv.json"}
  ],
  "runtime_files": []
})json";
    }

    // Replace toolsPackageDir cleanly
    std::filesystem::remove_all(toolsPackageDir);
    std::filesystem::rename(stagingDir, toolsPackageDir);

    // Verify Package 1 unique sentinel is completely gone (no leftover files)
    assert((!std::filesystem::exists(pkg1Sentinel)));
    assert((!std::filesystem::exists(toolsPackageDir / "tools" / "builtin" / "func.json")));
    assert((!std::filesystem::exists(toolsPackageDir / "python" / "builtin" / "calc" / "calc.py")));
    assert((!std::filesystem::exists(toolsPackageDir / "tools" / "builtin" / "calc.json")));
    std::cout << "[Evidence T-08] Package replacement verified: Package 1 unique files and scripts do not linger.\n";

    // 6. Simulate restart with Package 2:
    const auto pkg2    = visionrt::readToolsPackage(toolsPackageDir);
    auto       loader2 = std::make_shared<PluginLoader>();
    ToolRegistry registry2;
    registerReceiveTools(registry2);
    registry2.setPluginLoader(loader2);
    registry2.registerPackage(pkg2);
    registry2.loadNativeImplementations();
    setupPython(registry2, python);

    // Verify new tool test.plugin.counter is available
    assert((registry2.find("test.plugin.counter") != nullptr));
    assert((registry2.find("test.plugin.function") == nullptr));
    assert((registry2.find("custom.py_math") == nullptr));

    // Load original project:
    ProjectStorage storage2(registry2);
    auto           loadedOldProject = storage2.load(savedProjectFile);
    // Node 1 (test.plugin.function) and Node 2 (custom.py_math) must have DefinitionMissing diagnostics!
    bool           foundMissingN1 = false;
    bool           foundMissingN2 = false;
    for (const auto &d : loadedOldProject.diagnostics)
    {
        if (d.node && *d.node == 1 && d.code == ProjectStorageCode::DefinitionMissing)
        {
            foundMissingN1 = true;
        }
        if (d.node && *d.node == 2 && d.code == ProjectStorageCode::DefinitionMissing)
        {
            foundMissingN2 = true;
        }
    }
    assert((foundMissingN1));
    assert((foundMissingN2));
    std::cout << "[Evidence T-08] Reopening project after package replacement: removed tools diagnosed with "
                 "DefinitionMissing.\n";

    // Running flow with missing definition is rejected
    RuntimeEngine engine2(2);
    const auto    startResult = engine2.start(loadedOldProject.project, registry2, RunMode::Once);
    assert((startResult.code != 0));

    std::cout << "[PASS] Offline package replacement and failure protection test passed successfully." << std::endl;
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

    PythonRuntime python;

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
    testRealPluginModulePipeline(python);
    std::cout << "[PASS] Mock test_plugin_module pipeline passed." << std::endl;

    try
    {
        testOfflinePackageReplacementAndProtection(repoRoot, python);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[TEST FAILED WITH EXCEPTION] " << e.what() << std::endl;
        return 1;
    }

    std::cout << "All example contract tests passed successfully!" << std::endl;
    return 0;
}
