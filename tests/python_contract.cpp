#include "core/ToolPackage.h"
#include "core/ToolRegistry.h"
#include "python/PythonRuntime.h"
#include "runtime/Snapshot.h"

#include <QTemporaryDir>
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
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
                QTemporaryDir external;
                assert(external.isValid());
                const auto externalDeclarations = std::filesystem::path(external.path().toStdWString()) / "declarations";
                std::filesystem::create_directories(externalDeclarations);
                {
                    std::ofstream declaration(externalDeclarations / "external.json");
                    declaration << R"({
                        "schema_version":2,"tool_id":"test.external.roots","display_name":"外部根工具","version":"1",
                        "kind":"python_function","entry":{"package":"external","file":"tool.py","symbol":"execute"},
                        "init_params":[],"inputs":[],"outputs":[]
                    })";
                }
                const auto overridden = readToolsPackage(
                    root, ToolRoots{externalDeclarations,
                                    std::filesystem::path(external.path().toStdWString()) / "code"});
                assert(std::any_of(overridden.tools.begin(), overridden.tools.end(),
                                   [](const auto &tool) { return tool.definition.id == "test.external.roots"; }));
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
                assert(registry.definitions().size() == 5);
                assert(registry.find("custom.image.mean") && registry.find("custom.call.counter")
                       && registry.find("custom.python.roi") && registry.find("custom.python.image_add")
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
                    auto roi = registry.create("custom.python.roi", {});
                    assert(roi->initialize({}).code == 0);
                    const auto result = roi->execute({
                        {"images", images}
                    });
                    assert(result.status.code == 0 && result.outputs.size() == 1);
                    const auto &groups = std::get<DataRef<ROI>>(result.outputs.at("rois")).get();
                    assert(groups.size() == 2);
                    assert((groups[0].front() == std::vector<double>{2, 0, 0, 3, 2, 0}));
                    assert((groups[1].front() == std::vector<double>{2, 0, 0, 5, 4, 0}));
                    const auto empty = roi->execute({
                        {"images", DataRef<Image>(Image{})}
                    });
                    assert(empty.status.code == 0 && std::get<DataRef<ROI>>(empty.outputs.at("rois")).get().empty());
                    assert(roi->release().code == 0);
                }
                {
                    auto add = registry.create("custom.python.image_add", {});
                    assert(add->initialize({}).code == 0);
                    auto copyInput = DataRef<Image>(Image{cv::Mat(2, 3, CV_8UC1, cv::Scalar(10)),
                                                          cv::Mat(2, 3, CV_8UC3, cv::Scalar(20, 20, 20))});
                    const auto copied = add->execute({
                        {"images", copyInput},
                        {"mode", std::string("copy")}
                    });
                    assert(copied.status.code == 0);
                    assert(copyInput.get()[0].at<std::uint8_t>(0, 0) == 10);
                    const auto &copiedImages = std::get<DataRef<Image>>(copied.outputs.at("images")).get();
                    assert(copiedImages.size() == 2);
                    assert(copiedImages[0].at<std::uint8_t>(0, 0) == 11);
                    assert(copiedImages[1].at<cv::Vec3b>(0, 0) == cv::Vec3b(21, 21, 21));
                    assert(copiedImages[0].data != copyInput.get()[0].data);

                    auto inplaceInput = DataRef<Image>(Image{cv::Mat(2, 3, CV_8UC1, cv::Scalar(30))});
                    const auto inplace = add->execute({
                        {"images", inplaceInput},
                        {"mode", std::string("inplace")}
                    });
                    assert(inplace.status.code == 0);
                    assert(inplaceInput.get()[0].at<std::uint8_t>(0, 0) == 31);
                    const auto &inplaceImages = std::get<DataRef<Image>>(inplace.outputs.at("images")).get();
                    assert(inplaceImages[0].data == inplaceInput.get()[0].data);
                    assert(inplaceImages[0].at<std::uint8_t>(0, 0) == 31);

                    const auto invalid = add->execute({
                        {"images", copyInput},
                        {"mode", std::string("other")}
                    });
                    assert(invalid.status.code == 1 && isMissing(invalid.outputs.at("images")));
                    assert(add->release().code == 0);
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
