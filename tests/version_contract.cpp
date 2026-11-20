#ifdef NDEBUG
#    undef NDEBUG
#endif

#include "common/Version.h"

#include <common/VersionDef.h>

#include <cassert>
#include <iostream>

using namespace visionrt;

int main(int argc, char **argv)
{
    assert(argc == 3);
    // 1. 验证版本宏
    static_assert(VISIONRT_MAKE_VERSION(1, 2, 3, 4) == 1020304U);
    static_assert(VISIONRT_MAKE_VERSION(1, 2, 3) == 1020300U);

    // 2. 验证接口返回
    const std::string version_str = GetVersionString();
    assert(version_str == argv[1]);

    const std::string branch_str = GetBranchString();
    assert(!branch_str.empty());

    const std::string commit_str = GetCommitHashString();
    assert(!commit_str.empty());

    const std::string build_time_str = GetBuildTimeString();
    assert(!build_time_str.empty());

    const std::string full_str = GetFullVersionString();
    assert(full_str.find(version_str) != std::string::npos);
    assert(full_str.find(branch_str) != std::string::npos);
    assert(full_str.find(commit_str) != std::string::npos);

    // 3. 验证结构体
    VersionInfo info = GetVersion();
    assert(info.major == VISIONRT_VERSION_MAJOR);
    assert(info.minor == VISIONRT_VERSION_MINOR);
    assert(info.patch == VISIONRT_VERSION_PATCH);
    assert(info.tweak == VISIONRT_VERSION_TWEAK);
    assert(VISIONRT_VERSION_API_AT_LEAST(info.major, info.minor));
    assert(!VISIONRT_VERSION_API_AT_LEAST(info.major + 1, 0));
    assert(info.version_string == version_str);
    assert(info.branch == branch_str);
    assert(info.commit == commit_str);
    assert(info.build_time == build_time_str);
    assert(info.full_string == full_str);

    // 4. 验证 Contracts 版本：版本与提交来自 Contracts 自己的仓库，构建时刻由构建产物元数据提供，
    //    这里不伪造时间戳。
    ComponentBuildInfo contracts = GetContractsVersion();
    assert(contracts.version == argv[2]);
    assert(!contracts.commit.empty());
    assert(contracts.commit.length() == 40);
    assert(contracts.built_at_epoch == 0);
    assert(info.contracts_version == contracts.version);
    assert(info.contracts_commit == contracts.commit);

    std::cout << "Version Contract Passed: " << full_str << " (Configured at " << build_time_str << ")\n";
    std::cout << "Contracts: " << contracts.version << " commit: " << contracts.commit << "\n";
    return 0;
}
