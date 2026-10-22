#ifdef NDEBUG
#    undef NDEBUG
#endif

#include "core/ToolRegistry.h"
#include "plugins/PluginLoader.h"

#include <core/ToolPackage.h>

#include <cassert>
#include <filesystem>
#include <iostream>

using namespace visionrt;

#include "runtime/RuntimeEngine.h"
#if defined(VISIONRT_BUILD_APP)
#    include "ui/AppController.h"
#endif
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QProcess>
#include <QTemporaryDir>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

namespace {

QString toQString(const std::filesystem::path &p)
{
    const auto u8 = p.generic_u8string();
    return QString::fromUtf8(reinterpret_cast<const char *>(u8.data()), static_cast<qsizetype>(u8.size()));
}

RuntimeUpdate finish(RuntimeEngine &engine)
{
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto update = engine.pollLatest();
        if (update.completion)
        {
            engine.acknowledgeCompletion(*update.completion);
            return update;
        }
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("runtime completion deadline exceeded");
}

ResultSlot::Snapshot snapshot(const RuntimeUpdate &update, NodeId id)
{
    for (auto &item : update.nodes)
        if (item.node == id)
            return item.snapshot;
    throw std::runtime_error("node update absent");
}

void lifecycleAndReentrancy(const std::filesystem::path &modulePath)
{
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

void singleModuleLoadOnlyOnce(const std::filesystem::path &modulePath)
{
    auto loader = std::make_shared<PluginLoader>();
    loader->registerModule("test.plugin.counter", modulePath);
    PlatformToolServices services;

    std::vector<std::unique_ptr<IToolInstance>> adapters;
    for (int i = 0; i < 5; ++i)
    {
        auto ad = loader->createAdapter("test.plugin.counter", services);
        assert(ad->initialize({}).code == 0);
        adapters.push_back(std::move(ad));
    }
    // All 5 adapters must share the same single LoadedModule
    assert(loader->loadedModuleCount() == 1);

    for (auto &ad : adapters)
    {
        assert(ad->release().code == 0);
    }
}

void faultInjection(const std::filesystem::path &modulePath)
{
    auto                 loader = std::make_shared<PluginLoader>();
    PlatformToolServices services;

    for (const auto &[id, path] : {
             std::pair{ "test.missing.dll", std::filesystem::path("non_existent_library_12345.dll")},
             std::pair{"test.unknown.tool",                                              modulePath}
    })
    {
        loader->registerModule(id, path);
        try
        {
            loader->createAdapter(id, services);
            assert(false);
        }
        catch (const RegistryError &error)
        {
            assert(error.status.code == -3 && !error.status.message.empty());
        }
    }

    // 3. Partial initialization failure inside plugin
    loader->registerModule("test.plugin.fail_init", modulePath);
    auto failInitAdapter = loader->createAdapter("test.plugin.fail_init", services);
    auto failInitStatus  = failInitAdapter->initialize({});
    assert(failInitStatus.code == 7);
    auto failExec = failInitAdapter->execute({});
    assert(failExec.status.code == 7);
}

void advancedFaultInjectionAndModuleDestroy(const std::filesystem::path &modulePath)
{
    auto                 loader = std::make_shared<PluginLoader>();
    PlatformToolServices services;
    QLibrary             probe(QString::fromStdString(modulePath.string()));
    assert(probe.load());
    const auto releaseCalls
        = reinterpret_cast<int(VT_CALL *)() noexcept>(probe.resolve("testPluginCounterReleaseCalls"));
    assert(releaseCalls);
    const auto before = releaseCalls();

    for (const auto &[id, code] : {
             std::pair{"test.plugin.create_null_success", -3},
             std::pair{"test.plugin.create_nonnull_fail",  5}
    })
    {
        loader->registerModule(id, modulePath);
        try
        {
            loader->createAdapter(id, services);
            assert(false);
        }
        catch (const RegistryError &error)
        {
            assert(error.status.code == code);
            if (code == 5)
                assert(error.status.detail == "factory diagnostic");
        }
    }
    assert(releaseCalls() == before + 1);

    // 3. initialize throws exception -> host catches, returns -1
    loader->registerModule("test.plugin.init_throw", modulePath);
    auto       throwAdapter = loader->createAdapter("test.plugin.init_throw", services);
    const auto thrown       = throwAdapter->initialize({});
    assert(thrown.code == -1 && thrown.message == "工具初始化异常");
    assert(thrown.detail == "Simulated init exception in plugin");

    // 4. release failure
    loader->registerModule("test.plugin.release_fail", modulePath);
    auto relFailAdapter = loader->createAdapter("test.plugin.release_fail", services);
    assert(relFailAdapter->initialize({}).code == 0);
    assert(relFailAdapter->release().code == 6);

    // Verify all instances were destroyed inside module
    QLibrary lib(QString::fromStdString(modulePath.string()));
    auto     getInstCount    = reinterpret_cast<int(VT_CALL *)() noexcept>(lib.resolve("testPluginInstanceCount"));
    auto     getDestroyCalls = reinterpret_cast<int(VT_CALL *)() noexcept>(lib.resolve("testPluginDestroyCalls"));
    assert(getInstCount != nullptr);
    assert(getDestroyCalls != nullptr);
    assert(getInstCount() == 0); // No leaked plugin instances!
    assert(getDestroyCalls() > 0);
}

void cppFunctionLifecycleAndNodeRuntime(const std::filesystem::path &modulePath)
{
    auto loader = std::make_shared<PluginLoader>();
    loader->registerModule("test.plugin.function", modulePath);

    ToolRegistry registry;
    registry.setPluginLoader(loader);

    ToolDefinition def;
    def.id           = "test.plugin.function";
    def.displayName  = "函数算子";
    def.version      = "1.0.0";
    def.kind         = ToolKind::Cpp;

    def.inputs.push_back({"x", "输入", {Type::Int}, std::int64_t{0}});
    def.initParams.push_back({"resource", "资源", {Type::Int}, std::int64_t{1}});
    def.outputs.push_back({"y", "输出", {Type::Int}});
    registry.addDefinition(def);
    registry.loadNativeImplementations();

    RuntimeEngine     engine(1);
    ProjectDefinition project;
    FlowDefinition    flow;
    flow.id          = "flow_1";
    flow.name        = "Flow 1";
    flow.runSelected = true;

    NodeDefinition node;
    node.id               = 1;
    node.toolId           = def.id;
    node.toolNameSnapshot = def.displayName;
    node.signatures.inputs.emplace("x", TypeSpec{Type::Int});
    node.signatures.outputs.emplace("y", TypeSpec{Type::Int});
    node.signatures.outputs.emplace("__status_code", TypeSpec{Type::Int});
    node.signatures.outputs.emplace("__status_message", TypeSpec{Type::String});
    node.inputs["x"] = {BindingMode::Literal, std::int64_t{21}, std::nullopt};
    flow.nodes.push_back(node);
    project.flows = {flow};

    QLibrary probe(toQString(modulePath));
    assert(probe.load());
    const auto instanceCount = reinterpret_cast<int(VT_CALL *)() noexcept>(probe.resolve("testPluginInstanceCount"));
    const auto initializeCount
        = reinterpret_cast<int(VT_CALL *)() noexcept>(probe.resolve("testPluginFunctionInitializeCalls"));
    assert(instanceCount && initializeCount);
    const auto initializations = initializeCount();
    assert(engine.requestSynchronize(project, registry));
    auto prepared = finish(engine);
    assert(!snapshot(prepared, 1));
    assert(instanceCount() == 1 && initializeCount() == initializations);
    assert(engine.requestSynchronize(project, registry));
    assert(engine.state() == RuntimeState::Idle);

    auto startStatus = engine.start(project, registry, RunMode::Once);
    assert(startStatus.code == 0);
    auto update = finish(engine);
    auto snap   = snapshot(update, 1);
    assert(snap != nullptr);
    assert(snap->status.code == 0);
    assert(std::get<std::int64_t>(snap->outputs.at("y")) == 42);
    assert(instanceCount() == 1 && initializeCount() == initializations + 1);
    project.flows.front().nodes.front().inputs.at("x").literal = std::int64_t{42};
    assert(engine.requestSynchronize(project, registry));
    assert(engine.state() == RuntimeState::Idle);
    assert(engine.start(project, registry, RunMode::Once));
    snap = snapshot(finish(engine), 1);
    assert(std::get<std::int64_t>(snap->outputs.at("y")) == 84);
    assert(instanceCount() == 1 && initializeCount() == initializations + 1);

    const auto publication                                     = snap->publication;
    project.flows.front().nodes.front().initParams["resource"] = std::int64_t{2};
    assert(engine.requestSynchronize(project, registry));
    prepared = finish(engine);
    assert(snapshot(prepared, 1)->publication == publication);
    assert(instanceCount() == 1 && initializeCount() == initializations + 1);
    assert(engine.start(project, registry, RunMode::Once));
    finish(engine);
    assert(initializeCount() == initializations + 2);

    project.path = modulePath.parent_path() / "managed.vrt.json";
    assert(engine.requestSynchronize(project, registry));
    finish(engine);
    assert(instanceCount() == 1 && initializeCount() == initializations + 2);
    auto copied = project.flows.front().nodes.front();
    copied.id   = 2;
    project.flows.front().nodes.push_back(copied);
    assert(engine.requestSynchronize(project, registry));
    prepared = finish(engine);
    assert(!snapshot(prepared, 2));
    assert(instanceCount() == 2 && initializeCount() == initializations + 2);
    project.flows.front().nodes.pop_back();
    assert(engine.requestSynchronize(project, registry));
    finish(engine);
    assert(instanceCount() == 1 && initializeCount() == initializations + 2);

    def.id = "test.plugin.create_nonnull_fail";
    loader->registerModule(def.id, modulePath);
    registry.addDefinition(def);
    registry.loadNativeImplementations();
    project.flows.front().nodes.front().toolId = def.id;
    assert(engine.requestSynchronize(project, registry));
    prepared = finish(engine);
    assert(prepared.nodes.front().instanceDiagnostic->status.code == 5);
    assert(prepared.nodes.front().instanceDiagnostic->status.detail == "factory diagnostic");
    assert(prepared.nodes.front().instanceDiagnostic->phase == NodePhase::Create);
    assert(instanceCount() == 0);
    assert(engine.requestSynchronize(project, registry));
    assert(engine.state() == RuntimeState::Idle);
    assert(engine.start(project, registry, RunMode::Once));
    snap = snapshot(finish(engine), 1);
    assert(snap && snap->status.code == 5);
    assert(snap->status.detail == "factory diagnostic");
    assert(snap->phase == NodePhase::Create && snap->status.message.find("节点") == std::string::npos);
    assert(isMissing(snap->outputs.at("y")));
}

void writeText(const std::filesystem::path &path, const std::string &content)
{
    std::filesystem::create_directories(path.parent_path());
    QFile file(toQString(path));
    assert(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    assert(file.write(content.data(), static_cast<qint64>(content.size())) == static_cast<qint64>(content.size()));
}

// Inside a package root the index is the only discovery source; the loader and registry follow it.
void packageRegistration(const std::filesystem::path &modulePath)
{
    QTemporaryDir temp;
    assert(temp.isValid());
    const auto base           = std::filesystem::u8path(temp.path().toUtf8().toStdString());
    const auto pkgDir         = base / std::filesystem::path(u8"工具 包 with space");
    const auto declarationDir = pkgDir / std::filesystem::path(u8"assets/tools/builtin/测试/数学");
    std::filesystem::create_directories(pkgDir / "bin");
    std::filesystem::copy_file(modulePath, pkgDir / "bin" / "test_module.dll",
                               std::filesystem::copy_options::overwrite_existing);

    writeText(declarationDir / "counter.json", R"json({
  "schema_version": 1,
  "tool_id": "test.plugin.counter",
  "display_name": "打包计数器",
  "version": "1.0.0",
  "kind": "cpp",
  "init_params": [
    {"id": "initial", "name": "初始值", "type": "Int", "default": 10}
  ],
  "inputs": [],
  "outputs": [
    {"id": "count", "name": "计数值", "type": "Int"}
  ]
})json");
    writeText(declarationDir / "function.json", R"json({
  "schema_version": 1,
  "tool_id": "test.plugin.function",
  "display_name": "打包函数",
  "version": "1.0.0",
  "kind": "cpp",
  "init_params": [],
  "inputs": [
    {"id": "x", "name": "输入X", "type": "Int"}
  ],
  "outputs": [
    {"id": "y", "name": "输出Y", "type": "Int"}
  ]
})json");
    // Present on disk but absent from the index: must never be discovered.
    writeText(declarationDir / "ghost.json", R"json({
  "schema_version": 1,
  "tool_id": "test.plugin.ghost.packaged",
  "display_name": "未索引工具",
  "version": "1.0.0",
  "kind": "cpp",
  "init_params": [],
  "inputs": [],
  "outputs": [
    {"id": "count", "name": "计数值", "type": "Int"}
  ]
})json");
    writeText(pkgDir / "module_index.json", R"json({
  "schema_version": 1,
  "variant": "cpu",
  "modules": [
    {"id": "test_module", "library": "bin/test_module.dll"}
  ],
  "tools": [
    {"tool_id": "test.plugin.counter", "module": "test_module", "definition": "assets/tools/builtin/测试/数学/counter.json"},
    {"tool_id": "test.plugin.function", "module": "test_module", "definition": "assets/tools/builtin/测试/数学/function.json"}
  ],
  "runtime_files": ["bin/test_module.dll"]
})json");

    const auto package = readToolsPackage(pkgDir);
    assert(package.tools.size() == 2);

    auto         loader = std::make_shared<PluginLoader>();
    ToolRegistry registry;
    registry.setPluginLoader(loader);
    registry.registerPackage(package);

    assert(registry.find("test.plugin.counter") != nullptr);
    assert(registry.find("test.plugin.function") != nullptr);
    assert(registry.find("test.plugin.ghost.packaged") == nullptr);
    assert(loader->availability("test.plugin.counter").state == ToolAvailability::ImplementationNotLoaded);
    assert(loader->availability("test.plugin.function").state == ToolAvailability::ImplementationNotLoaded);
    registry.loadNativeImplementations();

    registry.registerPackage(readToolsPackage(pkgDir));
    assert(registry.find("test.plugin.ghost.packaged") == nullptr);
    assert(registry.find("test.plugin.counter") != nullptr);
    assert(registry.definitions().size() == 2);

    const auto legacyDir = base / "legacy-tools" / std::filesystem::path(u8"custom/自定义/示例");
    writeText(legacyDir / "legacy.json", R"json({
  "schema_version": 1,
  "tool_id": "test.plugin.legacy",
  "display_name": "旧目录工具",
  "version": "1.0.0",
  "kind": "cpp",
  "init_params": [],
  "inputs": [
    {"id": "x", "name": "输入X", "type": "Int"}
  ],
  "outputs": [
    {"id": "y", "name": "输出Y", "type": "Int"}
  ]
})json");
    registry.registerPackage(readToolsPackage(pkgDir));
    const auto *legacy = registry.find("test.plugin.legacy");
    assert(legacy == nullptr);

    const auto duplicateDir = base / "duplicate-tools" / std::filesystem::path(u8"builtin/测试/数学");
    writeText(duplicateDir / "counter.json", R"json({
  "schema_version": 1,
  "tool_id": "test.plugin.counter",
  "display_name": "重复计数器",
  "version": "2.0.0",
  "kind": "cpp",
  "init_params": [
    {"id": "initial", "name": "初始值", "type": "Int", "default": 10}
  ],
  "inputs": [],
  "outputs": [
    {"id": "count", "name": "计数值", "type": "Int"}
  ]
})json");
    registry.registerPackage(readToolsPackage(pkgDir));
    const auto *counter = registry.find("test.plugin.counter");
    assert(counter != nullptr);
    const auto counterRoot = package.root.generic_u8string();
    assert(counter->definitionPath.generic_u8string().rfind(counterRoot, 0) == 0);

    // Both packaged tools execute through the single module of the package.
    PlatformToolServices services;
    auto                 instance = registry.create("test.plugin.counter", services);
    assert(instance != nullptr);
    Values initParams;
    initParams.emplace("initial", std::int64_t{10});
    assert(instance->initialize(initParams).code == 0);
    auto counterResult = instance->execute({});
    assert(counterResult.status.code == 0);
    assert(std::get<std::int64_t>(counterResult.outputs.at("count")) == 11);
    assert(instance->release().code == 0);

    auto function = registry.create("test.plugin.function", services);
    assert(function != nullptr);
    assert(function->initialize({}).code == 0);
    Values functionInputs;
    functionInputs.emplace("x", std::int64_t{21});
    auto functionResult = function->execute(functionInputs);
    assert(functionResult.status.code == 0);
    assert(std::get<std::int64_t>(functionResult.outputs.at("y")) == 42);
    assert(function->release().code == 0);

    // Registering another package replaces the tool set instead of merging it.
    const auto otherDir = base / "other-package";
    std::filesystem::create_directories(otherDir / "bin");
    std::filesystem::copy_file(modulePath, otherDir / "bin" / "test_module.dll",
                               std::filesystem::copy_options::overwrite_existing);
    writeText(otherDir / std::filesystem::path(u8"assets/tools/custom/Python/示例/function.json"), R"json({
  "schema_version": 1,
  "tool_id": "test.plugin.function",
  "display_name": "另一包函数",
  "version": "1.0.0",
  "kind": "cpp",
  "init_params": [],
  "inputs": [
    {"id": "x", "name": "输入X", "type": "Int"}
  ],
  "outputs": [
    {"id": "y", "name": "输出Y", "type": "Int"}
  ]
})json");
    writeText(otherDir / "module_index.json", R"json({
  "schema_version": 1,
  "variant": "cpu",
  "modules": [
    {"id": "test_module", "library": "bin/test_module.dll"}
  ],
  "tools": [
    {"tool_id": "test.plugin.function", "module": "test_module", "definition": "assets/tools/custom/Python/示例/function.json"}
  ],
  "runtime_files": []
})json");
    const auto otherPackage = readToolsPackage(otherDir);
    registry.registerPackage(otherPackage);
    registry.loadNativeImplementations();
    assert(registry.find("test.plugin.function") != nullptr);
    assert(registry.find("test.plugin.counter") == nullptr);
    assert(registry.find("test.plugin.legacy") == nullptr);
    const auto *relocated = registry.find("test.plugin.function");
    assert(relocated->builtin);
    assert(registry.availability("test.plugin.function").state == ToolAvailability::Available);

    // A package that fails validation leaves the registered tool set untouched.
    writeText(otherDir / "module_index.json", "modules: [ broken\n");
    bool rejected = false;
    try
    {
        registry.registerPackage(readToolsPackage(otherDir));
    }
    catch (const std::exception &)
    {
        rejected = true;
    }
    assert(rejected);
    assert(registry.find("test.plugin.function") != nullptr);
    assert(registry.availability("test.plugin.function").state == ToolAvailability::Available);
}

