#include "core/ProjectStorage.h"
#include "core/ToolRegistry.h"
#include <core/ToolPackage.h>

#include <algorithm>
#include <cstdlib>
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
            if (!file.path().filename().generic_string().ends_with(".vrt.json"))
                continue;
            const auto loaded = storage.load(file.path());
            for (const auto &diagnostic : loaded.diagnostics)
                if (diagnostic.code == visionrt::ProjectStorageCode::SchemaChanged)
                    throw std::runtime_error(file.path().filename().string() + ": " + diagnostic.field
                                             + " requires schema reconciliation");
            for (const auto &flow : loaded.project.flows)
                for (const auto &node : flow.nodes)
                {
                    const auto &anchor = flow.nodes.front().position;
                    if (std::abs(static_cast<long long>(node.position.x) - anchor.x) > 4
                        || std::abs(static_cast<long long>(node.position.y) - anchor.y) > 4)
                        throw std::runtime_error(
                            file.path().filename().string()
                            + ": compact examples must use canvas grid cells, not pixel coordinates");
                    const auto *definition = registry.find(node.toolId);
                    if (!definition)
                        continue;
                    const auto check = [&](const visionrt::PortDefinition &parameter, const visionrt::PortValue &value)
                    {
                        if (parameter.options.empty() || visionrt::isMissing(value))
                            return;
                        if (std::none_of(parameter.options.begin(), parameter.options.end(),
                                         [&](const auto &option) { return visionrt::equalValues(option, value); }))
                            throw std::runtime_error(file.path().filename().string() + ": " + node.toolId + "."
                                                     + visionrt::portLabel(parameter.id) + " is not a declared option");
                        ++checked;
                    };
                    // The declaration number is the key of both maps; an entry the node never wrote
                    // is absent, and an absent value is not a rejected option.
                    for (const auto &parameter : definition->initParams)
                        if (const auto found = node.initParams.find(parameter.id); found != node.initParams.end())
                            check(parameter, found->second);
                    for (const auto &input : definition->inputs)
                        if (const auto found = node.inputs.find(input.id); found != node.inputs.end())
                            check(input, found->second.literal);
                }
        }
        if (checked == 0)
            throw std::runtime_error("No declared example options were checked");
        std::cout << "Example initialization/input options and saved signatures verified: " << checked << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
