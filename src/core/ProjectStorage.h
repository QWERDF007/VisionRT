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
    // A guard saved while the tool still accepted execution conditions. The current node model
    // no longer applies it: the value is kept, and the user reconfigures it as a state input.
    LegacyExecutionGuard
};

struct ProjectStorageDiagnostic
{
    ProjectStorageCode     code;
    std::string            flowId;
    std::optional<NodeId>  node;
    std::string            field;
    std::string            message;
    // The port the message is about, when it names one. Reconciliation reads this instead of
    // recovering a number from the display text.
    std::optional<PortRef> port;
};

// A connection the current declarations can no longer carry. Both ends are named, because the
// window has to say what was cut instead of only that a tool changed.
struct RemovedConnection
{
    std::string source;
    std::string target;
    std::string reason;
};

// One tool whose declaration changed since the project was saved. The nodes are listed by title;
// the counts say what survived, and each removal says which two ports it joined and why it went.
struct ToolUpdate
{
    std::string                    toolId;
    std::string                    nameZh;
    std::string                    savedVersion;
    std::string                    currentVersion;
    std::vector<std::string>       nodes;
    std::size_t                    kept = 0;
    std::vector<RemovedConnection> removed;
};

struct ProjectLoadResult
{
    ProjectDefinition                     project;
    std::vector<ProjectStorageDiagnostic> diagnostics;
    // Every tool whose declaration moved under this project, empty when nothing changed.
    std::vector<ToolUpdate> toolUpdates;
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
    // Values are stored exactly as the user entered them; moving a project never rewrites a path.
    void save(ProjectDefinition &project, const std::filesystem::path &destination = {}) const;

private:
    const ToolRegistry &registry_;
};
} // namespace visionrt
