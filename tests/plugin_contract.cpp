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
#include <QLibrary>
#include <QTemporaryDir>
#include <QProcess>
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

    // 1. Filename rejection: only module_index.yaml is supported
    loader->loadIndexFile(baseDir / "non_existent.yml");
    assert(!loader->diagnostics().empty());
    assert(loader->diagnostics().back().message.find("only module_index.yaml is supported") != std::string::npos);
    loader->clearDiagnostics();

    loader->loadIndexFile(baseDir / "other_index.yaml");
    assert(!loader->diagnostics().empty());
    assert(loader->diagnostics().back().message.find("only module_index.yaml is supported") != std::string::npos);
    loader->clearDiagnostics();

    const auto badJson = baseDir / "bad.json";
    {
        QFile f(toQString(badJson));
        assert(f.open(QIODevice::WriteOnly));
        f.write("{\"variant\": \"cpu\"}");
    }
    loader->loadIndexFile(badJson);
    assert(!loader->diagnostics().empty());
    assert(loader->diagnostics().back().message.find("only module_index.yaml is supported") != std::string::npos);
    loader->clearDiagnostics();

    // 2. Non-existent module_index.yaml
    loader->loadIndexFile(baseDir / "module_index.yaml");
    assert(!loader->diagnostics().empty());
    assert(loader->diagnostics().back().message.find("does not exist") != std::string::npos);
    loader->clearDiagnostics();

    // 3. Bad YAML syntax
    const auto indexFile = baseDir / "module_index.yaml";
    {
        QFile f(toQString(indexFile));
        assert(f.open(QIODevice::WriteOnly));
        f.write("modules: [ unclosed");
    }
    loader->loadIndexFile(indexFile);
    assert(!loader->diagnostics().empty());
    loader->clearDiagnostics();

    // 4. Missing required fields / empty modules
    {
        QFile f(toQString(indexFile));
        assert(f.open(QIODevice::WriteOnly));
        f.write("variant: cpu\nmodules: {}\ntools: {}\n");
    }
    loader->loadIndexFile(indexFile);
    assert(!loader->diagnostics().empty());
    loader->clearDiagnostics();

    // 5. Atomic rejection: if file contains dangling module reference or duplicate key, 0 tools are registered!
    {
        QFile f(toQString(indexFile));
        assert(f.open(QIODevice::WriteOnly));
        const char* content = R"(variant: cpu
modules:
  m1:
    library: bin/m1.dll
tools:
  t1:
    module: nonexistent_module
    definition: tools/t1.yaml
)";
        f.write(content);
    }
    loader->loadIndexFile(indexFile);
    assert(!loader->diagnostics().empty());
    // ATOMIC: no tool is registered from the invalid file!
    assert(!loader->hasModule("t1"));
    loader->clearDiagnostics();

    // 6. Existing valid registrations are untouched when an invalid index is rejected
    loader->registerModule("mod.preexisting", "bin/preexisting.dll");
    assert(loader->hasModule("mod.preexisting"));
    loader->loadIndexFile(indexFile);
    assert(!loader->diagnostics().empty());
    assert(loader->hasModule("mod.preexisting"));
    assert(!loader->hasModule("t1"));
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

    // Create module_index.yaml
    {
        QFile f(toQString(pkgDir / "module_index.yaml"));
        assert(f.open(QIODevice::WriteOnly));
        const char* indexContent = R"(variant: cpu
modules:
  test_module:
    library: bin/test_module.dll
tools:
  test.plugin.counter:
    module: test_module
    definition: assets/tools/builtin/测试/数学/test.plugin.counter.yaml
)";
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

