#ifdef NDEBUG
#undef NDEBUG
#endif

#include "plugins/PluginLoader.h"
#include "core/ToolRegistry.h"

#include <cassert>
#include <filesystem>
#include <iostream>

using namespace visionrt;

#include "runtime/RuntimeEngine.h"
#include "ui/AppController.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

namespace {

QString toQString(const std::filesystem::path& p) {
    const auto u8 = p.generic_u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(u8.data()), static_cast<qsizetype>(u8.size()));
}

RuntimeUpdate finish(RuntimeEngine& engine) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < deadline) {
        auto update = engine.pollLatest();
        if (update.completion) {
            engine.acknowledgeCompletion(*update.completion);
            return update;
        }
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("runtime completion deadline exceeded");
}

ResultSlot::Snapshot snapshot(const RuntimeUpdate& update, NodeId id) {
    for (auto& item : update.nodes) if (item.node == id) return item.snapshot;
    throw std::runtime_error("node update absent");
}

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

void advancedFaultInjectionAndModuleDestroy(const std::filesystem::path& modulePath) {
    auto loader = std::make_shared<PluginLoader>();
    PlatformToolServices services;

    // 1. create returns nullptr + code 0 -> host converts to -3
    loader->registerModule("test.plugin.create_null_success", modulePath);
    auto nullSuccessAdapter = loader->createAdapter("test.plugin.create_null_success", services);
    assert(nullSuccessAdapter->initialize({}).code == -3);

    // 2. create returns nonnull + code 5 -> host destroys inside module and returns 5
    loader->registerModule("test.plugin.create_nonnull_fail", modulePath);
    auto nonnullFailAdapter = loader->createAdapter("test.plugin.create_nonnull_fail", services);
    assert(nonnullFailAdapter->initialize({}).code == 5);

    // 3. initialize throws exception -> host catches, returns -1
    loader->registerModule("test.plugin.init_throw", modulePath);
    auto throwAdapter = loader->createAdapter("test.plugin.init_throw", services);
    assert(throwAdapter->initialize({}).code == -1);

    // 4. release failure
    loader->registerModule("test.plugin.release_fail", modulePath);
    auto relFailAdapter = loader->createAdapter("test.plugin.release_fail", services);
    assert(relFailAdapter->initialize({}).code == 0);
    assert(relFailAdapter->release().code == 6);

    // Verify all instances were destroyed inside module
    QLibrary lib(QString::fromStdString(modulePath.string()));
    auto getInstCount = reinterpret_cast<int(VT_CALL *)() noexcept>(lib.resolve("testPluginInstanceCount"));
    auto getDestroyCalls = reinterpret_cast<int(VT_CALL *)() noexcept>(lib.resolve("testPluginDestroyCalls"));
    assert(getInstCount != nullptr);
    assert(getDestroyCalls != nullptr);
    assert(getInstCount() == 0); // No leaked plugin instances!
    assert(getDestroyCalls() > 0);
}

void cppFunctionLifecycleAndNodeRuntime(const std::filesystem::path& modulePath) {
    auto loader = std::make_shared<PluginLoader>();
    loader->registerModule("test.plugin.function", modulePath);

    ToolRegistry registry;
    registry.setPluginLoader(loader);

    ToolDefinition def;
    def.id = "test.plugin.func.tool";
    def.displayName = "函数算子";
    def.version = "1.0.0";
    def.kind = ToolKind::CppFunction;
    def.registryName = "test.plugin.function";
    def.inputs.push_back({"x", "输入", {Type::Int}, std::int64_t{0}});
    def.outputs.push_back({"y", "输出", {Type::Int}});
    registry.addDefinition(def);

    RuntimeEngine engine(1);
    ProjectDefinition project;
    FlowDefinition flow;
    flow.id = "flow_1";
    flow.name = "Flow 1";
    flow.runSelected = true;

    NodeDefinition node;
    node.id = 1;
    node.toolId = def.id;
    node.toolNameSnapshot = def.displayName;
    node.signatures.inputs.emplace("x", TypeSpec{Type::Int});
    node.signatures.outputs.emplace("y", TypeSpec{Type::Int});
    node.signatures.outputs.emplace("__status_code", TypeSpec{Type::Int});
    node.signatures.outputs.emplace("__status_message", TypeSpec{Type::String});
    node.inputs["x"] = {BindingMode::Literal, std::int64_t{21}, std::nullopt};
    flow.nodes.push_back(node);
    project.flows = {flow};

    auto startStatus = engine.start(project, registry, RunMode::Once);
    assert(startStatus.code == 0);
    auto update = finish(engine);
    auto snap = snapshot(update, 1);
    assert(snap != nullptr);
    assert(snap->status.code == 0);
    assert(std::get<std::int64_t>(snap->outputs.at("y")) == 42);
}

