#pragma once

#include <visionrt/contracts/TypeSpec.h>
#include <filesystem>

namespace visionrt {
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
