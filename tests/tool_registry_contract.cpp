#include "core/ToolPackage.h"
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
        return {{}, inputs};
    }

    ToolStatus release() override
    {
        return {};
    }
};

ToolDefinition definition(std::string id)
{
    ToolDefinition result;
    result.id          = std::move(id);
    result.displayName = "注册工具";
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
    assert(registry.availability(tool.id).state == ToolAvailability::ImplementationUnavailable);
    rejects([&] { registry.create(tool.id, {}); });
    registry.registerCpp(tool, [](const PlatformToolServices &) { return std::make_unique<PassTool>(); });
    assert(registry.availability(tool.id).state == ToolAvailability::Available);
    rejects([&]
            { registry.registerCpp(tool, [](const PlatformToolServices &) { return std::make_unique<PassTool>(); }); });
    auto instance = registry.create(tool.id, {});
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
    assert(registry.find(tool.id)->outputs.back().type == TypeSpec{Type::Int});
    rejects([&] { registry.addDefinition(tool); });
    auto duplicate = package(tool);
    duplicate.tools.push_back(duplicate.tools.front());
    rejects([&] { registry.registerPackage(duplicate); });
    assert(registry.find(tool.id));

    auto python   = definition("test.python");
    python.kind   = ToolKind::PythonFunction;
    python.entry  = PythonEntry{"fixture", "tool.py", "execute"};
    registry.registerPackage(package(python));
    registry.setPythonImplementation(python.id,
                                     [](const PlatformToolServices &) { return std::make_unique<PassTool>(); });
    assert(registry.availability(python.id).state == ToolAvailability::Available);
    registry.registerPackage(package(python));
    assert(registry.availability(python.id).state == ToolAvailability::ImplementationUnavailable);
    registry.setPythonImplementation(python.id, {}, {-2, "Python 入口合同无效", "original import detail"});
    const auto failed = registry.availability(python.id);
    assert(failed.status.code == -2 && failed.status.message == "Python 入口合同无效");
    assert(failed.status.detail == "original import detail");
    try
    {
        registry.create(python.id, {});
        assert(false);
    }
    catch (const RegistryError &error)
    {
        assert(error.status.code == -2 && error.status.message == failed.status.message);
        assert(error.status.detail == failed.status.detail);
    }

    auto host        = definition("test.receiver");
    host.kind        = ToolKind::Host;
    host.hostBinding = "receiver";
    registry.registerPackage(package(host));
    assert(registry.availability(host.id).state == ToolAvailability::ImplementationUnavailable);
    registry.registerHostBinding(
        "receiver", [](const ToolDefinition &, const PlatformToolServices &) { return std::make_unique<PassTool>(); });
    assert(registry.availability(host.id).state == ToolAvailability::Available);
    assert(registry.create(host.id, {}));
    rejects([&] { registry.setPythonImplementation(host.id, {}); });

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
    assert(modules.availability(host.id).state == ToolAvailability::Available);
    modules.registerPackage({});
    assert(modules.definitions().empty());
    assert(loader->availability("test.missing.dll").state == ToolAvailability::ImplementationUnavailable);
    std::cout << "Stable tool IDs, derived status ports and transactional registry refresh verified\n";
}
