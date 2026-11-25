#include "core/ProjectArchive.h"
#include "core/ProjectStorage.h"
#include "core/ToolRegistry.h"
#include "visionrt/contracts/ZipArchive.h"

#include <QTemporaryDir>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template<class Action>
void expectContractError(Action &&action, const char *message)
{
    bool threw = false;
    try
    {
        action();
    }
    catch (const ContractError &)
    {
        threw = true;
    }
    catch (const std::runtime_error &)
    {
        threw = true;
    }
    require(threw, message);
}
} // namespace

int main()
try
{
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "cannot create temp directory");
    const auto root = std::filesystem::path(tempDir.path().toStdWString());

    // 1. Roundtrip Chinese filename & UTF-8 program.json payload.
    const auto chinesePath = root / std::filesystem::path(u8"中文工程容器_测试.vrtapp");
    const QByteArray payload = R"json({"format_version":9,"project":{"name":"中文工程"}})json";
    ProjectArchive::writeProgramJson(chinesePath, payload);
    require(std::filesystem::is_regular_file(chinesePath), "archive file not created");
    require(ProjectArchive::readProgramJson(chinesePath) == payload, "program.json roundtrip mismatch");

    // 2. Verify ZIP contains only program.json and has UTF-8 Bit 11 set in local/central headers.
    const auto entries = ZipArchive::readAll(chinesePath);
    require(entries.size() == 1 && entries.contains("program.json"), "archive must contain only program.json");

    // 3. Reject archive missing program.json.
    const auto missingProgram = root / "missing_program.vrtapp";
    ZipArchive::write(missingProgram, {{"other.txt", {'h', 'i'}}});
    expectContractError([&] { ProjectArchive::readProgramJson(missingProgram); },
                        "archive missing program.json should be rejected");

    // 4. Reject corrupt / non-ZIP file.
    const auto corruptPath = root / "corrupt.vrtapp";
    {
        std::ofstream out(corruptPath, std::ios::binary);
        out << "not a zip archive at all";
    }
    expectContractError([&] { ProjectArchive::readProgramJson(corruptPath); },
                        "corrupt archive should be rejected");

    // 5. Reject legacy .json / .yaml / .yml extensions in ProjectStorage and projectDestination.
    ToolRegistry   registry;
    ProjectStorage storage(registry);
    expectContractError([&] { (void)projectDestination(root / "legacy.json"); },
                        "projectDestination must reject .json");
    expectContractError([&] { (void)projectDestination(root / "legacy.yaml"); },
                        "projectDestination must reject .yaml");
    require(projectDestination(root / "implicit_name").extension() == ".vrtapp",
            "projectDestination must default to .vrtapp");
    const auto legacyFile = root / "old.json";
    {
        std::ofstream out(legacyFile, std::ios::binary);
        out << R"({"format_version":8})";
    }
    expectContractError([&] { (void)storage.load(legacyFile); },
                        "ProjectStorage::load must reject .json");

    // 6. Atomic write preserves existing valid archive on write failure (non-existent parent dir).
    const auto validBefore = ProjectArchive::readProgramJson(chinesePath);
    ProjectDefinition project;
    project.name = "测试工程";
    project.path = chinesePath;
    expectContractError([&] { storage.save(project, root / "no_such_dir" / "nested" / "fail.vrtapp"); },
                        "saving to missing directory should fail");
    require(ProjectArchive::readProgramJson(chinesePath) == validBefore,
            "failed save corrupted existing archive");

    std::cout << "ProjectArchive .vrtapp container contract verified\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
