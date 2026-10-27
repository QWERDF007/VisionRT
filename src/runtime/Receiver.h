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

enum class ReceiveDisposition
{
    Value, // 源节点发布了本次会话的新结果，值可用
    Missing, // 源节点已终结但没有该端口的可用值
    Timeout,
    Canceled,
    Capacity,
    ProtocolError
};

// Raw transport outcome: the terminal status of the source plus the value it
// actually published. It never decides whether a receiving tool may use a value
// that exists, so a failed source keeps its diagnostics and partial results.
struct ReceiveOutcome
{
    ReceiveDisposition disposition = ReceiveDisposition::ProtocolError;
    ToolStatus         sourceStatus;
    PortValue          value = Missing{};
    std::string        message;
};

// Injected only as a read-only capability. It cannot edit, start, or cancel a flow.
class IResultReader
{
public:
    virtual ~IResultReader() = default;
    virtual ReceiveOutcome receive(const ReceiveRequest &request, ReceiveCursor &cursor) = 0;
};

void registerReceiveTools(ToolRegistry &registry);
} // namespace visionrt
