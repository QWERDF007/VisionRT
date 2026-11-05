#pragma once

#include <visionrt/contracts/TypeSpec.h>

#include <QCoreApplication>
#include <QString>

#include <filesystem>

namespace visionrt {

// The one writable root of an installation: the software directory the executable was launched
// from. Tool declarations, user tools, preferences and logs all live under it; nothing this
// application writes belongs in a per-user profile or on the system drive.
inline std::filesystem::path softwareDataDir()
{
    const auto directory = QCoreApplication::applicationDirPath();
    // There is no fallback root: writing relative to the process working directory would put user
    // data outside the installation while claiming the installation owns it.
    if (directory.isEmpty())
        throw ContractError("无法确定软件目录，拒绝写入");
    return std::filesystem::weakly_canonical(std::filesystem::path(directory.toStdWString()));
}

inline std::filesystem::path resolveToolPath(const std::filesystem::path &root, const std::filesystem::path &relative)
{
    if (root.empty() || relative.empty() || relative.is_absolute() || relative.has_root_name()
        || relative.has_root_directory())
        throw ContractError("工具路径需要明确的根目录与相对路径");
    const auto base     = std::filesystem::weakly_canonical(std::filesystem::absolute(root));
    const auto resolved = std::filesystem::weakly_canonical(base / relative);
    auto       part     = resolved.begin();
    for (const auto &component : base)
    {
        if (part == resolved.end() || *part != component)
        {
            const auto text = relative.generic_u8string();
            throw ContractError("路径超出所选工具根目录：" + std::string(text.begin(), text.end()));
        }
        ++part;
    }
    return resolved;
}
}
