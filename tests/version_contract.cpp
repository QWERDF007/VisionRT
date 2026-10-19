#ifdef NDEBUG
#    undef NDEBUG
#endif

#include "common/Version.h"

#include <visionrt/common/VersionDef.h>

#include <cassert>
#include <iostream>

using namespace visionrt;

int main()
{
    // 1. 验证版本宏
    static_assert(VISIONRT_VERSION_MAJOR == 0, "Major version should be 0");
    static_assert(VISIONRT_VERSION_MINOR == 1, "Minor version should be 1");
    static_assert(VISIONRT_VERSION_PATCH == 0, "Patch version should be 0");
    static_assert(VISIONRT_VERSION_TWEAK == 0, "Tweak version should be 0");

    assert(VISIONRT_VERSION >= VISIONRT_MAKE_VERSION(0, 1, 0));
    assert(VISIONRT_VERSION_API_AT_LEAST(0, 1));

    // 2. 验证接口返回
    const std::string version_str = GetVersionString();
    assert(version_str == "0.1.0");

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
    assert(info.major == 0);
    assert(info.minor == 1);
    assert(info.patch == 0);
    assert(info.tweak == 0);
    assert(info.version_string == version_str);
    assert(info.branch == branch_str);
    assert(info.commit == commit_str);
    assert(info.build_time == build_time_str);
    assert(info.full_string == full_str);

    std::cout << "Version Contract Passed: " << full_str << " (Built at " << build_time_str << ")\n";
    return 0;
}
