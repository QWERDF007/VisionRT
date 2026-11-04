#include "core/CustomToolStore.h"
#include "core/DefinitionJson.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <cassert>
#include <fstream>
#include <iostream>

using namespace visionrt;

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir tempDir;
    assert(tempDir.isValid());
    const std::filesystem::path root(tempDir.path().toStdWString());

    // The default root is the software directory the executable lives in: user tools travel with the
    // installation and never land in a per-user profile or on the system drive.
    const auto appDirectory = std::filesystem::weakly_canonical(
        std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()));
    assert(CustomToolStore::defaultRootDir() == appDirectory / "custom-tools");

    CustomToolStore store(root);
    assert(store.rootDir() == root);
    assert(store.declarationsDir() == root / "tools");
    assert(store.pythonDir() == root / "python");

    // 1. A new tool's script returns every declared business output as Missing under a
    // "not implemented" status, so an untouched tool fails honestly instead of breaking the output
    // contract on its first run.
    ToolDefinition templateSource;
    templateSource.kind   = ToolKind::PythonFunction;
    templateSource.nameZh = "测试函数";
    templateSource.outputs.push_back(PortDefinition{
        .id = 2, .nameZh = "输出图像", .type = parseTypeToken("image")});
    templateSource.outputs.push_back(PortDefinition{
        .id = 5, .nameZh = "计数", .type = parseTypeToken("int")});

    const auto fnTemplate = CustomToolStore::pythonTemplate(templateSource);
    assert(fnTemplate.find("def execute(inputs)") != std::string::npos);
    assert(fnTemplate.find("inputs.get(0, 0)") != std::string::npos);
    assert(fnTemplate.find("\"outputs\": {0: 1, 1: \"工具尚未实现\", 2: None, 5: None}") != std::string::npos);

    templateSource.kind = ToolKind::PythonClass;
    const auto clsTemplate = CustomToolStore::pythonTemplate(templateSource);
    assert(clsTemplate.find("class Tool:") != std::string::npos);
    assert(clsTemplate.find("def initialize") != std::string::npos);
    assert(clsTemplate.find("def release") != std::string::npos);
    assert(clsTemplate.find("2: None, 5: None") != std::string::npos);
    assert(clsTemplate.find("initialize") < clsTemplate.find("def execute"));

    // 2. Save a custom tool (JSON-last: python script is written first)
    ToolDefinition def;
    def.schemaVersion = 6;
    def.nameEn        = "custom.test.demo";
    def.nameZh        = "演示自定义工具";
    def.version       = "1.0.0";
    def.kind          = ToolKind::PythonFunction;
    def.entry.package = "custom/test/demo";
    def.entry.file    = "tool.py";
    def.entry.symbol  = "execute";
    def.nextInputPortId = 3;
    def.nextOutputPortId = 3;
    def.inputs.push_back(PortDefinition{
        .id = 1,
        .nameZh = "阈值",
        .type = parseTypeToken("int"),
        .defaultValue = std::int64_t(128)
    });
    def.inputs.push_back(PortDefinition{
        .id = 2,
        .nameZh = "图像",
        .type = parseTypeToken("image")
    });
    def.outputs.push_back(PortDefinition{
        .id = 2,
        .nameZh = "输出图像",
        .type = parseTypeToken("image")
    });

    // A declared resource belongs to the tool, so a copy has to carry it.
    const auto resourceDir = root / "python" / "custom/test/demo";
    std::filesystem::create_directories(resourceDir);
    std::ofstream(resourceDir / "weights.txt") << "weights";
    def.resources.push_back("weights.txt");

    const auto saved = store.saveCustomTool(def);

    // Verify script file and json file exist
    const auto scriptPath = root / "python" / "custom/test/demo" / "tool.py";
    const auto jsonPath   = root / "tools" / "custom.test.demo.json";
    assert(std::filesystem::exists(scriptPath));
    assert(std::filesystem::exists(jsonPath));
    assert(store.isUserTool(saved));
    // The authored draft is not rewritten: normalisation works on the stored copy.
    assert(def.inputs.size() == 2);
    assert(saved.inputs.size() == 3);

    // 3. Discover custom tools (materialised definition has input 0 and outputs 0/1 injected)
    auto tools = store.discoverCustomTools();
    assert(tools.size() == 1);
    assert(tools[0].definition.nameEn == "custom.test.demo");
    assert(tools[0].definition.nameZh == "演示自定义工具");
    assert(tools[0].definition.inputs.size() == 3);
    assert(tools[0].definition.inputs[0].id == 0);
    assert(tools[0].definition.inputs[1].id == 1);
    assert(tools[0].definition.inputs[2].id == 2);
    assert(tools[0].definition.outputs.size() == 3);
    assert(tools[0].definition.outputs[0].id == 0);
    assert(tools[0].definition.outputs[1].id == 1);
    assert(tools[0].definition.outputs[2].id == 2);
    assert(tools[0].definition.nextInputPortId == 3);
    assert(tools[0].definition.nextOutputPortId == 3);

    // 4. Clone custom tool
    const auto cloned = store.cloneCustomTool(tools[0].definition, "custom.test.demo_copy1", "演示自定义工具 副本");
    assert(cloned.nameEn == "custom.test.demo_copy1");
    assert(cloned.nameZh == "演示自定义工具 副本");
    assert(cloned.nextInputPortId == 3);
    assert(cloned.nextOutputPortId == 3);
    assert(cloned.inputs.size() == 3);
    assert(cloned.inputs[0].id == 0);
    assert(cloned.inputs[1].id == 1);
    assert(cloned.inputs[2].id == 2);
    const auto clonedScript = root / "python" / cloned.entry.package / "tool.py";
    assert(std::filesystem::exists(clonedScript));
    assert(std::filesystem::exists(root / "python" / cloned.entry.package / "weights.txt"));

    tools = store.discoverCustomTools();
    assert(tools.size() == 2);
    assert(store.findCustomTool("custom.test.demo_copy1").has_value());
    assert(!store.findCustomTool("custom.test.missing").has_value());

    // 5. A clone never overwrites a published tool: the caller picks the next free id.
    bool refused = false;
    try
    {
        store.cloneCustomTool(tools[0].definition, "custom.test.demo_copy1", "重复副本");
    }
    catch (const std::exception &)
    {
        refused = true;
    }
    assert(refused);
    assert(store.discoverCustomTools().size() == 2);

    // 5b. A copy without the source script would publish a tool that cannot run: the id alone is
    // not a copy.
    std::filesystem::remove(root / "python" / "custom/test/demo_copy1" / "tool.py");
    bool shellRefused = false;
    try
    {
        store.cloneCustomTool(store.findCustomTool("custom.test.demo_copy1")->definition,
                              "custom.test.shell", "空壳副本");
    }
    catch (const std::exception &)
    {
        shellRefused = true;
    }
    assert(shellRefused);
    assert(!store.findCustomTool("custom.test.shell"));

    // 5c. A native tool is a registered factory, not a declaration, so copying its JSON would
    // create a tool nothing can instantiate.
    ToolDefinition native;
    native.schemaVersion = 6;
    native.nameEn        = "builtin.native.demo";
    native.nameZh        = "原生工具";
    native.kind          = ToolKind::Cpp;
    bool nativeRefused   = false;
    try
    {
        store.cloneCustomTool(native, "custom.native.copy", "原生副本");
    }
    catch (const std::exception &)
    {
        nativeRefused = true;
    }
    assert(nativeRefused);

    // 5d. An installed Python example can be copied into the user root: the script and resources are
    // read from the package root the definition was discovered under, not from the editor's store.
    const std::filesystem::path installedRoot = root / "installed";
    const auto installedPackage = installedRoot / "python" / "builtin" / "examples" / "installed";
    std::filesystem::create_directories(installedPackage);
    {
        std::ofstream script(installedPackage / "tool.py", std::ios::binary);
        script << "def execute(inputs):\n    return {\"outputs\": {0: 0, 1: \"\"}, \"detail\": \"\"}\n";
        std::ofstream resource(installedPackage / "weights.txt", std::ios::binary);
        resource << "weights";
    }

    ToolDefinition installed;
    installed.schemaVersion = 6;
    installed.nameEn        = "examples.installed";
    installed.nameZh        = "内置示例";
    installed.kind          = ToolKind::PythonFunction;
    installed.entry.package = "examples/installed";
    installed.entry.file    = "tool.py";
    installed.entry.symbol  = "execute";
    installed.resources.push_back("weights.txt");
    installed.source.origin            = ToolOrigin::Builtin;
    installed.source.roots.declarations = installedRoot / "tools";
    installed.source.roots.python       = installedRoot / "python" / "builtin";
    normalizeToolDefinition(installed);

    const auto installedCopy = store.cloneCustomTool(installed, "custom.test.installed_copy", "内置示例 副本");
    assert(installedCopy.source.origin == ToolOrigin::Custom);
    assert(std::filesystem::exists(root / "python" / installedCopy.entry.package / "tool.py"));
    assert(std::filesystem::exists(root / "python" / installedCopy.entry.package / "weights.txt"));
    // The installed original is untouched: a copy adds a declaration, it does not move one.
    assert(std::filesystem::exists(installedPackage / "tool.py"));
    assert(std::filesystem::exists(installedRoot / "python" / "builtin" / "examples" / "installed" / "weights.txt"));
    // The copy is an ordinary own tool; removing it keeps the later counts about this store's own
    // fixtures rather than about this one extra copy.
    assert(store.deleteCustomTool("custom.test.installed_copy"));

    // 6. A tool id names a declaration file, so it cannot leave the store root.
    bool rejected = false;
    try
    {
        store.deleteCustomTool("../escape");
    }
    catch (const std::exception &)
    {
        rejected = true;
    }
    assert(rejected);

    // 7. Delete custom tool
    assert(store.deleteCustomTool("custom.test.demo_copy1"));
    tools = store.discoverCustomTools();
    assert(tools.size() == 1);
    assert(!std::filesystem::exists(root / "tools" / "custom.test.demo_copy1.json"));
    assert(!store.deleteCustomTool("custom.test.demo_copy1"));

    std::cout << "[PASS] custom_tool_store_contract" << std::endl;
    return 0;
}
