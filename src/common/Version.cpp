#include "common/Version.h"

#include <visionrt/common/VersionDef.h>
#include <visionrt/common/detail/BuildTime.h>
#include <visionrt/common/detail/VersionUtils.h>

namespace visionrt {

std::string GetVersionString()
{
    return VISIONRT_VERSION_STRING;
}

std::string GetBranchString()
{
    return VISIONRT_BRANCH;
}

std::string GetCommitHashString()
{
    return VISIONRT_COMMIT;
}

std::string GetBuildTimeString()
{
    return VISIONRT_BUILD_TIME;
}

std::string GetFullVersionString()
{
    std::string s = VISIONRT_VERSION_STRING;
    s += " - [";
    s += VISIONRT_BRANCH;
    s += "] - (";
    s += VISIONRT_COMMIT;
    s += ")";
    return s;
}

ComponentBuildInfo GetContractsVersion()
{
    return ComponentBuildInfo{
        VISIONRT_CONTRACTS_VERSION,
        VISIONRT_CONTRACTS_COMMIT,
        VISIONRT_CONTRACTS_BUILD_TIME
    };
}

VersionInfo GetVersion()
{
    VersionInfo info;
    info.major                = VISIONRT_VERSION_MAJOR;
    info.minor                = VISIONRT_VERSION_MINOR;
    info.patch                = VISIONRT_VERSION_PATCH;
    info.tweak                = VISIONRT_VERSION_TWEAK;
    info.suffix               = VISIONRT_VERSION_SUFFIX;
    info.branch               = VISIONRT_BRANCH;
    info.commit               = VISIONRT_COMMIT;
    info.build_time           = VISIONRT_BUILD_TIME;
    info.version_string       = VISIONRT_VERSION_STRING;
    info.full_string          = GetFullVersionString();
    info.contracts_version    = VISIONRT_CONTRACTS_VERSION;
    info.contracts_commit     = VISIONRT_CONTRACTS_COMMIT;
    info.contracts_build_time = VISIONRT_CONTRACTS_BUILD_TIME;
    return info;
}

} // namespace visionrt
