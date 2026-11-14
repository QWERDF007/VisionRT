#include "core/ExampleProject.h"
#include "core/ProjectStorage.h"
#include "core/ToolPackage.h"
#include "core/ToolRegistry.h"
#include "ui/AppController.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QTest>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void write(const std::filesystem::path &path, std::string_view bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(stream.good(), "cannot write fixture file");
}

std::string read(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

// One reader node whose image path is the folder the example ships next to itself.
ProjectDefinition exampleProject(const std::filesystem::path &path)
{
    ProjectDefinition project;
    project.name = "演示";
    project.path = path;
    NodeDefinition node;
    node.id                = 1;
    node.toolId            = "ReadImage";
    node.toolNameSnapshot  = "图像读取";
    node.alias             = "读图";
    node.inputs[1].mode    = BindingMode::Literal;
    node.inputs[1].literal = std::string("images");
    node.signatures.inputs[1] = TypeSpec{Type::String};
    project.flows.front().nodes.push_back(std::move(node));
    project.nextNodeId = 2;
    return project;
}
} // namespace

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    require(argc == 2, "Usage: example_project_contract <tools-package>");
    ToolRegistry registry;
    registry.registerPackage(readToolsPackage(std::filesystem::u8path(argv[1])));

    QTemporaryDir temporary;
    require(temporary.isValid(), "example fixture directory unavailable");
    const auto root     = std::filesystem::path(temporary.path().toStdWString()) / u8"安装 目录";
    const auto appDir   = root / "VisionRT";
    const auto examples = ExampleProject::directory(appDir);
    const auto example  = examples / u8"01_基础入门" / u8"演示" / u8"演示.vrt.json";
    write(example.parent_path() / "images" / "input_01.png", "first-image-bytes");

    // Recognition is a prefix of the installed examples, never the source tree or a user folder.
    require(ExampleProject::owns(appDir, example), "installed example was not recognised");
    require(!ExampleProject::owns(appDir, root / "用户" / "我的工程.vrt.json"),
            "a user project was mistaken for an installed example");
    require(!ExampleProject::owns(appDir, {}), "an unsaved project was reported as an installed example");

    // A copy of an example is an ordinary save: the literal travels unchanged, the resource stays in
    // the installation, and nothing is collected into the user folder behind the user's back.
    const auto     userProject = root / u8"用户" / u8"我的工程.vrt.json";
    require(std::filesystem::create_directories(userProject.parent_path()), "cannot create the user folder");
    ProjectStorage storage(registry);
    auto           copied = exampleProject(example);
    storage.save(copied, userProject);
    const auto saved = read(userProject);
    require(saved.find("\"images\"") != std::string::npos, "the saved project lost its relative image path");
    require(saved.find("examples") == std::string::npos, "the saved project points back into the installation");
    require(!std::filesystem::exists(userProject.parent_path() / "images"),
            "saving a copy collected example resources into the user folder");

    // The controller rule: an installed example never writes itself, and its copy leaves the
    // installation as an ordinary project that saves in place again.
    const auto installedProject = ExampleProject::directory(appDir) / u8"演示.vrt.json";
    write(installedProject,
          R"({"format_version":7,"project":{"name":"演示","next_node_seq":"1","next_flow_seq":"2"},)"
          R"("flows":[{"flow_id":"flow_1","name":"演示","run_selected":true,"nodes":[]}]})");
    const auto before = read(installedProject);

    AppController controller(1, QString::fromStdWString(appDir.wstring()));
    bool          replaced = false;
    bool          asked    = false;
    QObject::connect(&controller, &AppController::projectReplaced, [&] { replaced = true; });
    QObject::connect(&controller, &AppController::savePathRequired, [&] { asked = true; });
    controller.requestOpen(QString::fromStdWString(installedProject.wstring()));
    controller.resolveUnsaved("discard");
    require(QTest::qWaitFor([&] { return replaced; }, 15000), "the installed example did not open");

    require(!controller.save(), "Ctrl+S wrote an installed example");
    require(asked, "Ctrl+S did not ask for another location");

    const auto copy = root / u8"用户" / u8"示例副本.vrt.json";
    require(controller.save(QString::fromStdWString(copy.wstring())), "saving the example elsewhere failed");
    require(std::filesystem::is_regular_file(copy), "the user copy was not written");
    require(read(installedProject) == before, "the installation changed while saving a copy");

    asked = false;
    require(controller.save(), "the user copy no longer saves in place");
    require(!asked, "an ordinary project asked for another location");
    require(std::filesystem::is_regular_file(copy), "the user copy disappeared");

    std::cout << "Example project: recognised, exported with resources, conflict refused, example protected\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
