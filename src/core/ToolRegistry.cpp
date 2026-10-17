#include "core/ToolRegistry.h"
#include "core/ValueYaml.h"
#include "plugins/PluginLoader.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>

#include "visionrt/sdk/SchemaYaml.h"

namespace visionrt {
namespace {
std::string pathText(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
bool cppKind(ToolKind kind) { return kind == ToolKind::CppClass || kind == ToolKind::CppFunction; }

const PortDefinition* portById(const std::vector<PortDefinition>& ports, std::string_view id) {
    const auto found = std::find_if(ports.begin(), ports.end(), [&](const auto& port) { return port.id == id; });
    return found == ports.end() ? nullptr : &*found;
}
void align(const ToolDefinition& definition, const ToolDefinition& contract) {
    if (definition.kind != contract.kind || definition.registryName != contract.registryName)
        throw ContractError("C++ tool kind/entry does not match registered contract: " + definition.id);
    const auto portsMatch = [](const auto& actual, const auto& expected, const char* direction) {
        if (actual.size() != expected.size()) throw ContractError(std::string("C++ contract port count differs: ") + direction);
        for (const auto& port : actual) {
            const auto* other = portById(expected, port.id);
            if (!other || !compatible(port.type, other->type))
                throw ContractError(std::string("C++ contract differs at ") + direction + "." + port.id);
        }
    };
    portsMatch(definition.initParams, contract.initParams, "init_params");
    portsMatch(definition.inputs, contract.inputs, "inputs");
    portsMatch(definition.outputs, contract.outputs, "outputs");
}
using OrderedPath = std::pair<std::u8string, std::filesystem::path>;
void collect(const std::filesystem::path& directory, int depth, std::vector<OrderedPath>& paths,
             std::vector<RegistryDiagnostic>& diagnostics) {
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, error), end;
    if (error) {
        if (error != std::errc::no_such_file_or_directory) diagnostics.push_back({directory, error.message(), {}});
        return;
    }
    for (; iterator != end; iterator.increment(error)) {
        if (error) { diagnostics.push_back({directory, error.message(), {}}); break; }
        const auto entry = *iterator;
        if (entry.is_directory(error)) {
            if (depth < 2 && !entry.is_symlink(error)) collect(entry.path(), depth + 1, paths, diagnostics);
        } else if (!error && entry.is_regular_file(error)) {
            auto extension = entry.path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
                return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
            });
            if (extension == ".yaml" || extension == ".yml") {
                if (depth == 2) {
                    auto path = entry.path().lexically_normal();
                    paths.emplace_back(path.generic_u8string(), std::move(path));
                } else diagnostics.push_back({entry.path(), "Tool YAML requires category/subcategory directories", {}});
            }
        }
        if (error) { diagnostics.push_back({entry.path(), error.message(), {}}); error.clear(); }
    }
    if (error) diagnostics.push_back({directory, error.message(), {}});
}
} // namespace

ToolDefinition parseDefinitionNode(const YAML::Node& node, const std::filesystem::path& path, bool builtin) {
    YAML::Emitter emitter;
    emitter << node;
    return parseToolDefinitionYaml(emitter.c_str(), path, builtin);
}

ToolDefinition parseDefinition(const std::filesystem::path& path, bool builtin) {
    return parseToolDefinitionFile(path, builtin);
}

