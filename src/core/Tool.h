#pragma once

#include "visionrt/sdk/Tool.h"
#include "visionrt/sdk/Schema.h"
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace visionrt {

class IResultReader;
struct PlatformToolServices : ToolServices {
    using ToolServices::ToolServices;
    PlatformToolServices() = default;
    PlatformToolServices(const ToolServices& base) : ToolServices(base) {}
    PlatformToolServices(PathResolver p, std::shared_ptr<IResultReader> r = {})
        : ToolServices{std::move(p)}, results(std::move(r)) {}

    std::shared_ptr<IResultReader> results;
};

using ToolFactory = std::function<std::unique_ptr<IToolInstance>(const PlatformToolServices&)>;
}
