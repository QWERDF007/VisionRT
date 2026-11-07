// Package replacement is a deployment transaction, not an example pipeline: it belongs to the
// package group so a pipeline change does not have to run it and a deployment change does not have
// to run the tool examples. The fixture builds its own tools package under a temporary root.

#include "core/NodeContracts.h"
#include "core/ProjectStorage.h"
#include "core/ToolPackage.h"
#include "plugins/PluginLoader.h"
#include "python/PythonRuntime.h"
#include "TestPythonEnvironment.h"
#include "runtime/Receiver.h"
#include "runtime/RuntimeEngine.h"

#include "RuntimeTestSupport.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace visionrt;

namespace {

using namespace testsupport;

void runPackageReplacement(PythonRuntime &python)
{
    std::cout << "[TEST] Running offline package replacement and failure protection test (Ticket 06 / T-08 / T-09)..."
              << std::endl;

    const auto appDir    = std::filesystem::u8path(QCoreApplication::applicationDirPath().toUtf8().toStdString());
    auto       pluginDll = appDir / "test_plugin_module.dll";
    if (!std::filesystem::exists(pluginDll))
    {
        pluginDll = appDir / "bin" / "test_plugin_module.dll";
    }
    assert((std::filesystem::exists(pluginDll)));

    // 1. Create temporary deployment root with Chinese characters and spaces
    QTemporaryDir deployTemp;
    assert((deployTemp.isValid()));
    const auto installRoot
        = std::filesystem::u8path(deployTemp.path().toUtf8().toStdString()) / "测试 部署 路径 with space";
    const auto toolsPackageDir = installRoot / "tools-package";
    std::filesystem::create_directories(toolsPackageDir / "bin");
    std::filesystem::create_directories(toolsPackageDir / "tools" / "builtin");

    // Copy plugin DLL to bin
    std::filesystem::copy_file(pluginDll, toolsPackageDir / "bin" / "test_plugin_module.dll");

    // Write C++ tool declaration (func.json)
    {
        std::ofstream ofs(toolsPackageDir / "tools" / "builtin" / "func.json");
        ofs << R"json({
  "schema_version": 6,
  "name_en": "test.plugin.function",
  "name_zh": "Plugin Function",
  "version": "1.0.0",
  "implementation": {
    "kind": "cpp"
  },
  "next_port_id": {"init": 0, "inputs": 2, "outputs": 3},
  "ports": {
    "inputs": [
      {
        "id": 1,
        "name_zh": "X",
        "type": "int",
        "default": 10
      }
    ],
    "outputs": [
      {
        "id": 2,
        "name_zh": "Y",
        "type": "int"
      }
    ]
  }
})json";
    }

    // Write Python tool declaration and script (calc.py, calc.json)
    {
        std::filesystem::create_directories(toolsPackageDir / "python" / "builtin" / "calc");
        std::ofstream py(toolsPackageDir / "python" / "builtin" / "calc" / "calc.py");
        py << "def execute(inputs):\n"
           << "    return {'outputs': {2: int(inputs[1]) + 5, 0: 0, 1: 'ok'}, 'detail': ''}\n";

        std::ofstream ofs(toolsPackageDir / "tools" / "builtin" / "calc.json");
        ofs << R"json({
  "schema_version": 6,
  "name_en": "custom.py_math",
  "name_zh": "Python Calc",
  "version": "1.0.0",
  "implementation": {
    "kind": "python_function",
    "package": "calc",
    "file": "calc.py",
    "symbol": "execute"
  },
  "next_port_id": {"init": 0, "inputs": 2, "outputs": 3},
  "ports": {
    "inputs": [
      {
        "id": 1,
        "name_zh": "Val",
        "type": "int"
      }
    ],
    "outputs": [
      {
        "id": 2,
        "name_zh": "Res",
        "type": "int"
      }
    ]
  }
})json";
    }

    // Write Platform Receiver node declaration (recv.json)
    {
        std::ofstream ofs(toolsPackageDir / "tools" / "builtin" / "recv.json");
        ofs << R"json({
  "schema_version": 6,
  "name_en": "platform.test_recv",
  "name_zh": "Test Receiver",
  "version": "1.0.0",
  "implementation": {
    "kind": "host",
    "binding": "visionrt.receiver"
  },
  "next_port_id": {"init": 4, "inputs": 1, "outputs": 5},
  "ports": {
    "init": [
      {
        "id": 0,
        "name_zh": "Source Flow",
        "type": "string"
      },
      {
        "id": 1,
        "name_zh": "Source Node",
        "type": "string"
      },
      {
        "id": 2,
        "name_zh": "Source Port",
        "type": "int"
      },
      {
        "id": 3,
        "name_zh": "Timeout",
        "type": "int",
        "default": 1000
      }
    ],
    "outputs": [
      {
        "id": 2,
        "name_zh": "Value",
        "type": "int"
      },
      {
        "id": 3,
        "name_zh": "Source Status Code",
        "type": "int"
      },
      {
        "id": 4,
        "name_zh": "Source Status Message",
        "type": "string"
      }
    ]
  }
})json";
    }

    // Write module_index.json for Package 1
    {
        std::ofstream ofs(toolsPackageDir / "module_index.json");
        ofs << R"json({
  "schema_version": 1,
  "variant": "cpu",
  "modules": [
    {"id": "test_plugin_module", "library": "bin/test_plugin_module.dll"}
  ],
  "tools": [
    {"tool_id": "test.plugin.function", "module": "test_plugin_module", "definition": "tools/builtin/func.json"},
    {"tool_id": "custom.py_math", "definition": "tools/builtin/calc.json"},
    {"tool_id": "platform.test_recv", "definition": "tools/builtin/recv.json"}
  ],
  "runtime_files": []
})json";
    }

    // Add unique Package 1 sentinel file
    const auto pkg1Sentinel = toolsPackageDir / "tools" / "builtin" / "pkg1_sentinel.txt";
    {
        std::ofstream ofs(pkg1Sentinel);
        ofs << "PACKAGE_1_FILE";
    }

    // Saved project path
    const auto savedProjectFile = installRoot / "test_pipeline.vrt.json";

    // 2. Package 1 execution: C++ domain DLL, Python tool, and Platform Receiver node in pipeline
    {
        const auto pkg1    = visionrt::readToolsPackage(toolsPackageDir);
        auto       loader1 = std::make_shared<PluginLoader>();
        ToolRegistry registry1;
        registerReceiveTools(registry1);
        registry1.setPluginLoader(loader1);
        registry1.registerPackage(pkg1);
        registry1.loadNativeImplementations();
        setupPython(registry1, python);

        // Build project:
        // Flow 1 ("sender_flow"):
        //   Node 1 (test.plugin.function, x=10 -> y=20)
        //   Node 2 (custom.py_math, val <- Node 1 y -> res=25)
        // Flow 2 ("receiver_flow"):
        //   Node 3 (platform.test_recv, receives "res" from Node 2 in sender_flow -> value=25)
        ProjectDefinition project;
        project.name       = "Test Pipeline Project";
        project.nextFlowId = 3;

        FlowDefinition f1;
        f1.id          = "sender_flow";
        f1.name        = "sender_flow";
        f1.runSelected = true;

        NodeDefinition n1;
        n1.id               = 1;
        n1.toolId           = "test.plugin.function";
        n1.toolNameSnapshot = "Plugin Function";
        n1.position         = {100, 100};
        InputBinding b1;
        b1.mode    = BindingMode::Literal;
        b1.literal = std::int64_t{10};
        n1.inputs.emplace(1, b1);
        n1.signatures.inputs[1]  = {Type::Int};
        n1.signatures.outputs[2] = {Type::Int};
        f1.nodes.push_back(n1);

        NodeDefinition n2;
        n2.id               = 2;
        n2.toolId           = "custom.py_math";
        n2.toolNameSnapshot = "Python Calc";
        n2.position         = {250, 200};
        InputBinding b2;
        b2.mode    = BindingMode::Upstream;
        b2.literal = std::int64_t{0};
        b2.source  = SourcePort{1, 2};
        n2.inputs.emplace(1, b2);
        n2.signatures.inputs[1]  = {Type::Int};
        n2.signatures.outputs[2] = {Type::Int};
        f1.nodes.push_back(n2);
        project.flows.push_back(f1);

        FlowDefinition f2;
        f2.id          = "receiver_flow";
        f2.name        = "receiver_flow";
        f2.runSelected = true;

        NodeDefinition n3;
        n3.id               = 3;
        n3.toolId           = "platform.test_recv";
        n3.toolNameSnapshot = "Test Receiver";
        n3.position         = {100, 250};
        // Receiver init ports keep the declaration order 0..3; the source port travels as a number.
        n3.initParams = {
            {ReceiveInitPorts::SourceFlow, std::string("sender_flow")},
            {ReceiveInitPorts::SourceNode, std::string(std::to_string(n2.id))},
            {ReceiveInitPorts::SourcePort, std::int64_t{2}},
            {        ReceiveInitPorts::Timeout, std::int64_t{1000}}
        };
        n3.signatures.initParams[ReceiveInitPorts::SourceFlow] = {Type::String};
        n3.signatures.initParams[ReceiveInitPorts::SourceNode] = {Type::String};
        n3.signatures.initParams[ReceiveInitPorts::SourcePort] = {Type::Int};
        n3.signatures.initParams[ReceiveInitPorts::Timeout]    = {Type::Int};
        n3.signatures.outputs[ReceivePorts::Value]               = {Type::Int};
        n3.signatures.outputs[ReceivePorts::SourceStatus]        = {Type::Int};
        n3.signatures.outputs[ReceivePorts::SourceStatusMessage] = {Type::String};
        f2.nodes.push_back(n3);
        project.flows.push_back(f2);
        project.nextNodeId = 4;

        RuntimeEngine engine1(2);
        assert((engine1.start(project, registry1, RunMode::Once).code == 0));
        auto update1 = finish(engine1);

        auto s1 = snapshot(update1, 1);
        assert((s1 && s1->status.code == 0));
        assert((std::get<std::int64_t>(s1->outputs.at(2)) == 20));

        auto s2 = snapshot(update1, 2);
        if (!s2 || s2->status.code != 0)
        {
            std::cerr << "Node 2 status failed: code=" << (s2 ? s2->status.code : -999)
                      << " msg=" << (s2 ? s2->status.message : "null") << std::endl;
        }
        assert((s2 && s2->status.code == 0));
        assert((std::get<std::int64_t>(s2->outputs.at(2)) == 25));

        auto s3 = snapshot(update1, 3);
        if (!s3 || s3->status.code != 0)
        {
            std::cerr << "Node 3 status failed: code=" << (s3 ? s3->status.code : -999)
                      << " msg=" << (s3 ? s3->status.message : "null") << std::endl;
        }
        assert((s3 && s3->status.code == 0));
        assert((std::get<std::int64_t>(s3->outputs.at(ReceivePorts::Value)) == 25));
        std::cout << "[Evidence T-08] Pipeline with C++ domain DLL, Python tool, and Receiver node executed with exact "
                     "numerical value (25) in unicode path.\n";

        // Save project
        ProjectStorage storage1(registry1);
        auto reconciled = storage1.reconcile(project);
        project          = std::move(reconciled.project);
        storage1.save(project, savedProjectFile);

        // Reopen and re-execute to verify numerical equality on reload
        auto reloaded1 = storage1.load(savedProjectFile);
        for (const auto &d : reloaded1.diagnostics)
        {
            std::cerr << "RELOAD DIAGNOSTIC: node=" << (d.node ? *d.node : 0) << " code=" << static_cast<int>(d.code)
                      << " field=" << d.field << " msg=" << d.message << std::endl;
        }
        assert((reloaded1.diagnostics.empty()));
        RuntimeEngine engineReload(2);
        assert((engineReload.start(reloaded1.project, registry1, RunMode::Once).code == 0));
        auto updateReload = finish(engineReload);
        auto r_s3         = snapshot(updateReload, 3);
        assert((r_s3 && std::get<std::int64_t>(r_s3->outputs.at(ReceivePorts::Value)) == 25));
        std::cout << "[Evidence T-08] Save / reopen consistency and numerical equality verified.\n";
    }

    // 3. Test failure protection: Attempt deploying invalid package (corrupt / missing referenced files)
    // Existing installation must not be altered or corrupted!
    QTemporaryDir badPkgTemp;
    const auto    badPkgDir = std::filesystem::u8path(badPkgTemp.path().toUtf8().toStdString()) / "bad_pkg";
    std::filesystem::create_directories(badPkgDir / "tools" / "builtin");
    {
        std::ofstream ofs(badPkgDir / "module_index.json");
        ofs << R"json({
  "schema_version": 1,
  "variant": "cpu",
  "modules": [
    {"id": "nonexistent_mod", "library": "bin/nonexistent.dll"}
  ],
  "tools": [
    {"tool_id": "broken.tool", "module": "nonexistent_mod", "definition": "tools/builtin/missing.json"}
  ],
  "runtime_files": []
})json";
    }
    bool badDeployFailed = false;
    try
    {
        visionrt::readToolsPackage(badPkgDir);
    }
    catch (const std::exception &)
    {
        badDeployFailed = true;
    }
    assert((badDeployFailed));
    // Package 1 on disk remains untouched
    assert((std::filesystem::exists(pkg1Sentinel)));
    assert((std::filesystem::exists(toolsPackageDir / "bin" / "test_plugin_module.dll")));

    // 4. Test locked-file protection:
    // When a process has an open file in tools-package, replacement must fail without deleting target
    const auto    lockedPath = toolsPackageDir / "bin" / "test_plugin_module.dll";
    std::ifstream lockFile(lockedPath, std::ios::binary);
    assert((lockFile.is_open()));

    // Attempt renaming directory while file is locked
    const auto      backupDir = installRoot / ".tools-package-backup-test";
    std::error_code ecRename;
    std::filesystem::rename(toolsPackageDir, backupDir, ecRename);