void windowsDllLoadingAndConcurrency(const std::filesystem::path& modulePath,
                                     const std::filesystem::path& pluginAPath,
                                     const std::filesystem::path& depAPath,
                                     const std::filesystem::path& pluginBPath,
                                     const std::filesystem::path& depBPath) {
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

    ToolStatus status1, status2;
    std::shared_ptr<const LoadedModule> m1, m2;
    std::int64_t val1 = 0, val2 = 0;

    std::thread t1([&] {
        m1 = loader1->loadModule(dll1, status1);
        if (status1.code == 0 && m1 != nullptr) {
            loader1->registerModule("test.plugin.a", dll1);
            PlatformToolServices s;
            auto ad = loader1->createAdapter("test.plugin.a", s);
            if (ad && ad->initialize({}).code == 0) {
                auto r = ad->execute({});
                if (r.status.code == 0) {
                    val1 = std::get<std::int64_t>(r.outputs.at("val"));
                }
                ad->release();
            }
        }
    });

    std::thread t2([&] {
        m2 = loader2->loadModule(dll2, status2);
        if (status2.code == 0 && m2 != nullptr) {
            loader2->registerModule("test.plugin.b", dll2);
            PlatformToolServices s;
            auto ad = loader2->createAdapter("test.plugin.b", s);
            if (ad && ad->initialize({}).code == 0) {
                auto r = ad->execute({});
                if (r.status.code == 0) {
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

void unicodeAndChinesePathLoading(const std::filesystem::path& pluginAPath,
                                  const std::filesystem::path& depAPath) {
    QTemporaryDir temp;
    assert(temp.isValid());
    const auto baseDir = std::filesystem::u8path(temp.path().toUtf8().toStdString());
    const auto unicodeDir = baseDir / "测试 插件 路径 with space";
    const auto binDir = unicodeDir / "bin";
    std::filesystem::create_directories(binDir);

    const auto targetDll = binDir / "plugin_a.dll";
    const auto targetDep = binDir / depAPath.filename();
    std::filesystem::copy_file(pluginAPath, targetDll, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(depAPath, targetDep, std::filesystem::copy_options::overwrite_existing);

    // Create module_index.yaml inside unicodeDir
    const auto indexFile = unicodeDir / "module_index.yaml";
    {
        QFile f(toQString(indexFile));
        assert(f.open(QIODevice::WriteOnly));
        const std::string content =
            "variant: cpu\n"
            "modules:\n"
            "  mod_a:\n"
            "    library: bin/plugin_a.dll\n"
            "tools:\n"
            "  test.plugin.a:\n"
            "    module: mod_a\n"
            "    definition: assets/tools/a.yaml\n";
        f.write(content.c_str(), content.size());
    }

    auto loader = std::make_shared<PluginLoader>();
    loader->setBaseDirectory(unicodeDir);
    loader->loadIndexFile(indexFile);
    assert(loader->diagnostics().empty());
    assert(loader->hasModule("test.plugin.a"));

    PlatformToolServices services;
    auto adapter = loader->createAdapter("test.plugin.a", services);
    assert(adapter != nullptr);
    auto initStatus = adapter->initialize({});
    if (initStatus.code != 0) {
        std::cerr << "Init failed: code=" << initStatus.code << " msg=" << initStatus.message << "\n";
    }
    assert(initStatus.code == 0);
    auto res = adapter->execute({});
    assert(res.status.code == 0);
    assert(std::get<std::int64_t>(res.outputs.at("val")) == 111);

    // Cross-release snapshot check: hold output while releasing adapter and loader
    const auto retainedVal = res.outputs.at("val");
    assert(adapter->release().code == 0);
    adapter.reset();
    loader.reset();
    assert(std::get<std::int64_t>(retainedVal) == 111);
}

static int runMissingDepChild(const std::filesystem::path& pluginAPath,
                               const std::filesystem::path& pluginBPath,
                               const std::filesystem::path& depBPath) {
    QTemporaryDir temp;
    if (!temp.isValid()) return 10;
    const auto baseDir = std::filesystem::u8path(temp.path().toUtf8().toStdString());
    const auto dirFail = baseDir / "dir_fail";
    const auto dirOk = baseDir / "dir_ok";
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

    auto loaderFail = std::make_shared<PluginLoader>();
    ToolStatus statusMissing;
    auto missingMod = loaderFail->loadModule(missingDepDll, statusMissing);
    if (missingMod != nullptr) {
        std::cerr << "Expected missingMod == nullptr, but loaded successfully!\n";
        return 1;
    }
    if (statusMissing.code == 0) {
        std::cerr << "Expected failure status code, got 0\n";
        return 2;
    }
#ifdef _WIN32
    // Must fail in the dynamic link loader path (Win32 error 126: ERROR_MOD_NOT_FOUND)
    if (statusMissing.message.find("126") == std::string::npos &&
        statusMissing.message.find("Win32 error") == std::string::npos) {
        std::cerr << "Expected Win32 error in message: " << statusMissing.message << "\n";
        return 3;
    }
#endif

    // Ensure loader Ok is not poisoned and can still load and execute valid module B
    auto loaderOk = std::make_shared<PluginLoader>();
    ToolStatus statusValid;
    auto validMod = loaderOk->loadModule(okDll, statusValid);
    if (validMod == nullptr || statusValid.code != 0) {
        std::cerr << "Failed to load okDll: " << statusValid.message << "\n";
        return 4;
    }
    loaderOk->registerModule("test.plugin.b", okDll);
    PlatformToolServices services;
    auto ad = loaderOk->createAdapter("test.plugin.b", services);
    if (!ad || ad->initialize({}).code != 0) {
        std::cerr << "Failed to initialize ok adapter\n";
        return 5;
    }
    auto res = ad->execute({});
    if (res.status.code != 0) {
        std::cerr << "Failed to execute ok adapter\n";
        return 6;
    }
    if (std::get<std::int64_t>(res.outputs.at("val")) != 222) {
        std::cerr << "Expected val 222, got " << std::get<std::int64_t>(res.outputs.at("val")) << "\n";
        return 7;
    }
    ad->release();
    return 0;
}

void missingDependencyIsolation(const QString& appPath,
                                const std::filesystem::path& pluginAPath,
                                const std::filesystem::path& pluginBPath,
                                const std::filesystem::path& depBPath) {
    QProcess proc;
    QStringList args;
    args << "--run-missing-dep-child"
         << toQString(pluginAPath)
         << toQString(pluginBPath)
         << toQString(depBPath);
    proc.start(appPath, args);
    assert(proc.waitForFinished(10000));
    if (proc.exitCode() != 0) {
        std::cerr << "Child process stdout:\n" << proc.readAllStandardOutput().toStdString() << "\n";
        std::cerr << "Child process stderr:\n" << proc.readAllStandardError().toStdString() << "\n";
    }
    assert(proc.exitStatus() == QProcess::NormalExit);
    assert(proc.exitCode() == 0);
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--run-missing-dep-child") {
        if (argc < 5) return 11;
        QGuiApplication qt(argc, argv);
        return runMissingDepChild(std::filesystem::u8path(argv[2]),
                                  std::filesystem::u8path(argv[3]),
                                  std::filesystem::u8path(argv[4]));
    }

    if (argc < 2) {
        std::cerr << "Usage: plugin_contract <path_to_test_plugin_module.dll> [plugin_a] [dep_a] [plugin_b] [dep_b]\n";
        return 1;
    }
    QGuiApplication qt(argc, argv);
    const QString appPath = argv[0];
    const auto modulePath = std::filesystem::u8path(argv[1]);
    const auto pluginAPath = argc > 2 ? std::filesystem::u8path(argv[2]) : modulePath;
    const auto depAPath = argc > 3 ? std::filesystem::u8path(argv[3]) : modulePath;
    const auto pluginBPath = argc > 4 ? std::filesystem::u8path(argv[4]) : modulePath;
    const auto depBPath = argc > 5 ? std::filesystem::u8path(argv[5]) : modulePath;

    try {
        lifecycleAndReentrancy(modulePath);
        singleModuleLoadOnlyOnce(modulePath);
        faultInjection(modulePath);
        advancedFaultInjectionAndModuleDestroy(modulePath);
        cppFunctionLifecycleAndNodeRuntime(modulePath);
        moduleIndexDiagnostics();
        windowsDllLoadingAndConcurrency(modulePath, pluginAPath, depAPath, pluginBPath, depBPath);
        unicodeAndChinesePathLoading(pluginAPath, depAPath);
        missingDependencyIsolation(appPath, pluginAPath, pluginBPath, depBPath);
        appControllerToolsPackageIntegration(modulePath);
        registryIntegration(modulePath);
        std::cout << "plugin contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Plugin contract failed: " << error.what() << '\n';
        return 1;
    }
}

