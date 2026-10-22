#ifdef NDEBUG
#    undef NDEBUG
#endif

#include "core/ToolPackage.h"
#include "plugins/PluginLoader.h"

#include <QLibrary>
#include <cassert>
#include <iostream>

using namespace visionrt;

int main(int argc, char **argv)
{
    assert(argc == 6);
    auto       loader = std::make_shared<PluginLoader>();
    const auto path   = std::filesystem::path(argv[1]);
    loader->registerModule("test.registration.first", path);
    loader->registerModule("test.registration.second", path);
    assert(loader->loadedModuleCount() == 0);
    ToolRegistry catalog;
    catalog.setPluginLoader(loader);
    ToolsPackage package;
    for (const char *id : {"test.registration.first", "test.registration.second", "not.registered"})
    {
        ToolDefinition definition;
        definition.id          = id;
        definition.displayName = "注册测试";
        definition.version     = "1";
        definition.kind        = ToolKind::Cpp;
        package.tools.push_back({definition, path});
    }
    auto rejectedTool          = package.tools.front();
    rejectedTool.definition.id = "test.rejected.module";
    rejectedTool.moduleLibrary = std::filesystem::path(argv[2]);
    package.tools.push_back(std::move(rejectedTool));
    catalog.registerPackage(package);
    assert(catalog.availability("test.registration.first").state != ToolAvailability::Available);
    assert(loader->loadedModuleCount() == 0);
    try
    {
        catalog.create("test.registration.first", {});
        assert(false);
    }
    catch (const RegistryError &error)
    {
        assert(error.reason == ToolAvailability::ImplementationNotLoaded);
    }
    catalog.loadNativeImplementations();
    assert(catalog.availability("test.registration.first").state == ToolAvailability::Available);
    assert(catalog.availability("not.registered").state == ToolAvailability::ImplementationUnavailable);
    assert(catalog.availability("test.rejected.module").state == ToolAvailability::ImplementationUnavailable);
    assert(!catalog.availability("test.rejected.module").message.empty());
    catalog.loadNativeImplementations();
    assert(loader->loadedModuleCount() == 1);
    ToolStatus status;
    auto       module = loader->loadModule(path, status);
    assert(module && status);
    assert(module->registry.nativeFactories().size() == 2);
    assert(module->registry.nativeFactories().contains("test.registration.first"));
    assert(loader->loadedModuleCount() == 1);
    assert(loader->loadModule(path, status) == module);
    QLibrary probe(QString::fromLocal8Bit(argv[1]));
    assert(probe.load());
    const auto createCalls = reinterpret_cast<int(VT_CALL *)() noexcept>(probe.resolve("testRegistrationCreateCalls"));
    assert(createCalls && createCalls() == 0);
    loader->registerModule("test.registration.first", std::filesystem::path(argv[2]));
    catalog.loadNativeImplementations();
    assert(catalog.availability("test.registration.first").state == ToolAvailability::ImplementationUnavailable);
    assert(catalog.availability("test.registration.second").state == ToolAvailability::Available);
    loader->registerModule("test.registration.first", path);
    assert(catalog.availability("test.registration.first").state == ToolAvailability::Available);
    assert(createCalls() == 0);
    for (const auto &[id, expected] : {
             std::pair{ "test.registration.first", 11},
             {"test.registration.second", 22}
    })
    {
        auto instance = loader->createAdapter(id, {});
        assert(instance && instance->initialize({}));
        const auto result = instance->execute({});
        assert(result.status);
        assert(std::get<std::int64_t>(result.outputs.at("value")) == expected);
        assert(instance->release());
        assert(instance->release());
    }
    loader->registerModule("not.registered", path);
    const auto absent = loader->createAdapter("not.registered", {})->initialize({});
    assert(!absent && absent.message.find("not.registered") != std::string::npos);
    assert(createCalls() == 2);
    for (int index = 2; index < argc; ++index)
    {
        ToolStatus rejected;
        assert(!loader->loadModule(std::filesystem::path(argv[index]), rejected));
        assert(!rejected && !rejected.message.empty());
        assert(loader->loadedModuleCount() == 1);
        if (index == 5)
            assert(rejected.message.find("不同模块") != std::string::npos);
    }
    auto surviving = loader->createAdapter("test.registration.first", {});
    assert(surviving->initialize({}));
    assert(std::get<std::int64_t>(surviving->execute({}).outputs.at("value")) == 11);
    assert(surviving->release());
    std::cout << "ABI3 registration copies IDs, isolates rejected tables and dispatches module-owned factories\n";
}
