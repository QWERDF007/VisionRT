#ifdef NDEBUG
#undef NDEBUG
#endif

#include "plugins/PluginLoader.h"
#include "core/ToolRegistry.h"

#include <cassert>
#include <filesystem>
#include <iostream>

using namespace visionrt;

namespace {

void lifecycleAndReentrancy(const std::filesystem::path& modulePath) {
    auto loader = std::make_shared<PluginLoader>();
    loader->registerModule("test.plugin.counter", modulePath);
    PlatformToolServices services;

    // First adapter
    auto adapter1 = loader->createAdapter("test.plugin.counter", services);
    assert(adapter1 != nullptr);
    Values initParams1;
    initParams1.emplace("initial", std::int64_t{10});
    auto initStatus1 = adapter1->initialize(initParams1);
    assert(initStatus1.code == 0);

    auto res1 = adapter1->execute({});
    assert(res1.status.code == 0);
    assert(std::get<std::int64_t>(res1.outputs.at("count")) == 11);

    auto res2 = adapter1->execute({});
    assert(res2.status.code == 0);
    assert(std::get<std::int64_t>(res2.outputs.at("count")) == 12);

    auto res3 = adapter1->execute({});
    assert(res3.status.code == 0);
    assert(std::get<std::int64_t>(res3.outputs.at("count")) == 13);

    auto relStatus1 = adapter1->release();
    assert(relStatus1.code == 0);

    // Second independent adapter
    auto adapter2 = loader->createAdapter("test.plugin.counter", services);
    assert(adapter2 != nullptr);
    Values initParams2;
    initParams2.emplace("initial", std::int64_t{100});
    auto initStatus2 = adapter2->initialize(initParams2);
    assert(initStatus2.code == 0);

    auto res4 = adapter2->execute({});
    assert(res4.status.code == 0);
    assert(std::get<std::int64_t>(res4.outputs.at("count")) == 101);

    assert(adapter2->release().code == 0);
}

void singleModuleLoadOnlyOnce(const std::filesystem::path& modulePath) {
    auto loader = std::make_shared<PluginLoader>();
    loader->registerModule("test.plugin.counter", modulePath);
    PlatformToolServices services;

    std::vector<std::unique_ptr<IToolInstance>> adapters;
    for (int i = 0; i < 5; ++i) {
        auto ad = loader->createAdapter("test.plugin.counter", services);
        assert(ad->initialize({}).code == 0);
        adapters.push_back(std::move(ad));
    }
    // All 5 adapters must share the same single LoadedModule
    assert(loader->loadedModuleCount() == 1);

    for (auto& ad : adapters) {
        assert(ad->release().code == 0);
    }
}

void faultInjection(const std::filesystem::path& modulePath) {
    auto loader = std::make_shared<PluginLoader>();
    PlatformToolServices services;

    // 1. Missing DLL file
    loader->registerModule("test.missing.dll", "non_existent_library_12345.dll");
    auto missingAdapter = loader->createAdapter("test.missing.dll", services);
    auto missingInit = missingAdapter->initialize({});
    assert(missingInit.code == -3);
    auto missingExec = missingAdapter->execute({});
    assert(missingExec.status.code == -3);

    // 2. Unknown registry_name / null factory returned from module
    loader->registerModule("test.unknown.tool", modulePath);
    auto unknownAdapter = loader->createAdapter("test.unknown.tool", services);
    auto unknownInit = unknownAdapter->initialize({});
    assert(unknownInit.code == -3);
    auto unknownExec = unknownAdapter->execute({});
    assert(unknownExec.status.code == -3);

    // 3. Partial initialization failure inside plugin
    loader->registerModule("test.plugin.fail_init", modulePath);
    auto failInitAdapter = loader->createAdapter("test.plugin.fail_init", services);
    auto failInitStatus = failInitAdapter->initialize({});
    assert(failInitStatus.code == 7);
    auto failExec = failInitAdapter->execute({});
    assert(failExec.status.code == 7);
}

void registryIntegration(const std::filesystem::path& modulePath) {
    auto loader = std::make_shared<PluginLoader>();
    loader->registerModule("test.plugin.counter", modulePath);

    ToolRegistry registry;
    registry.setPluginLoader(loader);

    ToolDefinition def;
    def.id = "test.plugin.counter.tool";
    def.displayName = "插件计数器";
    def.version = "1.0.0";
    def.kind = ToolKind::CppClass;
    def.registryName = "test.plugin.counter";
    PortDefinition outPort;
    outPort.id = "count";
    outPort.name = "计数";
    outPort.type = {Type::Int};
    def.outputs.push_back(outPort);

    registry.addDefinition(def);

    auto avail = registry.availability(def.id);
    assert(avail.state == ToolAvailability::Available);

    PlatformToolServices services;
    auto instance = registry.create(def.id, services);
    assert(instance != nullptr);

    assert(instance->initialize({}).code == 0);
    auto res = instance->execute({});
    assert(res.status.code == 0);
    assert(std::get<std::int64_t>(res.outputs.at("count")) == 1);
    assert(instance->release().code == 0);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: plugin_contract <path_to_test_plugin_module.dll>\n";
        return 1;
    }
    const auto modulePath = std::filesystem::u8path(argv[1]);
    try {
        lifecycleAndReentrancy(modulePath);
        singleModuleLoadOnlyOnce(modulePath);
        faultInjection(modulePath);
        registryIntegration(modulePath);
        std::cout << "plugin contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Plugin contract failed: " << error.what() << '\n';
        return 1;
    }
}
