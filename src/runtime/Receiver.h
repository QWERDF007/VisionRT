#pragma once

#include "core/Project.h"
#include "core/ToolRegistry.h"

#include <chrono>

namespace visionrt {
struct ReceiveRequest
{
    std::string               flowId;
    std::string               nodeSequence;
    std::string               port;
    TypeSpec                  type;
    std::chrono::milliseconds timeout{1000};
};

struct ReceiveCursor
{
    std::uint64_t session     = 0;
    NodeId        source      = 0;
    std::uint64_t publication = 0;
};

// Injected only as a read-only capability. It cannot edit, start, or cancel a flow.
class IResultReader
{
public:
    virtual ~IResultReader()                                                         = default;
    virtual ToolResult receive(const ReceiveRequest &request, ReceiveCursor &cursor) = 0;
};

void registerReceiveTools(ToolRegistry &registry);
} // namespace visionrt
