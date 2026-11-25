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
#include <iostream>

using namespace visionrt;
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

int main(int argc, char **argv)
try
{
    QCoreApplication application(argc, argv);
    PythonRuntime    python(testsupport::pythonEnvironment());
    auto             loader = std::make_shared<PluginLoader>();
    ToolRegistry     registry;
    registry.setNativeProvider(loader);
    registry.registerPackage(readToolsPackage(std::filesystem::path(u8"" VISIONRT_TEST_TOOLS_PACKAGE)));
    registry.loadNativeImplementations();
    testsupport::setupPython(registry, python);
    ProjectStorage storage(registry);
    const auto     sourceRoot = std::filesystem::path(u8"" VISIONRT_SOURCE_DIR);
    const auto     file       = sourceRoot / u8"examples/综合示例/变距尺子标定测量/变距尺子标定测量.vrtapp";
    auto           loaded     = storage.load(file);
    for (const auto &diagnostic : loaded.diagnostics)
        std::cerr << diagnostic.field << ": " << diagnostic.message << '\n';
    require(loaded.diagnostics.empty(), "calibration example needs no schema reconciliation");
    require(loaded.project.flows.front().nodes.size() == 4, "calibration example has four existing tool nodes");
    const auto &node        = loaded.project.flows.front().nodes.at(1);
    const auto *declaration = registry.find(node.toolId);

    // Hydrate training images from the training resource ZIP and write to a temporary archive.
    std::string hydrateError;
    auto        hydrated = hydrateNodeTrainingSets(node, *declaration, sourceRoot, &hydrateError);
    require(hydrateError.empty(), "hydration of calibration training archive failed");
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "temp directory unavailable");
    const auto tempArchive = std::filesystem::path(tempDir.path().toStdWString()) / "retrained-calibration.zip";
    const auto archiveUtf8 = tempArchive.generic_u8string();
    hydrated[2]            = std::string(reinterpret_cast<const char *>(archiveUtf8.data()), archiveUtf8.size());
    const auto plateUtf8   = (sourceRoot / u8"examples/综合示例/变距尺子标定测量/assets/calplate_80mm.cpd").generic_u8string();
    hydrated[1]            = std::string(reinterpret_cast<const char *>(plateUtf8.data()), plateUtf8.size());

    auto trainer = registry.create(node.toolId);
    auto trained = trainer->train(hydrated);
    require(static_cast<bool>(validateToolExecution(declaration->outputs, trained.outputs, trained.detail,
                                                    ProducedBy::Train)),
            "native calibration trains through the ordinary Values interface");
    require(std::filesystem::is_regular_file(tempArchive), "calibration train() wrote training resource ZIP");
    require(!TrainingArchive::readModel(tempArchive, "calibration-model").empty(),
            "calibration archive contains reusable model bytes");
    require(TrainingArchive::readImages(tempArchive).size() == 1,
            "calibration archive preserves training source image");
    require(std::get<CoordinateFrame2D>(trained.outputs.at(6)).count(0) == 1,
            "training publishes a generic coordinate frame");

    RuntimeEngine engine(2, sourceRoot);
    require(static_cast<bool>(engine.start(loaded.project, registry, RunMode::Once)), "calibration pipeline starts");
    const auto update = testsupport::finish(engine);
    for (NodeId id = 1; id <= 4; ++id)
    {
        const auto result = testsupport::snapshot(update, id);
        if (!result->status)
            std::cerr << result->status.message << '\n' << result->status.detail << '\n';
        require(static_cast<bool>(result->status), "calibration pipeline tool failed");
    }
    const auto result    = testsupport::snapshot(update, 4);
    const auto mean      = std::get<double>(result->outputs.at(3));
    const auto deviation = std::get<double>(result->outputs.at(4));
    require(mean >= 0.99 && mean <= 1.01, "mean ruler pitch stays within one percent of one millimeter");
    require(deviation < 20, "sample standard deviation stays below twenty micrometers");
    std::cout << "mean=" << mean << " mm; sample deviation=" << deviation << " um\n";
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
