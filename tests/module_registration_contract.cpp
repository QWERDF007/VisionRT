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
