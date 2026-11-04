#include "core/ToolPackage.h"
#include "ToolResultAssertions.h"
#include "core/NodeContracts.h"
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
                    declaration << R"({"schema_version":6,"name_en":"test.external.roots","name_zh":"外部根工具","version":"1","implementation":{"kind":"python_function","package":"external","file":"tool.py","symbol":"execute"},"next_port_id":{"init":0,"inputs":1,"outputs":2}})";
                }
                const auto overridden = readToolsPackage(
                    root, ToolRoots{externalDeclarations,
                                    std::filesystem::path(external.path().toStdWString()) / "code"});
                assert(std::any_of(overridden.tools.begin(), overridden.tools.end(),
                                   [](const auto &tool) { return tool.definition.nameEn == "test.external.roots"; }));
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
                    assert(testview::status(result).code == 0 && std::get<std::int64_t>(result.outputs.at(2)) == 42);
                    assert(instance->release().code == 0);
                    std::cout << "Pure Python ZIP result: 42\n";
                    return;
                }
                assert(registry.definitions().size() == 7);
                assert(registry.find("custom.image.mean") && registry.find("custom.call.counter")
                       && registry.find("custom.python.roi") && registry.find("custom.python.image_add")
                       && registry.find("visiontools.python.image_sizes")
                       && registry.find("visiontools.logic.expression") && registry.find("visiontools.logic.branch"));
                const auto images = DataRef<Image>(Image{cv::Mat(4, 6, CV_16UC1, cv::Scalar(8))(cv::Rect(1, 1, 3, 2)),
                                                         cv::Mat(4, 5, CV_8UC3, cv::Scalar(2, 2, 2))});
                {
                    auto mean = registry.create("custom.image.mean", {});
                    assert(mean->initialize({}).code == 0);
                    const auto result = mean->execute({
                        {1, images}
                    });
                    assert(testview::status(result).code == 0 && testview::businessCount(result) == 1);
                    assert((nestedOf<NestedVector<double, 1>>(std::get<Array>(result.outputs.at(2)))
                            == NestedVector<double, 1>{8.0, 2.0}));
                    // A wrong input type never reaches the Python function; the bridge reports it.
                    const auto wrongInput = testview::executionFailure(*mean, {{1, std::int64_t{7}}});
                    assert(!wrongInput.empty());
                    const auto missing = mean->execute({
                        {1, Missing{}}
                    });
                    // A declared port with no value is a business failure; the tool omits the value it
                    // never produced and the host materialises the declared default afterwards.
                    assert(testview::status(missing).code == 1001 && !missing.outputs.contains(2));
                    const auto empty = mean->execute({
                        {1, DataRef<Image>(Image{})}
                    });
                    assert(testview::status(empty).code == 0);
                    assert((nestedOf<NestedVector<double, 1>>(std::get<Array>(empty.outputs.at(2))).empty()));
                    assert(mean->release().code == 0);
                }
                {
                    auto       function = registry.create("visiontools.python.image_sizes", {});
                    const auto result   = function->execute({
                        {1, images}
                    });
                    assert(testview::status(result).code == 0);
                    assert((nestedOf<NestedVector<std::int64_t, 2>>(std::get<Array>(result.outputs.at(2)))
                            == NestedVector<std::int64_t, 2>{
                                {2, 3},
                                {4, 5}
                    }));
                }
                {
                    // A numeric host Array reaches Python as an ndarray. The packaged declaration
                    // cannot state variable ports, so the node contract materialises them and the
                    // production bridge converts the Array on the way in.
                    const auto *declaration = registry.find("visiontools.logic.expression");
                    assert(declaration && declaration->nodeModel == NodeModel::Expression);
                    NodeDefinition node;
                    node.id               = 1;
                    node.toolId           = declaration->nameEn;
                    node.toolNameSnapshot = declaration->nameZh;
                    node.expression       = ExpressionConfig{};
                    node.expression->variables.push_back(
                        {2, "数值", "values", {Type::Array, DType::Float64, 1}, false});
                    node.expression->program.push_back(
                        {3, "平均", "average", "mean(values)", {Type::Float}, true});
                    node.expression->nextPortId = 4;
                    auto expression = registry.create(nodeContract(node, *declaration), {});
                    assert(expression->initialize(nodeParameters(node, *declaration)).code == 0);
                    const auto result = expression->execute({
                        {2, Array::fromNested(NestedVector<double, 1>{2.0, 4.0})}
                    });
                    assert(testview::status(result).code == 0);
                    assert(std::get<double>(result.outputs.at(3)) == 3.0);
                    assert(expression->release().code == 0);
                }
                {
                    const auto *declaration = registry.find("visiontools.logic.branch");
                    assert(declaration && declaration->nodeModel == NodeModel::Condition);
                    NodeDefinition node;
                    node.id               = 2;
                    node.toolId           = declaration->nameEn;
                    node.toolNameSnapshot = declaration->nameZh;
                    node.expression       = ExpressionConfig{};
                    node.expression->variables.push_back(
                        {2, "数值", "values", {Type::Array, DType::Float64, 1}, false});
                    node.expression->program.push_back(
                        {3, "成立", "ok", "mean(values) > 2", {Type::Bool}, false});
                    node.expression->nextPortId = 4;
                    auto branch = registry.create(nodeContract(node, *declaration), {});
                    assert(branch->initialize(nodeParameters(node, *declaration)).code == 0);
                    const auto result = branch->execute({
                        {2, Array::fromNested(NestedVector<double, 1>{2.0, 4.0})}
                    });
                    assert(testview::status(result).code == 0);
                    assert(std::get<bool>(result.outputs.at(2)));
                    assert(!std::get<bool>(result.outputs.at(3)));
                    assert(branch->release().code == 0);
                }
                {
                    auto roi = registry.create("custom.python.roi", {});
                    assert(roi->initialize({}).code == 0);
                    const auto result = roi->execute({
                        {1, images}
                    });
                    assert(testview::status(result).code == 0 && testview::businessCount(result) == 1);
                    const auto &groups = std::get<DataRef<ROI>>(result.outputs.at(2)).get();
                    assert(groups.size() == 2);
                    assert((groups[0].front() == std::vector<double>{2, 0, 0, 3, 2, 0}));
                    assert((groups[1].front() == std::vector<double>{2, 0, 0, 5, 4, 0}));
                    const auto empty = roi->execute({
                        {1, DataRef<Image>(Image{})}
                    });
                    assert(testview::status(empty).code == 0 && std::get<DataRef<ROI>>(empty.outputs.at(2)).get().empty());
                    assert(roi->release().code == 0);
                }
                {
                    auto add = registry.create("custom.python.image_add", {});
                    assert(add->initialize({}).code == 0);
                    auto copyInput = DataRef<Image>(Image{cv::Mat(2, 3, CV_8UC1, cv::Scalar(10)),
                                                          cv::Mat(2, 3, CV_8UC3, cv::Scalar(20, 20, 20))});
                    const auto copied = add->execute({
                        {1, copyInput},
                        {2, std::string("copy")}
                    });
                    assert(testview::status(copied).code == 0);
                    assert(copyInput.get()[0].at<std::uint8_t>(0, 0) == 10);
                    const auto &copiedImages = std::get<DataRef<Image>>(copied.outputs.at(2)).get();
                    assert(copiedImages.size() == 2);
                    assert(copiedImages[0].at<std::uint8_t>(0, 0) == 11);
                    assert(copiedImages[1].at<cv::Vec3b>(0, 0) == cv::Vec3b(21, 21, 21));
                    assert(copiedImages[0].data != copyInput.get()[0].data);

                    auto inplaceInput = DataRef<Image>(Image{cv::Mat(2, 3, CV_8UC1, cv::Scalar(30))});
                    const auto inplace = add->execute({
                        {1, inplaceInput},
                        {2, std::string("inplace")}
                    });
                    assert(testview::status(inplace).code == 0);
                    assert(inplaceInput.get()[0].at<std::uint8_t>(0, 0) == 31);
                    const auto &inplaceImages = std::get<DataRef<Image>>(inplace.outputs.at(2)).get();
                    assert(inplaceImages[0].data == inplaceInput.get()[0].data);
                    assert(inplaceImages[0].at<std::uint8_t>(0, 0) == 31);

                    const auto invalid = add->execute({
                        {1, copyInput},
                        {2, std::string("other")}
                    });
                    assert(testview::status(invalid).code == 1003 && !invalid.outputs.contains(2));
                    assert(add->release().code == 0);
                }
                {
                    auto first  = registry.create("custom.call.counter", {});
                    auto second = registry.create("custom.call.counter", {});
                    assert(first->initialize({}).code == 0 && second->initialize({}).code == 0);
                    assert(std::get<std::int64_t>(first->execute({}).outputs.at(2)) == 1);
                    assert(std::get<std::int64_t>(first->execute({}).outputs.at(2)) == 2);
                    assert(std::get<std::int64_t>(second->execute({}).outputs.at(2)) == 1);
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
