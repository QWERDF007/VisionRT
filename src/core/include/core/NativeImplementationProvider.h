#pragma once

#include "core/Export.h"

#include "core/Tool.h"
#include <span>
#include <string_view>

namespace visionrt {
struct ToolsPackage;

enum class ToolAvailability
{
    Available,
    DefinitionMissing,
    ImplementationNotLoaded,
    ImplementationUnavailable
};

struct Availability
{
    ToolAvailability state;
    ToolStatus status;
};

class VISIONRT_CORE_API NativeImplementationProvider
{
public:
    virtual ~NativeImplementationProvider() = default;
    virtual void registerPackage(const ToolsPackage &package) = 0;
    virtual void loadImplementations(std::span<const std::string_view> toolIds) = 0;
    virtual Availability availability(std::string_view toolId) const = 0;
    virtual std::unique_ptr<IToolInstance> createAdapter(std::string_view toolId) = 0;
};
}