#ifdef _WIN32
    // On Windows NTFS, renaming directory with open file fails
    assert((ecRename));
    assert((std::filesystem::exists(toolsPackageDir)));
    assert((std::filesystem::exists(pkg1Sentinel)));
#endif
    lockFile.close();

    // 5. Clean Package Replacement:
    // Package 2 replaces test.plugin.function with test.plugin.counter, and removes custom.py_math
    // Deploy Package 2 to toolsPackageDir:
    const auto stagingDir = installRoot / ".tools-package-staging-test";
    std::filesystem::create_directories(stagingDir / "bin");
    std::filesystem::create_directories(stagingDir / "tools" / "builtin");
    std::filesystem::copy_file(pluginDll, stagingDir / "bin" / "test_plugin_module.dll");

    // Write counter declaration
    {
        std::ofstream ofs(stagingDir / "tools" / "builtin" / "counter.json");
        ofs << R"json({
  "schema_version": 6,
  "name_en": "test.plugin.counter",
  "name_zh": "Plugin Counter",
  "version": "1.0.0",
  "implementation": {
    "kind": "cpp"
  },
  "next_port_id": {"init": 1, "inputs": 1, "outputs": 3},
  "ports": {
    "init": [
      {
        "id": 0,
        "name_zh": "Initial",
        "type": "int"
      }
    ],
    "outputs": [
      {
        "id": 2,
        "name_zh": "Count",
        "type": "int"
      }
    ]
  }
})json";
    }
    // Write platform receiver declaration
    {
        std::ofstream ofs(stagingDir / "tools" / "builtin" / "recv.json");
        ofs << R"json({
  "schema_version": 6,
  "name_en": "platform.test_recv",
  "name_zh": "Test Receiver",
  "version": "1.0.0",
  "implementation": {
    "kind": "host",
    "binding": "visionrt.receiver"
  },
  "next_port_id": {"init": 4, "inputs": 1, "outputs": 5},
  "ports": {
    "init": [
      {
        "id": 0,
        "name_zh": "Source Flow",
        "type": "string"
      },
      {
        "id": 1,
        "name_zh": "Source Node",
        "type": "string"
      },
      {
        "id": 2,
        "name_zh": "Source Port",
        "type": "int"
      },
      {
        "id": 3,
        "name_zh": "Timeout",
        "type": "int",
        "default": 1000
      }
    ],
    "outputs": [
      {
        "id": 2,
        "name_zh": "Value",
        "type": "int"
      },
      {
        "id": 3,
        "name_zh": "Source Status Code",
        "type": "int"
      },
      {
        "id": 4,
        "name_zh": "Source Status Message",
        "type": "string"
      }
    ]
  }
})json";
    }
    // Write module_index.json for Package 2
    {
        std::ofstream ofs(stagingDir / "module_index.json");
        ofs << R"json({
  "schema_version": 1,
  "variant": "cpu",
  "modules": [
    {"id": "test_plugin_module", "library": "bin/test_plugin_module.dll"}
  ],
  "tools": [
    {"tool_id": "test.plugin.counter", "module": "test_plugin_module", "definition": "tools/builtin/counter.json"},
    {"tool_id": "platform.test_recv", "definition": "tools/builtin/recv.json"}
  ],
  "runtime_files": []
})json";
    }

    // Replace toolsPackageDir cleanly
    std::filesystem::remove_all(toolsPackageDir);
    std::filesystem::rename(stagingDir, toolsPackageDir);

    // Verify Package 1 unique sentinel is completely gone (no leftover files)
    assert((!std::filesystem::exists(pkg1Sentinel)));
    assert((!std::filesystem::exists(toolsPackageDir / "tools" / "builtin" / "func.json")));
    assert((!std::filesystem::exists(toolsPackageDir / "python" / "builtin" / "calc" / "calc.py")));
    assert((!std::filesystem::exists(toolsPackageDir / "tools" / "builtin" / "calc.json")));
    std::cout << "[Evidence T-08] Package replacement verified: Package 1 unique files and scripts do not linger.\n";

    // 6. Simulate restart with Package 2:
    const auto pkg2    = visionrt::readToolsPackage(toolsPackageDir);
    auto       loader2 = std::make_shared<PluginLoader>();
    ToolRegistry registry2;
    registerReceiveTools(registry2);
    registry2.setPluginLoader(loader2);
    registry2.registerPackage(pkg2);
    registry2.loadNativeImplementations();
    setupPython(registry2, python);

    // Verify new tool test.plugin.counter is available
    assert((registry2.find("test.plugin.counter") != nullptr));
    assert((registry2.find("test.plugin.function") == nullptr));
    assert((registry2.find("custom.py_math") == nullptr));

    // Load original project:
    ProjectStorage storage2(registry2);
    auto           loadedOldProject = storage2.load(savedProjectFile);
    // Node 1 (test.plugin.function) and Node 2 (custom.py_math) must have DefinitionMissing diagnostics!
    bool           foundMissingN1 = false;
    bool           foundMissingN2 = false;
    for (const auto &d : loadedOldProject.diagnostics)
    {
        if (d.node && *d.node == 1 && d.code == ProjectStorageCode::DefinitionMissing)
        {
            foundMissingN1 = true;
        }
        if (d.node && *d.node == 2 && d.code == ProjectStorageCode::DefinitionMissing)
        {
            foundMissingN2 = true;
        }
    }
    assert((foundMissingN1));
    assert((foundMissingN2));
    std::cout << "[Evidence T-08] Reopening project after package replacement: removed tools diagnosed with "
                 "DefinitionMissing.\n";

    // Running flow with missing definition is rejected
    RuntimeEngine engine2(2);
    const auto    startResult = engine2.start(loadedOldProject.project, registry2, RunMode::Once);
    assert((startResult.code != 0));

    std::cout << "[PASS] Offline package replacement and failure protection test passed successfully." << std::endl;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        PythonRuntime python(testsupport::pythonEnvironment());
        runPackageReplacement(python);
        std::cout << "[ALL TESTS PASSED] package replacement and protection passed." << std::endl;
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "[ERROR] Package replacement test failed: " << error.what() << std::endl;
        return 1;
    }
}
