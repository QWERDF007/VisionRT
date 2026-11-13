#pragma once

#include "core/Project.h"

#include <filesystem>

namespace visionrt {
class ToolRegistry;

// The projects the product installs under <application directory>/examples. Two questions live in
// one place: whether a project file still belongs to that installation, and how a copy of it becomes
// the user's own project with the files it reads. Storage stays the single JSON implementation; this
// neither parses nor writes a project file.
class ExampleProject
{
public:
    static std::filesystem::path directory(const std::filesystem::path &applicationDirectory);
    // True when the project file is inside the installed examples. An empty path, or one outside
    // the installation, is an ordinary user project.
    static bool owns(const std::filesystem::path &applicationDirectory, const std::filesystem::path &projectPath);
    // Copies every resource the project references into the destination's directory, keeping the
    // relative layout the project already stores, and returns the candidate that describes that
    // layout. A destination file that already holds different content stops the copy with a Chinese
    // message naming both paths; identical content is left alone so a repeated export is a no-op.
    static ProjectDefinition adopt(const ProjectDefinition &source, const std::filesystem::path &destination,
                                   const ToolRegistry &registry);
};
} // namespace visionrt
