#include "core/ToolPackage.h"
#include "core/ToolRegistry.h"
#include "python/PythonRuntime.h"
#include "runtime/Snapshot.h"

#include <cassert>
#include <filesystem>
#include <future>
#include <iostream>

using namespace visionrt;

int main(int argc, char **argv)
{
    assert(argc == 2 || (argc == 3 && std::string_view(argv[1]) == "--package"));
    const auto argument = std::string(argv[argc - 1]);
    const auto root = std::filesystem::absolute(std::filesystem::path(std::u8string(argument.begin(), argument.end())));
    PythonRuntime runtime;
    try
    {
        auto worker = std::async(
            std::launch::async,
            [&]
            {
                const auto   package = readToolsPackage(root);
                ToolRegistry registry;
                for (const auto &tool : package.tools)
                    if (tool.definition.kind == ToolKind::PythonFunction
                        || tool.definition.kind == ToolKind::PythonClass)
                        registry.addDefinition(tool.definition);
                for (const auto &loaded : runtime.refresh(registry.definitions()))
                {
                    assert(loaded.status);
                    registry.setPythonImplementation(loaded.toolId, runtime.factory(*registry.find(loaded.toolId)),
                                                     loaded.status);
                }
                if (argc == 3)
                {
                    assert(package.index.modules.empty() && package.tools.size() == 1);
                    auto instance = registry.create("test.add", {});
                    assert(instance->initialize({}).code == 0);
                    const auto result = instance->execute({});
                    assert(result.status.code == 0 && std::get<std::int64_t>(result.outputs.at("value")) == 42);
                    assert(instance->release().code == 0);
                    std::cout << "Pure Python ZIP result: 42\n";
                    return;
                }
                assert(registry.definitions().size() == 3);
                assert(registry.find("custom.image.mean") && registry.find("custom.call.counter")
                       && registry.find("visiontools.python.image_sizes"));
                const auto images = DataRef<Image>(Image{cv::Mat(4, 6, CV_16UC1, cv::Scalar(8))(cv::Rect(1, 1, 3, 2)),
                                                         cv::Mat(4, 5, CV_8UC3, cv::Scalar(2, 2, 2))});
                {
                    auto mean = registry.create("custom.image.mean", {});
                    assert(mean->initialize({}).code == 0);
                    const auto result = mean->execute({
                        {"images", images}
                    });
                    assert(result.status.code == 0 && result.outputs.size() == 1);
                    assert((std::get<Array>(result.outputs.at("means")).get<NestedVector<double, 1>>()
                            == NestedVector<double, 1>{8.0, 2.0}));
                    const auto wrong = mean->execute({
                        {"images", std::int64_t{7}}
                    });
                    assert(wrong.status.code == -2 && isMissing(wrong.outputs.at("means")));
                    assert(dynamic_cast<IToolFailureContext &>(*mean).failurePhase() == NodePhase::Prepare);
                    const auto missing = mean->execute({
                        {"images", Missing{}}
                    });
                    assert(missing.status.code == 1 && isMissing(missing.outputs.at("means")));
                    const auto empty = mean->execute({
                        {"images", DataRef<Image>(Image{})}
                    });
                    assert(empty.status.code == 0);
                    assert((std::get<Array>(empty.outputs.at("means")).get<NestedVector<double, 1>>().empty()));
                    assert(mean->release().code == 0);
                }
                {
                    auto       function = registry.create("visiontools.python.image_sizes", {});
                    const auto result   = function->execute({
                        {"images", images}
                    });
                    assert(result.status.code == 0);
                    assert((std::get<Array>(result.outputs.at("sizes")).get<NestedVector<std::int64_t, 2>>()
                            == NestedVector<std::int64_t, 2>{
                                {2, 3},
                                {4, 5}
                    }));
                }
                {
                    auto first  = registry.create("custom.call.counter", {});
                    auto second = registry.create("custom.call.counter", {});
                    assert(first->initialize({}).code == 0 && second->initialize({}).code == 0);
                    assert(std::get<std::int64_t>(first->execute({}).outputs.at("count")) == 1);
                    assert(std::get<std::int64_t>(first->execute({}).outputs.at("count")) == 2);
                    assert(std::get<std::int64_t>(second->execute({}).outputs.at("count")) == 1);
                    assert(first->release().code == 0 && second->release().code == 0);
                }
            });
        worker.get();
        std::cout << "Installed JSON package consumed through PythonRuntime\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
