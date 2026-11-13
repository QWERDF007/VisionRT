#include "core/ExampleProject.h"

#include "core/ToolDefinition.h"
#include "core/ToolRegistry.h"

#include <QFile>
#include <QString>

#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace visionrt {
namespace {
QString qtPath(const std::filesystem::path &path)
{
    const auto utf8 = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char *>(utf8.data()), static_cast<qsizetype>(utf8.size()));
}

std::string utf8Path(const std::filesystem::path &path)
{
    const auto utf8 = path.generic_u8string();
    return {reinterpret_cast<const char *>(utf8.data()), utf8.size()};
}

bool sameName(const std::filesystem::path &left, const std::filesystem::path &right)
{
    const auto a = left.native();
    const auto b = right.native();
#ifdef _WIN32
    // Windows compares path components case-insensitively; folding ASCII covers every spelling the
    // shell can hand back for an installed directory.
    if (a.size() != b.size())
        return false;
    for (std::size_t index = 0; index < a.size(); ++index)
    {
        const auto fold = [](auto value)
        {
            return value >= u'A' && value <= u'Z' ? static_cast<decltype(value)>(value - u'A' + u'a') : value;
        };
        if (fold(a[index]) != fold(b[index]))
            return false;
    }
    return true;
#else
    return a == b;
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

bool sameContent(const std::filesystem::path &left, const std::filesystem::path &right)
{
    QFile first(qtPath(left));
    QFile second(qtPath(right));
    if (!first.open(QIODevice::ReadOnly) || !second.open(QIODevice::ReadOnly))
        return false;
    if (first.size() != second.size())
        return false;
    return first.readAll() == second.readAll();
}

void copyResource(const std::filesystem::path &source, const std::filesystem::path &destination)
{
    if (std::filesystem::is_directory(source))
    {
        for (const auto &entry : std::filesystem::recursive_directory_iterator(source))
        {
            if (!entry.is_regular_file())
                continue;
            copyResource(entry.path(), destination / entry.path().lexically_relative(source));
        }
        return;
    }
    if (std::filesystem::exists(destination))
    {
        if (std::filesystem::is_directory(destination))
            throw std::runtime_error("目标位置已有同名目录，未覆盖：" + utf8Path(destination));
        if (sameContent(source, destination))
            return;
        throw std::runtime_error("目标位置已有同名资源，未覆盖：" + utf8Path(destination));
    }
    std::filesystem::create_directories(destination.parent_path());
    std::filesystem::copy_file(source, destination);
}

// Every resource the project names through a path port. A relative literal is the layout the export
// reproduces; an absolute one is not followed, because an example never stores a developer machine
// path and a user project is not exported here.
void visitResources(const ProjectDefinition &project, const ToolRegistry &registry,
                    const std::function<void(const std::filesystem::path &)> &visit)
{
    for (const auto &flow : project.flows)
    {
        for (const auto &node : flow.nodes)
        {
            const auto *definition = registry.find(node.toolId);
            if (!definition || !node.unresolved.isEmpty())
                continue;
            const auto follow = [&visit](const PortValue &value)
            {
                const auto *text = std::get_if<std::string>(&value);
                if (!text || text->empty())
                    return;
                const auto path = std::filesystem::u8path(*text);
                if (!path.is_absolute())
                    visit(path);
            };
            for (const auto &port : definition->initParams)
            {
                if (!isPathPort(port))
                    continue;
                if (const auto value = node.initParams.find(port.id); value != node.initParams.end())
                    follow(value->second);
            }
            for (const auto &port : definition->inputs)
            {
                if (!isPathPort(port))
                    continue;
                if (const auto value = node.inputs.find(port.id); value != node.inputs.end())
                    follow(value->second.literal);
            }
        }
    }
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

ProjectDefinition ExampleProject::adopt(const ProjectDefinition &source, const std::filesystem::path &destination,
                                        const ToolRegistry &registry)
{
    if (source.path.empty())
        throw std::runtime_error("示例工程缺少来源位置，无法复制素材");
    const auto sourceDirectory = std::filesystem::absolute(source.path).lexically_normal().parent_path();
    const auto target          = std::filesystem::absolute(destination).lexically_normal();
    const auto targetDirectory = target.parent_path();
    ProjectDefinition candidate = source;
    candidate.path              = target;
    if (targetDirectory == sourceDirectory)
        return candidate;
    visitResources(source, registry,
                   [&](const std::filesystem::path &relative)
                   {
                       const auto origin = (sourceDirectory / relative).lexically_normal();
                       if (!std::filesystem::exists(origin))
                           return;
                       copyResource(origin, (targetDirectory / relative).lexically_normal());
                   });
    return candidate;
}
} // namespace visionrt