void moduleIndexDiagnostics() {
    auto loader = std::make_shared<PluginLoader>();
    QTemporaryDir temp;
    assert(temp.isValid());
    const auto baseDir = std::filesystem::u8path(temp.path().toUtf8().toStdString());
    loader->setBaseDirectory(baseDir);

    // 1. Non-existent file
    loader->loadIndexFile(baseDir / "non_existent.json");
    assert(!loader->diagnostics().empty());
    loader->clearDiagnostics();

    // 2. Bad JSON
    const auto badJson = baseDir / "bad.json";
    {
        QFile f(toQString(badJson));
        assert(f.open(QIODevice::WriteOnly));
        f.write("{ invalid json ");
    }
    loader->loadIndexFile(badJson);
    assert(!loader->diagnostics().empty());
    loader->clearDiagnostics();

    // 3. Duplicate registry_name and escaping path
    const auto testJson = baseDir / "test.json";
    {
        QFile f(toQString(testJson));
        assert(f.open(QIODevice::WriteOnly));
        const char* content = R"({
            "schema_version": 1,
            "modules": [
                {"registry_name": "mod.valid", "path": "bin/valid.dll"},
                {"registry_name": "mod.valid", "path": "bin/dup.dll"},
                {"registry_name": "mod.escape", "path": "../../evil.dll"},
                {"registry_name": "mod.abs", "path": "C:/abs/path.dll"}
            ]
        })";
        f.write(content);
    }
    loader->loadIndexFile(testJson);
    assert(loader->diagnostics().size() == 3); // duplicate, escape, absolute
    assert(loader->hasModule("mod.valid"));
    assert(!loader->hasModule("mod.escape"));
    assert(!loader->hasModule("mod.abs"));
}

void appControllerToolsPackageIntegration(const std::filesystem::path& modulePath) {
    QTemporaryDir temp;
    assert(temp.isValid());
    const auto appDir = std::filesystem::u8path(temp.path().toUtf8().toStdString());

    // Create tools-package directory structure
    const auto pkgDir = appDir / "tools-package";
    const auto binDir = pkgDir / "bin";
    const auto yamlDir = pkgDir / "assets" / "tools" / "builtin" / "测试" / "数学";
    std::filesystem::create_directories(binDir);
    std::filesystem::create_directories(yamlDir);

    // Copy module dll into binDir
    std::filesystem::copy_file(modulePath, binDir / "test_module.dll", std::filesystem::copy_options::overwrite_existing);

    // Create module_index.json
    {
        QFile f(toQString(pkgDir / "module_index.json"));
        assert(f.open(QIODevice::WriteOnly));
        const char* indexContent = R"({
            "schema_version": 1,
            "modules": [
                {"registry_name": "test.plugin.counter", "path": "bin/test_module.dll"}
            ]
        })";
        f.write(indexContent);
    }

    // Create tool yaml
    {
        QFile f(toQString(yamlDir / "test.plugin.counter.yaml"));
        assert(f.open(QIODevice::WriteOnly));
        const char* yamlContent = R"(
schema_version: 1
tool_id: test.plugin.counter.packaged
display_name: 实际打包插件
version: 1.0.0
kind: cpp_class
entry:
  registry_name: test.plugin.counter
init_params:
  - id: initial
    name: 初始值
    type: Int
    default: 10
inputs: []
outputs:
  - id: count
    name: 计数值
    type: Int
)";
        f.write(yamlContent);
    }

    // Initialize AppController pointing to appDir
    AppController app(1, temp.path());
    const auto meta = app.toolMetadata("test.plugin.counter.packaged");
    assert(!meta.isEmpty());
    assert(meta.value("displayName").toString() == "实际打包插件");
    assert(meta.value("registryName").toString() == "test.plugin.counter");
    const auto defs = app.toolDefinitions();
    assert(std::any_of(defs.begin(), defs.end(), [](const QVariant& v) {
        return v.toMap().value("id").toString() == "test.plugin.counter.packaged";
    }));
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
    QGuiApplication qt(argc, argv);
    const auto modulePath = std::filesystem::u8path(argv[1]);
    try {
        lifecycleAndReentrancy(modulePath);
        singleModuleLoadOnlyOnce(modulePath);
        faultInjection(modulePath);
        advancedFaultInjectionAndModuleDestroy(modulePath);
        cppFunctionLifecycleAndNodeRuntime(modulePath);
        moduleIndexDiagnostics();
        appControllerToolsPackageIntegration(modulePath);
        registryIntegration(modulePath);
        std::cout << "plugin contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Plugin contract failed: " << error.what() << '\n';
        return 1;
    }
}
