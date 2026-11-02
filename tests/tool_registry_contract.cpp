#include "core/ToolPackage.h"
#include "result_view.h"
#include "core/ToolRegistry.h"
#include "plugins/PluginLoader.h"

#include <cassert>
#include <iostream>

using namespace visionrt;

namespace {
template<class Action>
void rejects(Action action)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::exception &)
    {
        rejected = true;
    }
    assert(rejected);
}

class PassTool final : public IToolInstance
{
public:
    ToolStatus initialize(const Values &) override
    {
        return {};
    }

    ToolResult execute(const Values &inputs) override
    {
        return toolResult(inputs, {});
    }

    ToolStatus release() override
    {
        return {};
    }
};

ToolDefinition definition(std::string id)
{
    ToolDefinition result;
    result.nameEn      = std::move(id);
    result.nameZh = "注册工具";
    result.inputs      = {
        {"value", "输入", {Type::Int}}
    };
    result.outputs = {
        {"value", "输出", {Type::Int}}
    };
    return result;
}

ToolsPackage package(const ToolDefinition &tool)
{
    ToolsPackage result;
    result.tools.push_back({tool, {}});
    return result;
}
}

int main()
{
    ToolRegistry registry;
    auto         tool = definition("test.pass");
    registry.registerPackage(package(tool));
    assert(registry.availability("missing").state == ToolAvailability::DefinitionMissing);
    assert(registry.availability(tool.nameEn).state == ToolAvailability::ImplementationUnavailable);
    rejects([&] { registry.create(tool.nameEn, {}); });
    registry.registerCpp(tool, [](const PlatformToolServices &) { return std::make_unique<PassTool>(); });
    assert(registry.availability(tool.nameEn).state == ToolAvailability::Available);
    rejects([&]
            { registry.registerCpp(tool, [](const PlatformToolServices &) { return std::make_unique<PassTool>(); }); });
    auto instance = registry.create(tool.nameEn, {});
    assert(instance->initialize({}));
    assert(std::get<std::int64_t>(instance
                                      ->execute({
                                          {"value", std::int64_t{7}}
    })
                                      .outputs.at("value"))
           == 7);
    assert(instance->release());

    tool.outputs.insert(tool.outputs.begin(), {
                                                  {   "__status_code", "状态",    {Type::Int}},
                                                  {"__status_message", "消息", {Type::String}}
    });
    registry.registerPackage(package(tool));
    auto invalid                = tool;
    invalid.outputs.back().type = {Type::Float};
    rejects([&] { registry.registerPackage(package(invalid)); });
    assert(registry.find(tool.nameEn)->outputs.back().type == TypeSpec{Type::Int});
    rejects([&] { registry.addDefinition(tool); });
    auto duplicate = package(tool);
    duplicate.tools.push_back(duplicate.tools.front());
    rejects([&] { registry.registerPackage(duplicate); });
    assert(registry.find(tool.nameEn));

    auto python   = definition("test.python");
    python.kind   = ToolKind::PythonFunction;
    python.entry  = PythonEntry{"fixture", "tool.py", "execute"};
    registry.registerPackage(package(python));
    registry.setPythonImplementation(python.nameEn,
                                     [](const PlatformToolServices &) { return std::make_unique<PassTool>(); });
    assert(registry.availability(python.nameEn).state == ToolAvailability::Available);
    registry.registerPackage(package(python));
    assert(registry.availability(python.nameEn).state == ToolAvailability::ImplementationUnavailable);
    registry.setPythonImplementation(python.nameEn, {}, {-2, "Python 入口合同无效", "original import detail"});
    const auto failed = registry.availability(python.nameEn);
    assert(testview::status(failed).code == -2 && testview::status(failed).message == "Python 入口合同无效");
    assert(testview::status(failed).detail == "original import detail");
    try
    {
        registry.create(python.nameEn, {});
        assert(false);
    }
    catch (const RegistryError &error)
    {
        assert(testview::status(error).code == -2 && testview::status(error).message == testview::status(failed).message);
        assert(testview::status(error).detail == testview::status(failed).detail);
    }

    auto host        = definition("test.receiver");
    host.kind        = ToolKind::Host;
    host.hostBinding = "receiver";
    registry.registerPackage(package(host));
    assert(registry.availability(host.nameEn).state == ToolAvailability::ImplementationUnavailable);
    registry.registerHostBinding(
        "receiver", [](const ToolDefinition &, const PlatformToolServices &) { return std::make_unique<PassTool>(); });
    assert(registry.availability(host.nameEn).state == ToolAvailability::Available);
    assert(registry.create(host.nameEn, {}));
    rejects([&] { registry.setPythonImplementation(host.nameEn, {}); });

    auto         loader = std::make_shared<PluginLoader>();
    ToolRegistry modules;
    modules.setPluginLoader(loader);
    auto unavailable                        = package(definition("test.missing.dll"));
    unavailable.tools.front().moduleLibrary = "nonexistent-module.dll";
    unavailable.tools.push_back({host, {}});
    modules.registerHostBinding(
        "receiver", [](const ToolDefinition &, const PlatformToolServices &) { return std::make_unique<PassTool>(); });
    modules.registerPackage(unavailable);
    assert(modules.availability("test.missing.dll").state == ToolAvailability::ImplementationUnavailable);
    assert(modules.availability(host.nameEn).state == ToolAvailability::Available);
    modules.registerPackage({});
    assert(modules.definitions().empty());
    assert(loader->availability("test.missing.dll").state == ToolAvailability::ImplementationUnavailable);
    std::cout << "Stable tool IDs, derived status ports and transactional registry refresh verified\n";
}
