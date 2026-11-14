#include "core/ExampleProject.h"

#include <QString>

#include <filesystem>

namespace visionrt {
namespace {
bool sameName(const std::filesystem::path &left, const std::filesystem::path &right)
{
#ifdef _WIN32
    // Windows compares path components case-insensitively; Qt owns that folding so the check does
    // not grow a second opinion about which spellings are the same directory.
    return QString::fromStdWString(left.native())
               .compare(QString::fromStdWString(right.native()), Qt::CaseInsensitive)
        == 0;
#else
    return left.native() == right.native();
#endif
}

// True when `path` is `root` itself or one of its descendants.
bool below(const std::filesystem::path &root, const std::filesystem::path &path)
{
    auto       part = path.begin();
    const auto end  = path.end();
    for (const auto &component : root)
    {
        if (part == end || !sameName(component, *part))
            return false;
        ++part;
    }
    return true;
}
} // namespace

std::filesystem::path ExampleProject::directory(const std::filesystem::path &applicationDirectory)
{
    return applicationDirectory / "examples";
}

bool ExampleProject::owns(const std::filesystem::path &applicationDirectory, const std::filesystem::path &projectPath)
{
    if (applicationDirectory.empty() || projectPath.empty())
        return false;
    const auto root = std::filesystem::absolute(applicationDirectory).lexically_normal();
    return below(directory(root), std::filesystem::absolute(projectPath).lexically_normal());
}
} // namespace visionrt