#if defined(VISIONRT_BUILD_APP)
void appControllerToolsPackageIntegration(const std::filesystem::path &modulePath)
{
    QTemporaryDir temp;
    assert(temp.isValid());
    const auto appDir = std::filesystem::u8path(temp.path().toUtf8().toStdString());

    // Create tools-package directory structure
    const auto pkgDir  = appDir / "tools-package";
    const auto binDir  = pkgDir / "bin";
    const auto jsonDir = pkgDir / std::filesystem::path(u8"assets/tools/builtin/测试/数学");
    std::filesystem::create_directories(binDir);
    std::filesystem::create_directories(jsonDir);

    // Copy module dll into binDir
    std::filesystem::copy_file(modulePath, binDir / "test_module.dll",
                               std::filesystem::copy_options::overwrite_existing);

    // Create module_index.json
    const auto indexFile = pkgDir / "module_index.json";
    {
        QFile f(toQString(indexFile));
        assert(f.open(QIODevice::WriteOnly));
        const char *indexContent = R"json({
  "schema_version": 1,
  "variant": "cpu",
  "modules": [
    {"id": "test_module", "library": "bin/test_module.dll"}
  ],
  "tools": [
    {"tool_id": "test.plugin.counter", "module": "test_module", "definition": "assets/tools/builtin/测试/数学/test.plugin.counter.json"}
  ],
  "runtime_files": []
})json";
        f.write(indexContent);
    }

    {
        QFile f(toQString(jsonDir / "test.plugin.counter.json"));
        assert(f.open(QIODevice::WriteOnly));
        const char *jsonContent = R"json({
  "schema_version": 1,
  "tool_id": "test.plugin.counter",
  "display_name": "实际打包插件",
  "version": "1.0.0",
  "kind": "cpp",
  "init_params": [
    {"id": "initial", "name": "初始值", "type": "Int", "default": 10}
  ],
  "inputs": [],
  "outputs": [
    {"id": "count", "name": "计数值", "type": "Int"}
  ]
})json";
        f.write(jsonContent);
    }

    // Initialize AppController pointing to appDir
    AppController app(1, temp.path());
    const auto    meta = app.toolMetadata("test.plugin.counter");
    assert(!meta.isEmpty());
    assert(meta.value("displayName").toString() == "实际打包插件");
    assert(meta.value("id").toString() == "test.plugin.counter");
    auto defs = app.toolDefinitions();
    assert(std::any_of(defs.begin(), defs.end(), [](const QVariant &v)
                       { return v.toMap().value("id").toString() == "test.plugin.counter"; }));

    auto waitForIdle = [&app]()
    {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (app.busy() && std::chrono::steady_clock::now() < deadline)
        {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(5ms);
        }
        assert(!app.busy());
    };

    assert(app.runState() == "Refreshing");
    waitForIdle();
    for (const auto &item : app.toolDefinitions())
        assert(item.toMap().value("available").toBool());
    QLibrary probe(toQString(binDir / "test_module.dll"));
    assert(probe.load());
    const auto instances = reinterpret_cast<int(VT_CALL *)() noexcept>(probe.resolve("testPluginInstanceCount"));
    assert(instances && instances() == 0);

    // 1. Add node for Tool A, runOnce, verify execution
    app.addNode("test.plugin.counter", 100, 100);
    waitForIdle();
    assert(app.nodes().size() == 1);
    const auto nodeAId = app.nodes().first().toMap().value("id").toString();
    assert(instances() == 1 && app.nodes().first().toMap().value("state") == "idle");
    app.copyNodes({nodeAId});
    app.pasteNodes(200, 100);
    waitForIdle();
    assert(app.nodes().size() == 2 && instances() == 2);
    const auto copiedNode = app.nodes().last().toMap().value("id").toString();
    assert(copiedNode != nodeAId);
    app.deleteNodes({copiedNode});
    waitForIdle();
    assert(app.nodes().size() == 1 && instances() == 1);
    app.selectNode(nodeAId);
    const auto saved = temp.filePath("node-lifecycle.vrt.json");
    assert(app.save(saved));
    waitForIdle();
    app.requestNew();
    waitForIdle();
    assert(app.nodes().isEmpty() && instances() == 0);
    app.requestOpen(saved);
    app.resolveUnsaved("discard");
    waitForIdle();
    assert(app.nodes().size() == 1 && instances() == 1);
    assert(app.nodes().first().toMap().value("state") == "idle");
    app.runOnce();
    waitForIdle();
    assert(app.nodes().first().toMap().value("state").toString() == "succeeded");

    // 2. Add Tool B (test.plugin.function) into module_index.json & declaration JSON
    const auto funcJsonFile = jsonDir / "test.plugin.function.json";
    {
        QFile f(toQString(funcJsonFile));
        assert(f.open(QIODevice::WriteOnly));
        const char *funcJsonContent = R"json({
  "schema_version": 1,
  "tool_id": "test.plugin.function",
  "display_name": "函数插件",
  "version": "1.0.0",
  "kind": "cpp",
  "init_params": [],
  "inputs": [
    {"id": "x", "name": "输入X", "type": "Int"}
  ],
  "outputs": [
    {"id": "y", "name": "输出Y", "type": "Int"}
  ]
})json";
        f.write(funcJsonContent);
    }
    {
        QFile f(toQString(indexFile));
        assert(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const char *indexWithB = R"json({
  "schema_version": 1,
  "variant": "cpu",
  "modules": [
    {"id": "test_module", "library": "bin/test_module.dll"}
  ],
  "tools": [
    {"tool_id": "test.plugin.counter", "module": "test_module", "definition": "assets/tools/builtin/测试/数学/test.plugin.counter.json"},
    {"tool_id": "test.plugin.function", "module": "test_module", "definition": "assets/tools/builtin/测试/数学/test.plugin.function.json"}
  ],
  "runtime_files": []
})json";
        f.write(indexWithB);
    }

    // Call rescanTools() and verify Tool B is discovered
    app.rescanTools();
    waitForIdle();

    defs        = app.toolDefinitions();
    bool foundB = false;
    for (const auto &d : defs)
    {
        if (d.toMap().value("id").toString() == "test.plugin.function")
        {
            foundB = true;
            assert(d.toMap().value("available").toBool());
        }
    }
    assert(foundB);

    // Add node for Tool B, bind x to node A's count, runOnce
    app.addNode("test.plugin.function", 300, 100);
    waitForIdle();
    assert(app.nodes().size() == 2);
    const auto nodeBId = app.nodes().last().toMap().value("id").toString();
    assert(app.bindInput(app.portContext("x"), nodeAId, "count"));
    app.runOnce();
    waitForIdle();
    for (const auto &n : app.nodes())
    {
        assert(n.toMap().value("state").toString() == "succeeded");
    }

    // 3. Delete Tool A mapping from module_index.json and rescan
    {
        QFile f(toQString(indexFile));
        assert(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const char *indexOnlyB = R"json({
  "schema_version": 1,
  "variant": "cpu",
  "modules": [
    {"id": "test_module", "library": "bin/test_module.dll"}
  ],
  "tools": [
    {"tool_id": "test.plugin.function", "module": "test_module", "definition": "assets/tools/builtin/测试/数学/test.plugin.function.json"}
  ],
  "runtime_files": []
})json";
        f.write(indexOnlyB);
    }
    app.rescanTools();
    waitForIdle();

    // Verify Node A is retained but marked unavailable
    assert(app.nodes().size() == 2);
    bool nodeAUnavailable = false;
    bool nodeBAvailable   = false;
    for (const auto &n : app.nodes())
    {
        const auto map = n.toMap();
        if (map.value("id").toString() == nodeAId)
        {
            assert(map.value("missing").toBool());
            nodeAUnavailable = true;
        }
        else if (map.value("id").toString() == nodeBId)
        {
            assert(!map.value("missing").toBool());
            nodeBAvailable = true;
        }
    }
    assert(nodeAUnavailable);
    assert(nodeBAvailable);

    // 4. Bad index rescan: corrupt module_index.json and rescan
    {
        QFile f(toQString(indexFile));
        assert(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const char *badJson = R"({"invalid": [)";
        f.write(badJson);
    }
    app.rescanTools();
    // Bad index is rejected immediately; Tool B remains available!
    defs   = app.toolDefinitions();
    foundB = false;
    for (const auto &d : defs)
    {
        if (d.toMap().value("id").toString() == "test.plugin.function")
        {
            foundB = true;
            assert(d.toMap().value("available").toBool());
        }
    }
    assert(foundB);

    // 5. Restore valid index, test consecutive rescan idempotency
    {
        QFile f(toQString(indexFile));
        assert(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const char *indexOnlyB = R"json({
  "schema_version": 1,
  "variant": "cpu",
  "modules": [
    {"id": "test_module", "library": "bin/test_module.dll"}
  ],
  "tools": [
    {"tool_id": "test.plugin.function", "module": "test_module", "definition": "assets/tools/builtin/测试/数学/test.plugin.function.json"}
  ],
  "runtime_files": []
})json";
        f.write(indexOnlyB);
    }
    app.rescanTools();
    waitForIdle();
    const auto count1 = app.toolDefinitions().size();
    app.rescanTools();
    waitForIdle();
    const auto count2 = app.toolDefinitions().size();
    assert(count1 == count2);

    // 6. Running rejection test: rescan request while not Idle is rejected
    // Delete node A to make the flow runnable, then toggle loop
    app.deleteNodes(QVariantList{nodeAId});
    waitForIdle();
    assert(app.nodes().size() == 1);
    app.toggleLoop();
    for (int i = 0; i < 20 && !app.busy(); ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(5ms);
    }
    assert(app.busy());
    const auto runningState = app.runState();
    assert(runningState == "RunningLoop");
    // Call rescanTools while running; must be rejected and state remains RunningLoop
    app.rescanTools();
    assert(app.runState() == "RunningLoop");
    app.deleteNodes({app.nodes().first().toMap().value("id")});
    waitForIdle();
    assert(app.runState() == "Idle");
    assert(app.nodes().isEmpty() && instances() == 0);
    {
        QFile definition(toQString(jsonDir / "release.json"));
        assert(definition.open(QIODevice::WriteOnly));
        definition.write(R"({"schema_version":1,"tool_id":"test.plugin.release_fail","display_name":"释放诊断",
                            "version":"1.0.0","kind":"cpp","init_params":[],"inputs":[],"outputs":[]})");
        QFile index(toQString(indexFile));
        assert(index.open(QIODevice::WriteOnly | QIODevice::Truncate));
        index.write(
            R"({"schema_version":1,"variant":"cpu","modules":[{"id":"test_module","library":"bin/test_module.dll"}],
                        "tools":[{"tool_id":"test.plugin.release_fail","module":"test_module",
                                  "definition":"assets/tools/builtin/测试/数学/release.json"}],"runtime_files":[]})");
    }
    app.rescanTools();
    waitForIdle();
    app.addNode("test.plugin.release_fail", 100, 100);
    waitForIdle();
    app.runOnce();
    waitForIdle();
    assert(app.nodes().first().toMap().value("state") == "succeeded");
    QString diagnosticJson;
    QObject::connect(&app, &AppController::outputTextReady, &app, [&](const QString &text) { diagnosticJson = text; });
    app.requestOutputText(app.portContext("__status_message", true), false, true);
    for (int attempt = 0; attempt < 100 && diagnosticJson.isEmpty(); ++attempt)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(5ms);
    }
    const auto exported = QJsonDocument::fromJson(diagnosticJson.toUtf8()).object();
    assert(exported.value("code").toInt() == 0 && exported.value("message") == "ok");
    assert(exported.value("phase") == "execute" && exported.value("tool_id") == "test.plugin.release_fail");
    assert(QDir(temp.path()).mkpath("moved"));
    assert(app.save(temp.filePath("moved/lifecycle.vrt.json")));
    waitForIdle();
    const auto released = app.nodes().first().toMap();
    assert(released.value("state") == "succeeded" && released.value("statusCode").toInt() == 0);
    assert(released.value("statusMessage").toString().isEmpty());
    assert(released.value("lifecycleMessage").toString().contains("释放资源失败"));
    assert(released.value("lifecycleDetail") == "native release detail");
    diagnosticJson.clear();
    app.requestOutputText(app.portContext("__status_message", true), false, true);
    for (int attempt = 0; attempt < 100 && diagnosticJson.isEmpty(); ++attempt)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(5ms);
    }
    const auto lifecycleExport = QJsonDocument::fromJson(diagnosticJson.toUtf8()).object();
    assert(lifecycleExport.value("code").toInt() == 0 && lifecycleExport.value("message") == "ok");
    assert(lifecycleExport.value("lifecycle").toObject().value("detail") == "native release detail");
    const auto reported = app.diagnostics().size();
    for (int attempt = 0; attempt < 10; ++attempt)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(5ms);
    }
    assert(app.diagnostics().size() == reported);
    assert(QDir(temp.path()).mkpath("again"));
    assert(app.save(temp.filePath("again/lifecycle.vrt.json")));
    waitForIdle();
    assert(app.diagnostics().size() == reported + 1);
}
#endif

