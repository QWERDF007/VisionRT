#pragma once

#include <visionrt/common/VersionDef.h>

#include <cstdint>
#include <string>

namespace visionrt {

// 一个组件的构建元数据。built_at_epoch 是构建完成时刻（秒，UTC 纪元），0 表示未知；它由构建
// 完成阶段写入的 build_info.json 提供，运行期再按本地时区显示。
struct ComponentBuildInfo
{
    std::string version;
    std::string commit;
    std::int64_t built_at_epoch = 0;
};

struct VersionInfo
{
    int         major{VISIONRT_VERSION_MAJOR};
    int         minor{VISIONRT_VERSION_MINOR};
    int         patch{VISIONRT_VERSION_PATCH};
    int         tweak{VISIONRT_VERSION_TWEAK};
    std::string suffix;
    std::string branch;
    std::string commit;
    std::string build_time;
    std::string version_string;
    std::string full_string;
    std::string contracts_version;
    std::string contracts_commit;
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

// 获取宿主实际引用的 Contracts 元数据
ComponentBuildInfo GetContractsVersion();

} // namespace visionrt
