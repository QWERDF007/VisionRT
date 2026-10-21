#include "core/ProjectStorage.h"
#include "core/ToolRegistry.h"
#include <visionrt/contracts/Package.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv)
{
    try
    {
        if (argc != 3)
            throw std::runtime_error("Usage: example_metadata_contract <tools-package> <examples-directory>");
        visionrt::ToolRegistry registry;
        registry.registerPackage(visionrt::readToolsPackage(std::filesystem::u8path(argv[1])));
        visionrt::ProjectStorage storage(registry);
        std::size_t checked = 0;
        for (const auto &file : std::filesystem::directory_iterator(std::filesystem::u8path(argv[2])))
        {
            if (file.path().extension() != ".vrt")
                continue;
            const auto loaded = storage.load(file.path());
            for (const auto &flow : loaded.project.flows)
                for (const auto &node : flow.nodes)
                {
                    const auto *definition = registry.find(node.toolId);
                    if (!definition)
                        continue;
                    for (const auto &parameter : definition->initParams)
                    {
                        const auto value = node.initParams.find(parameter.id);
                        if (value == node.initParams.end() || parameter.options.empty())
                            continue;
                        if (std::none_of(parameter.options.begin(), parameter.options.end(),
                                         [&](const auto &option) { return visionrt::equalValues(option, value->second); }))
                            throw std::runtime_error(file.path().filename().string() + ": " + node.toolId + "."
                                                     + parameter.id + " is not a declared option");
                        ++checked;
                    }
                }
        }
        if (checked == 0)
            throw std::runtime_error("No declared example initialization options were checked");
        std::cout << "Example initialization options verified: " << checked << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
