#pragma once

#include "core/Export.h"

#include "visionrt/contracts/Declaration.h"
#include "visionrt/contracts/ProtocolEnums.h"
#include "visionrt/contracts/Tool.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace visionrt {

struct ToolRoots
{
    std::filesystem::path declarations;
    std::filesystem::path python;
    bool                  operator==(const ToolRoots &) const = default;
};

struct ToolSource
{
    ToolOrigin origin = ToolOrigin::Custom;
    ToolRoots  roots;
    bool       operator==(const ToolSource &) const = default;
};

struct ToolDefinition : ToolDeclaration
{
    std::filesystem::path              definitionPath;
    // Derived from the declaration's directory inside its root, never read from the file.
    std::vector<std::string>           categories;
    ToolSource                         source;

    VISIONRT_CORE_API std::filesystem::path pythonPackage() const;
    VISIONRT_CORE_API std::filesystem::path pythonScript() const;
};

// The one Chinese breadcrumb of a tool ("预处理/图像处理"), used by the tool tree and node badges.
VISIONRT_CORE_API std::string categoryPath(const ToolDefinition& def);

// A stable ASCII identifier: letter or underscore first, then letters, digits, '_', '.' or '-'.
// Tool ids name declaration files, so anything that could leave a directory is rejected here.
VISIONRT_CORE_API void validateIdentifier(const std::string& value, bool isPort);

// The inverse of normalisation for host ports: leaves exactly what the declaration authored. The
// editor draft is a declaration, so it round-trips through this on load and never holds a second
// copy of a fixed row the port model already projects.
VISIONRT_CORE_API void stripHostPorts(ToolDefinition &def);

// Fixed parameter pages shared by the validator and the workbench.
VISIONRT_CORE_API std::span<const std::string_view> toolTabs() noexcept;

// The one place that turns a declaration into an effective contract: host-owned ports are
// materialised at their reserved numbers, every value-carrying port receives its explicit default
// or the type's empty value, high-water counters are raised above the live ids, and the result is
// validated. Idempotent; conflicting author declarations fail loudly.
VISIONRT_CORE_API void normalizeToolDefinition(ToolDefinition &def);

// Validating entry used after normalisation; kept separate for read-only checks.
VISIONRT_CORE_API void validateToolDefinition(const ToolDefinition& def);

// The lowest business number this direction does not use yet. Adding a port picks from here; the
// caller assigns it and nothing about a removed port is remembered.
VISIONRT_CORE_API PortId nextFreePortId(const ToolDefinition &def, PortStage stage);

// Editor kinds that can actually present this type. The declaration validator accepts any known
// editor whose shape fits; this is the subset the editor offers and reconciliation keeps, so a
// value field is never declared with a control that cannot render it.
VISIONRT_CORE_API const char *editorLabel(EditorKind editor);
VISIONRT_CORE_API std::vector<EditorKind> editorChoices(const TypeSpec &type);

// Retypes one draft port: every value, constraint, relation and reference the new type can still
// hold is kept, and the rest is cleared. The returned sentences name each cleared field, so an
// editor can show the exact loss before committing. A derived-type port, or one another declaration
// uses as a type source, is refused instead of being half-retyped.
VISIONRT_CORE_API std::vector<std::string> retypePort(ToolDefinition &def, PortStage stage, PortId id, TypeSpec newType);

// Removes one business port together with everything that named it: the relations other ports point
// at it with, and the interaction entries that offered it. The returned sentences name each dropped
// reference. A host-owned port, or one a type_from output derives from, is refused instead of being
// removed from under a declaration that still needs it.
VISIONRT_CORE_API std::vector<std::string> erasePort(ToolDefinition &def, PortStage stage, PortId id);

// An independent copy of a port's default value, safe to hand to a node that may write it.
VISIONRT_CORE_API PortValue portDefault(const PortDefinition& port);

} // namespace visionrt
