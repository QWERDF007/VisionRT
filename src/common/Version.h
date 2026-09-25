#pragma once

#include <visionrt/common/VersionDef.h>
#include <string>

namespace visionrt {

struct VersionInfo {
    int major{VISIONRT_VERSION_MAJOR};
    int minor{VISIONRT_VERSION_MINOR};
    int patch{VISIONRT_VERSION_PATCH};
    int tweak{VISIONRT_VERSION_TWEAK};
    std::string suffix;
    std::string branch;
    std::string commit;
    std::string build_time;
    std::string version_string;
    std::string full_string;
};

// 获取结构化版本信息
VersionInfo GetVersion();

// 获取版本号字符串，如 "0.1.0"
std::string GetVersionString();

// 获取 Git 分支名称
std::string GetBranchString();

// 获取 Git 提交哈希值
std::string GetCommitHashString();

// 获取构建时间字符串
std::string GetBuildTimeString();

// 获取完整版本信息字符串，格式："<version> - [<branch>] - (<commit>)"
std::string GetFullVersionString();

} // namespace visionrt
