#pragma once

#include "core/PythonLoadState.h"
#include "core/ToolDefinition.h"
#include "visionrt/contracts/Tool.h"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace visionrt {

class IResultReader;

struct PlatformToolServices : ToolServices
{
    using ToolServices::ToolServices;
    PlatformToolServices() = default;

    PlatformToolServices(const ToolServices &base)
        : ToolServices(base)
    {
    }

    PlatformToolServices(PathResolver p, std::shared_ptr<IResultReader> r = {})
        : ToolServices{std::move(p)}
        , results(std::move(r))
    {
    }

    std::shared_ptr<IResultReader> results;
};

using ToolFactory = std::function<std::unique_ptr<IToolInstance>(const PlatformToolServices &)>;

struct PythonFactoryFunctor
{
    std::shared_ptr<PythonLoadState>                                            loadState;
    std::function<std::unique_ptr<IToolInstance>(const PlatformToolServices &)> create;

    std::unique_ptr<IToolInstance> operator()(const PlatformToolServices &services) const
    {
        return create(services);
    }
};
} // namespace visionrt
