#pragma once

#include "core/Project.h"

namespace visionrt {
class ToolRegistry;

enum class ProjectStorageCode
{
    DefinitionMissing,
    PendingReference,
    InvalidReference,
    SchemaChanged,
    CounterRepaired,
    SequenceExhausted,
    Cycle,
    DuplicateFlowName,
    LayoutConflict,
    RangeWarning,
    UnknownPaths,
    // A format-4 execution condition on a fixed tool is preserved but no longer
    // applied; the project needs an explicit state-input migration.
    LegacyExecutionGuard
};

struct ProjectStorageDiagnostic
{
    ProjectStorageCode    code;
    std::string           flowId;
    std::optional<NodeId> node;
    std::string           field;
    std::string           message;
};

struct ProjectLoadResult
{
    ProjectDefinition                     project;
    std::vector<ProjectStorageDiagnostic> diagnostics;
};

// All parsing/reconciliation happens in a candidate. Exceptions never replace the
// caller's project; save installs its candidate only after QSaveFile::commit.
class ProjectStorage
{
public:
    explicit ProjectStorage(const ToolRegistry &registry)
        : registry_(registry)
    {
    }

    ProjectLoadResult                     load(const std::filesystem::path &path) const;
    ProjectLoadResult                     reconcile(const ProjectDefinition &project) const;
    // Save As rewrites only metadata-declared file/directory literals, including
    // hidden upstream drafts. Resource files are never inspected.
    std::vector<ProjectStorageDiagnostic> save(ProjectDefinition           &project,
                                               const std::filesystem::path &destination = {}) const;

private:
    const ToolRegistry &registry_;
};
} // namespace visionrt