void registryIntegration(const std::filesystem::path &modulePath)
{
    auto loader = std::make_shared<PluginLoader>();
    loader->registerModule("test.plugin.counter", modulePath);

    ToolRegistry registry;
    registry.setPluginLoader(loader);

    ToolDefinition def;
    def.id           = "test.plugin.counter";
    def.displayName  = "插件计数器";
    def.version      = "1.0.0";
    def.kind         = ToolKind::Cpp;

    PortDefinition outPort;
    outPort.id   = "count";
    outPort.name = "计数";
    outPort.type = {Type::Int};
    def.outputs.push_back(outPort);

    registry.addDefinition(def);

    assert(registry.availability(def.id).state == ToolAvailability::ImplementationNotLoaded);
    registry.loadNativeImplementations();

    auto avail = registry.availability(def.id);
    assert(avail.state == ToolAvailability::Available);

    PlatformToolServices services;
    auto                 instance = registry.create(def.id, services);
    assert(instance != nullptr);

    assert(instance->initialize({}).code == 0);
    auto res = instance->execute({});
    assert(res.status.code == 0);
    assert(std::get<std::int64_t>(res.outputs.at("count")) == 1);
    assert(instance->release().code == 0);
}

void windowsDllLoadingAndConcurrency(const std::filesystem::path &modulePath, const std::filesystem::path &pluginAPath,
                                     const std::filesystem::path &depAPath, const std::filesystem::path &pluginBPath,
                                     const std::filesystem::path &depBPath)
{
    QTemporaryDir temp1;
    QTemporaryDir temp2;
    assert(temp1.isValid() && temp2.isValid());
    const auto dir1 = std::filesystem::u8path(temp1.path().toUtf8().toStdString()) / "dir_one";
    const auto dir2 = std::filesystem::u8path(temp2.path().toUtf8().toStdString()) / "dir_two";
    std::filesystem::create_directories(dir1);
    std::filesystem::create_directories(dir2);

#ifdef _WIN32
    wchar_t initialDllDir[MAX_PATH] = {0};
    GetDllDirectoryW(MAX_PATH, initialDllDir);
    // Explicitly set a non-empty preset DLL directory before tests
    const auto presetDir = dir1 / "preset_dll_dir";
    std::filesystem::create_directories(presetDir);
    SetDllDirectoryW(presetDir.wstring().c_str());
    wchar_t currentPreset[MAX_PATH] = {0};
    GetDllDirectoryW(MAX_PATH, currentPreset);
    assert(wcscmp(presetDir.wstring().c_str(), currentPreset) == 0);
#endif

    const auto dll1 = dir1 / pluginAPath.filename();
    const auto dep1 = dir1 / depAPath.filename();
    const auto dll2 = dir2 / pluginBPath.filename();
    const auto dep2 = dir2 / depBPath.filename();

    std::filesystem::copy_file(pluginAPath, dll1, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(depAPath, dep1, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(pluginBPath, dll2, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(depBPath, dep2, std::filesystem::copy_options::overwrite_existing);

    auto loader1 = std::make_shared<PluginLoader>();
    auto loader2 = std::make_shared<PluginLoader>();

    ToolStatus                          status1, status2;
    std::shared_ptr<const LoadedModule> m1, m2;
    std::int64_t                        val1 = 0, val2 = 0;

    std::thread t1(
        [&]
        {
            m1 = loader1->loadModule(dll1, status1);
            if (status1.code == 0 && m1 != nullptr)
            {
                loader1->registerModule("test.plugin.a", dll1);
                PlatformToolServices s;
                auto                 ad = loader1->createAdapter("test.plugin.a", s);
                if (ad && ad->initialize({}).code == 0)
                {
                    auto r = ad->execute({});
                    if (r.status.code == 0)
                    {
                        val1 = std::get<std::int64_t>(r.outputs.at("val"));
                    }
                    ad->release();
                }
            }
        });

    std::thread t2(
        [&]
        {
            m2 = loader2->loadModule(dll2, status2);
            if (status2.code == 0 && m2 != nullptr)
            {
                loader2->registerModule("test.plugin.b", dll2);
                PlatformToolServices s;
                auto                 ad = loader2->createAdapter("test.plugin.b", s);
                if (ad && ad->initialize({}).code == 0)
                {
                    auto r = ad->execute({});
                    if (r.status.code == 0)
                    {
                        val2 = std::get<std::int64_t>(r.outputs.at("val"));
                    }
                    ad->release();
                }
            }
        });

    t1.join();
    t2.join();

    assert(status1.code == 0 && m1 != nullptr);
    assert(status2.code == 0 && m2 != nullptr);
    assert(val1 == 111);
    assert(val2 == 222);

#ifdef _WIN32
    wchar_t afterDllDir[MAX_PATH] = {0};
    GetDllDirectoryW(MAX_PATH, afterDllDir);
    assert(wcscmp(presetDir.wstring().c_str(), afterDllDir) == 0);
    SetDllDirectoryW(initialDllDir[0] ? initialDllDir : nullptr);
#endif
}

void unicodeAndChinesePathLoading(const std::filesystem::path &pluginAPath, const std::filesystem::path &depAPath)
{
    QTemporaryDir temp;
    assert(temp.isValid());
    const auto baseDir    = std::filesystem::u8path(temp.path().toUtf8().toStdString());
    const auto unicodeDir = baseDir / std::filesystem::path(u8"测试 插件 路径 with space");
    const auto binDir     = unicodeDir / "bin";
    std::filesystem::create_directories(binDir);

    const auto targetDll = binDir / "plugin_a.dll";
    const auto targetDep = binDir / depAPath.filename();
    std::filesystem::copy_file(pluginAPath, targetDll, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(depAPath, targetDep, std::filesystem::copy_options::overwrite_existing);

    writeText(unicodeDir / std::filesystem::path(u8"assets/tools/builtin/测试/示例") / "plugin_a.json",
              R"json({
  "schema_version": 1,
  "tool_id": "test.plugin.a",
  "display_name": "插件甲",
  "version": "1.0.0",
  "kind": "cpp",
  "init_params": [],
  "inputs": [],
  "outputs": [
    {"id": "val", "name": "值", "type": "Int"}
  ]
})json");
    writeText(unicodeDir / "module_index.json", R"json({
  "schema_version": 1,
  "variant": "cpu",
  "modules": [
    {"id": "mod_a", "library": "bin/plugin_a.dll"}
  ],
  "tools": [
    {"tool_id": "test.plugin.a", "module": "mod_a", "definition": "assets/tools/builtin/测试/示例/plugin_a.json"}
  ],
  "runtime_files": []
})json");

    // A package installed under a path with Chinese characters and spaces loads and executes.
    const auto package = readToolsPackage(unicodeDir);
    assert(package.tools.size() == 1);

    auto         loader = std::make_shared<PluginLoader>();
    ToolRegistry registry;
    registry.setPluginLoader(loader);
    registry.registerPackage(package);

    registry.loadNativeImplementations();
    const auto state = registry.availability("test.plugin.a");
    if (state.state != ToolAvailability::Available)
    {
        std::cerr << "Unavailable: " << state.message << "\n";
    }
    assert(state.state == ToolAvailability::Available);

    PlatformToolServices services;
    auto                 instance = registry.create("test.plugin.a", services);
    assert(instance != nullptr);
    auto initStatus = instance->initialize({});
    if (initStatus.code != 0)
    {
        std::cerr << "Init failed: code=" << initStatus.code << " msg=" << initStatus.message << "\n";
    }
    assert(initStatus.code == 0);
    auto res = instance->execute({});
    assert(res.status.code == 0);
    assert(std::get<std::int64_t>(res.outputs.at("val")) == 111);

    // Cross-release snapshot check: hold output while releasing adapter and loader
    const auto retainedVal = res.outputs.at("val");
    assert(instance->release().code == 0);
    instance.reset();
    loader.reset();
    assert(std::get<std::int64_t>(retainedVal) == 111);
}

static int runMissingDepChild(const std::filesystem::path &pluginAPath, const std::filesystem::path &pluginBPath,
                              const std::filesystem::path &depBPath)
{
    QTemporaryDir temp;
    if (!temp.isValid())
        return 10;
    const auto baseDir = std::filesystem::u8path(temp.path().toUtf8().toStdString());
    const auto dirFail = baseDir / "dir_fail";
    const auto dirOk   = baseDir / "dir_ok";
    std::filesystem::create_directories(dirFail);
    std::filesystem::create_directories(dirOk);

    // Plugin A exists on disk, but its required dependency (test_dep_a.dll) is missing!
    const auto missingDepDll = dirFail / pluginAPath.filename();
    std::filesystem::copy_file(pluginAPath, missingDepDll, std::filesystem::copy_options::overwrite_existing);

    // Plugin B has its dependency present
    const auto okDll = dirOk / pluginBPath.filename();
    const auto okDep = dirOk / depBPath.filename();
    std::filesystem::copy_file(pluginBPath, okDll, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(depBPath, okDep, std::filesystem::copy_options::overwrite_existing);

    auto       loaderFail = std::make_shared<PluginLoader>();
    ToolStatus statusMissing;
    auto       missingMod = loaderFail->loadModule(missingDepDll, statusMissing);
    if (missingMod != nullptr)
    {
        std::cerr << "Expected missingMod == nullptr, but loaded successfully!\n";
        return 1;
    }
    if (statusMissing.code == 0)
    {
        std::cerr << "Expected failure status code, got 0\n";
        return 2;
    }
#ifdef _WIN32
    // Must fail in the dynamic link loader path (Win32 error 126: ERROR_MOD_NOT_FOUND)
    if (statusMissing.message.find("126") == std::string::npos
        && statusMissing.message.find("Win32 error") == std::string::npos)
    {
        std::cerr << "Expected Win32 error in message: " << statusMissing.message << "\n";
        return 3;
    }
#endif

    // Ensure loader Ok is not poisoned and can still load and execute valid module B
    auto       loaderOk = std::make_shared<PluginLoader>();
    ToolStatus statusValid;
    auto       validMod = loaderOk->loadModule(okDll, statusValid);
    if (validMod == nullptr || statusValid.code != 0)
    {
        std::cerr << "Failed to load okDll: " << statusValid.message << "\n";
        return 4;
    }
    loaderOk->registerModule("test.plugin.b", okDll);
    PlatformToolServices services;
    auto                 ad = loaderOk->createAdapter("test.plugin.b", services);
    if (!ad || ad->initialize({}).code != 0)
    {
        std::cerr << "Failed to initialize ok adapter\n";
        return 5;
    }
    auto res = ad->execute({});
    if (res.status.code != 0)
    {
        std::cerr << "Failed to execute ok adapter\n";
        return 6;
    }
    if (std::get<std::int64_t>(res.outputs.at("val")) != 222)
    {
        std::cerr << "Expected val 222, got " << std::get<std::int64_t>(res.outputs.at("val")) << "\n";
        return 7;
    }
    ad->release();
    return 0;
}

void missingDependencyIsolation(const QString &appPath, const std::filesystem::path &pluginAPath,
                                const std::filesystem::path &pluginBPath, const std::filesystem::path &depBPath)
{
    QProcess    proc;
    QStringList args;
    args << "--run-missing-dep-child" << toQString(pluginAPath) << toQString(pluginBPath) << toQString(depBPath);
    proc.start(appPath, args);
    assert(proc.waitForFinished(10000));
    if (proc.exitCode() != 0)
    {
        std::cerr << "Child process stdout:\n" << proc.readAllStandardOutput().toStdString() << "\n";
        std::cerr << "Child process stderr:\n" << proc.readAllStandardError().toStdString() << "\n";
    }
    assert(proc.exitStatus() == QProcess::NormalExit);
    assert(proc.exitCode() == 0);
}

} // namespace

int main(int argc, char **argv)
{
#ifdef _WIN32
    if (argc == 5 && std::string(argv[1]) == "--late-runtime-child")
    {
        QCoreApplication qt(argc, argv);
        QTemporaryDir temporary;
        assert(temporary.isValid());
        const auto directory = std::filesystem::u8path(temporary.path().toUtf8().toStdString())
                               / std::filesystem::path(u8"延迟运行库");
        std::filesystem::create_directories(directory);
        const auto modulePath = directory / "late_module.dll";
        std::filesystem::copy_file(std::filesystem::u8path(argv[2]), modulePath);
        std::filesystem::copy_file(std::filesystem::u8path(argv[3]), directory / "test_dep_a.dll");
        std::filesystem::copy_file(std::filesystem::u8path(argv[4]), directory / "visionrt_late_dependency.dll");
        std::shared_ptr<const LoadedModule> retained;
        {
            PluginLoader loader;
            ToolStatus status;
            retained = loader.loadModule(modulePath, status);
            assert(retained && status);
        }
        const auto delayed = LoadLibraryW(L"visionrt_late_dependency.dll");
        if (!delayed)
        {
            std::cerr << "A loaded module lost its late-runtime search directory: " << GetLastError() << '\n';
            return 1;
        }
        const auto value = reinterpret_cast<int (*)()>(GetProcAddress(delayed, "dep_b_val"));
        assert(value && value() == 222);
        FreeLibrary(delayed);
        retained.reset();
        const auto orphan = LoadLibraryW(L"visionrt_late_dependency.dll");
        assert(!orphan);
        std::cout << "Late runtime directory follows loaded-module ownership and is removed after release.\n";
        return 0;
    }
#endif
    if (argc == 3 && std::string(argv[1]) == "--abi-child")
    {
        QCoreApplication qt(argc, argv);
        PluginLoader loader;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            ToolStatus status;
            const auto module = loader.loadModule(std::filesystem::u8path(argv[2]), status);
            assert(!module && !status);
            assert(status.message.find("ABI 主版本不匹配") != std::string::npos);
            assert(status.message.find("实际 2") != std::string::npos);
            assert(loader.loadedModuleCount() == 0);
        }
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--run-missing-dep-child")
    {
        if (argc < 5)
            return 11;
        QGuiApplication qt(argc, argv);
        return runMissingDepChild(std::filesystem::u8path(argv[2]), std::filesystem::u8path(argv[3]),
                                  std::filesystem::u8path(argv[4]));
    }

    if (argc < 2)
    {
        std::cerr << "Usage: plugin_contract <path_to_test_plugin_module.dll> [plugin_a] [dep_a] [plugin_b] [dep_b]\n";
        return 1;
    }
    QGuiApplication qt(argc, argv);
    const QString   appPath     = argv[0];
    const auto      modulePath  = std::filesystem::u8path(argv[1]);
    const auto      pluginAPath = argc > 2 ? std::filesystem::u8path(argv[2]) : modulePath;
    const auto      depAPath    = argc > 3 ? std::filesystem::u8path(argv[3]) : modulePath;
    const auto      pluginBPath = argc > 4 ? std::filesystem::u8path(argv[4]) : modulePath;
    const auto      depBPath    = argc > 5 ? std::filesystem::u8path(argv[5]) : modulePath;

    try
    {
        lifecycleAndReentrancy(modulePath);
        singleModuleLoadOnlyOnce(modulePath);
        faultInjection(modulePath);
        advancedFaultInjectionAndModuleDestroy(modulePath);
        cppFunctionLifecycleAndNodeRuntime(modulePath);
        packageRegistration(modulePath);
        windowsDllLoadingAndConcurrency(modulePath, pluginAPath, depAPath, pluginBPath, depBPath);
        unicodeAndChinesePathLoading(pluginAPath, depAPath);
        missingDependencyIsolation(appPath, pluginAPath, pluginBPath, depBPath);
#if defined(VISIONRT_BUILD_APP)
        appControllerToolsPackageIntegration(modulePath);
#endif
        registryIntegration(modulePath);
        std::cout << "plugin contracts passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Plugin contract failed: " << error.what() << '\n';
        return 1;
    }
}
