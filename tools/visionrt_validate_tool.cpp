#include "core/ToolPackage.h"

#include <iostream>
#include <string_view>
#include <vector>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#endif

namespace {
int runValidation(int argc, const wchar_t *const *argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: visionrt_validate_tool [--package <dir> | --index <file> | --emit-normalized-json "
                     "<file.json> | <file.json>]\n";
        return 1;
    }
    bool                  isIndex        = false;
    bool                  isPackage      = false;
    bool                  emitNormalized = false;
    std::filesystem::path targetPath;
    for (int i = 1; i < argc; ++i)
    {
        std::wstring_view arg = argv[i];
        if (arg == L"--index")
        {
            isIndex = true;
        }
        else if (arg == L"--package")
        {
            isPackage = true;
        }
        else if (arg == L"--emit-normalized-json")
        {
            emitNormalized = true;
        }
        else
        {
            targetPath = arg;
        }
    }
    if (targetPath.empty())
    {
        std::cerr << "Usage: visionrt_validate_tool [--package <dir> | --index <file> | --emit-normalized-json "
                     "<file.json> | <file.json>]\n";
        return 1;
    }
    if (!isPackage && targetPath.filename() == "module_index.json")
    {
        isIndex = true;
    }

    try
    {
        if (isPackage)
        {
            visionrt::validateToolsPackage(targetPath);
            std::cout << "Valid tools package: " << targetPath.string() << "\n";
        }
        else if (isIndex)
        {
            const auto index = visionrt::parseModuleIndexFile(targetPath);
            std::cout << "Valid module index: " << index.tools.size() << " tools across " << index.modules.size()
                      << " modules\n";
        }
        else if (emitNormalized)
        {
            const auto definition = visionrt::parseToolDefinitionFile(targetPath);
            std::cout << visionrt::emitNormalizedToolJson(definition);
        }
        else
        {
            const auto definition = visionrt::parseToolDefinitionFile(targetPath);
            std::cout << "Valid tool definition: " << definition.id << " (" << definition.displayName << ")\n";
        }
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Validation error: " << error.what() << "\n";
        return 2;
    }
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t **argv)
{
    return runValidation(argc, argv);
}
#else
int main(int argc, char **argv)
{
    std::vector<std::wstring>    wargs;
    std::vector<const wchar_t *> ptrs;
    for (int i = 0; i < argc; ++i)
    {
        auto u8 = std::filesystem::u8path(argv[i]).wstring();
        wargs.push_back(std::move(u8));
    }
    for (const auto &w : wargs) ptrs.push_back(w.c_str());
    return runValidation(argc, ptrs.data());
}
#endif
