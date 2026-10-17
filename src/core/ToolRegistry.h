#pragma once

#include "core/Tool.h"

#include <stdexcept>
#include <string_view>

namespace YAML { class Node; }

namespace visionrt {

ToolDefinition parseDefinition(const std::filesystem::path& path, bool builtin);
ToolDefinition parseDefinitionNode(const YAML::Node& node, const std::filesystem::path& path, bool builtin);

enum class ToolAvailability { Available, DefinitionMissing, ImplementationUnavailable };
struct Availability {
    ToolAvailability state;
    std::string message;
};
struct RegistryDiagnostic {
    std::filesystem::path path;
    std::string message;
    std::filesystem::path relatedPath;
};
class RegistryError : public std::runtime_error {
public:
    RegistryError(ToolAvailability reason, std::string message)
        : std::runtime_error(std::move(message)), reason(reason) {}
    ToolAvailability reason;
};

// Mutate only between runs; definitions and their factories form a runtime snapshot.
class ToolRegistry {
public:
    void scan(const std::filesystem::path& toolsRoot);
    void scanPython(const std::filesystem::path& toolsRoot);
    void addDefinition(ToolDefinition definition);
    void replaceDefinition(ToolDefinition definition);
    void validateContract(const ToolDefinition& definition) const;
    std::vector<const ToolDefinition*> cppContracts() const;
    void registerCpp(std::string registryName, ToolDefinition contract, ToolFactory factory);
    void setPythonImplementation(std::string toolId, ToolFactory factory, ToolStatus status = {});
    const ToolDefinition* find(std::string_view id) const;
    Availability availability(std::string_view id) const;
    std::unique_ptr<IToolInstance> create(std::string_view id, const PlatformToolServices& services) const;
    const std::vector<ToolDefinition>& definitions() const noexcept { return definitions_; }
    const std::vector<RegistryDiagnostic>& diagnostics() const noexcept { return diagnostics_; }

private:
    void scanDefinitions(const std::filesystem::path& toolsRoot, bool pythonOnly);
    struct CppImplementation {
        ToolDefinition contract;
        ToolFactory factory;
    };
    struct PythonImplementation {
        ToolFactory factory;
        ToolStatus status;
    };
    std::vector<ToolDefinition> definitions_;
    std::map<std::string, std::size_t, std::less<>> index_;
    std::vector<RegistryDiagnostic> diagnostics_;
    std::map<std::string, CppImplementation, std::less<>> cpp_;
    std::map<std::string, PythonImplementation, std::less<>> python_;
};

} // namespace visionrt
