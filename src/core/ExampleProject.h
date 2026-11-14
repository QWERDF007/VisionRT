#pragma once

#include "core/Project.h"

#include <filesystem>

namespace visionrt {

// The projects the product installs under <application directory>/examples. A saved example keeps
// reading the resources installed beside the product, so copying one is an ordinary save: the
// parameter strings travel unchanged and nothing is collected or rewritten.
class ExampleProject
{
public:
    static std::filesystem::path directory(const std::filesystem::path &applicationDirectory);
    // True when the project file is inside the installed examples. An empty path, or one outside
    // the installation, is an ordinary user project.
    static bool owns(const std::filesystem::path &applicationDirectory, const std::filesystem::path &projectPath);
};
} // namespace visionrt