void ToolRegistry::scan(const std::filesystem::path& toolsRoot) {
    scanRoots({toolsRoot});
}
void ToolRegistry::scanRoots(const std::vector<std::filesystem::path>& roots) {
    std::vector<std::filesystem::path> normalized;
    for (const auto& r : roots) {
        if (r.empty()) continue;
        const auto normal = r.lexically_normal();
        if (std::find(normalized.begin(), normalized.end(), normal) == normalized.end()) {
            normalized.push_back(normal);
        }
    }
    managedRoots_ = std::move(normalized);
    scanDefinitions(managedRoots_, false);
}
void ToolRegistry::scanPython(const std::filesystem::path& toolsRoot) {
    const auto normal = toolsRoot.lexically_normal();
    if (std::find(managedRoots_.begin(), managedRoots_.end(), normal) == managedRoots_.end()) {
        managedRoots_.push_back(normal);
    }
    scanDefinitions(managedRoots_, true);
}
void ToolRegistry::scanPythonRoots() {
    scanDefinitions(managedRoots_, true);
}
void ToolRegistry::scanDefinitions(const std::vector<std::filesystem::path>& roots, bool pythonOnly) {
    std::set<std::filesystem::path> cppPaths;
    std::map<std::filesystem::path, std::string> oldPython;
    if (pythonOnly) {
        std::erase_if(definitions_, [&](const auto& definition) {
            if (cppKind(definition.kind)) {
                cppPaths.insert(definition.definitionPath);
                return false;
            }
            oldPython.emplace(definition.definitionPath, definition.id);
            return true;
        });
    } else {
        definitions_.clear();
    }
    index_.clear();
    for (std::size_t i = 0; i < definitions_.size(); ++i) index_.emplace(definitions_[i].id, i);
    diagnostics_.clear();
    python_.clear();

    struct Candidate {
        bool builtin;
        std::size_t rootIndex;
        std::filesystem::path path;
        std::u8string sortKey;
    };
    std::vector<Candidate> candidates;

    for (std::size_t r = 0; r < roots.size(); ++r) {
        const auto& toolsRoot = roots[r];
        std::error_code ec;
        if (!std::filesystem::exists(toolsRoot, ec)) continue;
        const bool hasBuiltin = std::filesystem::is_directory(toolsRoot / "builtin", ec);
        const bool hasCustom = std::filesystem::is_directory(toolsRoot / "custom", ec);
        if (hasBuiltin || hasCustom) {
            for (const bool builtin : {true, false}) {
                std::vector<OrderedPath> paths;
                const auto root = (toolsRoot / (builtin ? "builtin" : "custom")).lexically_normal();
                collect(root, 0, paths, diagnostics_);
                for (auto& item : paths) {
                    candidates.push_back({builtin, r, std::move(item.second), std::move(item.first)});
                }
            }
        } else if (std::filesystem::is_directory(toolsRoot, ec)) {
            std::vector<OrderedPath> paths;
            collect(toolsRoot.lexically_normal(), 0, paths, diagnostics_);
            for (auto& item : paths) {
                candidates.push_back({false, r, std::move(item.second), std::move(item.first)});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.builtin != b.builtin) return a.builtin > b.builtin;
        if (a.rootIndex != b.rootIndex) return a.rootIndex < b.rootIndex;
        return a.sortKey < b.sortKey;
    });

    for (const auto& candidate : candidates) {
        const auto& path = candidate.path;
        if (pythonOnly && cppPaths.contains(path)) continue;
        oldPython.erase(path);
        try {
            auto definition = parseDefinition(path, candidate.builtin);
            if (pythonOnly && cppKind(definition.kind)) {
                diagnostics_.push_back({path, "New C++ definitions require application restart", {}});
                continue;
            }
            if (const auto* previous = find(definition.id)) {
                diagnostics_.push_back({path, "Duplicate tool_id " + definition.id + "; first definition: " + pathText(previous->definitionPath), previous->definitionPath});
                continue;
            }
            addDefinition(std::move(definition));
        } catch (const std::exception& error) {
            diagnostics_.push_back({path, error.what(), {}});
        }
    }
    for (const auto& [path, id] : oldPython)
        diagnostics_.push_back({path, "Python definition missing after refresh: " + id, {}});
}
void ToolRegistry::addDefinition(ToolDefinition definition) {
    validateToolDefinition(definition);
    if (const auto* previous = find(definition.id))
        throw ContractError("Duplicate tool_id " + definition.id + ": " + pathText(previous->definitionPath) + " and " + pathText(definition.definitionPath));
    validateContract(definition);
    const auto index = definitions_.size();
    const auto id = definition.id;
    definitions_.push_back(std::move(definition));
    index_.emplace(id, index);
}
void ToolRegistry::replaceDefinition(ToolDefinition definition) {
    const auto existing = index_.find(definition.id);
    if (existing == index_.end()) {
        addDefinition(std::move(definition));
        return;
    }
    validateToolDefinition(definition);
    validateContract(definition);
    definitions_[existing->second] = std::move(definition);
    python_.erase(existing->first);
}
void ToolRegistry::registerCpp(std::string registryName, ToolDefinition contract, ToolFactory factory) {
    if (registryName.empty() || !factory || !cppKind(contract.kind)) throw ContractError("Invalid C++ factory registration");
    if (contract.registryName.empty()) contract.registryName = registryName;
    if (contract.registryName != registryName) throw ContractError("C++ registry_name disagrees with contract");
    if (cpp_.contains(registryName)) throw ContractError("Duplicate C++ registry_name: " + registryName);
    validateToolDefinition(contract);
    for (const auto& definition : definitions_)
        if (cppKind(definition.kind) && definition.registryName == registryName) align(definition, contract);
    cpp_.emplace(std::move(registryName), CppImplementation{std::move(contract), std::move(factory)});
}
void ToolRegistry::validateContract(const ToolDefinition& definition) const {
    if (cppKind(definition.kind)) {
        const auto implementation = cpp_.find(definition.registryName);
        if (implementation != cpp_.end()) align(definition, implementation->second.contract);
    }
}
std::vector<const ToolDefinition*> ToolRegistry::cppContracts() const {
    std::vector<const ToolDefinition*> contracts;
    contracts.reserve(cpp_.size());
    for (const auto& [name, implementation] : cpp_) contracts.push_back(&implementation.contract);
    return contracts;
}
void ToolRegistry::setPythonImplementation(std::string toolId, ToolFactory factory, ToolStatus status) {
    const auto* definition = find(toolId);
    if (!definition || cppKind(definition->kind)) throw ContractError("Python implementation requires a loaded Python definition: " + toolId);
    python_.insert_or_assign(std::move(toolId), PythonImplementation{std::move(factory), std::move(status)});
}
const ToolDefinition* ToolRegistry::find(std::string_view id) const {
    const auto found = index_.find(id);
    return found == index_.end() ? nullptr : &definitions_[found->second];
}
Availability ToolRegistry::availability(std::string_view id) const {
    const auto* definition = find(id);
    if (!definition) return {ToolAvailability::DefinitionMissing, "Tool definition missing: " + std::string(id)};
    if (cppKind(definition->kind)) {
        if (cpp_.contains(definition->registryName)) return {ToolAvailability::Available, {}};
        if (pluginLoader_ && pluginLoader_->hasModule(definition->registryName)) return {ToolAvailability::Available, {}};
        return {ToolAvailability::ImplementationUnavailable, "C++ implementation unavailable: " + definition->registryName + " (" + pathText(definition->definitionPath) + ")"};
    }
    const auto implementation = python_.find(id);
    if (implementation != python_.end() && implementation->second.factory && implementation->second.status)
        return {ToolAvailability::Available, {}};
    return {ToolAvailability::ImplementationUnavailable, "Python implementation unavailable: " + std::string(id) +
        (implementation != python_.end() && !implementation->second.status.message.empty() ? ": " + implementation->second.status.message : " (not loaded)")};
}
std::unique_ptr<IToolInstance> ToolRegistry::create(std::string_view id, const PlatformToolServices& services) const {
    const auto state = availability(id);
    if (state.state != ToolAvailability::Available) throw RegistryError(state.state, state.message);
    const auto& definition = *find(id);
    if (cppKind(definition.kind)) {
        const auto it = cpp_.find(definition.registryName);
        if (it != cpp_.end() && it->second.factory) {
            auto instance = it->second.factory(services);
            if (!instance) throw RegistryError(ToolAvailability::ImplementationUnavailable, "Tool factory returned no instance: " + std::string(id));
            return instance;
        }
        if (pluginLoader_ && pluginLoader_->hasModule(definition.registryName)) {
            auto instance = pluginLoader_->createAdapter(definition.registryName, services);
            if (!instance) throw RegistryError(ToolAvailability::ImplementationUnavailable, "Plugin loader returned no adapter: " + std::string(id));
            return instance;
        }
    }
    const auto& factory = python_.find(id)->second.factory;
    auto instance = factory(services);
    if (!instance) throw RegistryError(ToolAvailability::ImplementationUnavailable, "Tool factory returned no instance: " + std::string(id));
    return instance;
}

} // namespace visionrt
