#pragma once

// The host's own error vocabulary. Tools have their own positive business codes; this catalog only
// describes what a node, a registry or a project operation did before or after a tool ran. One
// enum and one value type for the whole host: no per-subsystem error classes.

#include <visionrt/contracts/Tool.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace visionrt {

enum class ErrorCode : std::int64_t
{
    Ok                  = 0,
    Internal            = -1,
    InvalidArgument     = -2,
    Busy                = -3,
    NotFound            = -4,
    Cancelled           = -5,
    NotExecuted         = -6,
    FailedPrecondition  = -7,
    ReadbackMismatch    = -8,
    Timeout             = -9,
    IoError             = -10,
    PluginLoadFailed    = -11,
    PythonUnavailable   = -12,
    DataInvalid         = -13,
    ResourceUnavailable = -14,
    Unsupported         = -15
};

std::string_view defaultMessage(ErrorCode code) noexcept;

// The integer a status carries. Diagnostics keep using the same numbers, so an existing consumer
// that compares codes still works.
constexpr std::int64_t hostCode(ErrorCode code) noexcept
{
    return static_cast<std::int64_t>(code);
}

// A failure a caller can branch on: a stable code, the sentence a user should read, and the
// optional technical cause. `code` is the integer the status carries, so a consumer can compare it
// without knowing the enum; `kind()` names that same value for a switch.
struct ErrorInfo
{
    std::int64_t code = 0;
    std::string  message;
    std::string  detail;

    ErrorInfo() = default;

    ErrorInfo(ErrorCode value, std::string text = {}, std::string cause = {})
        : code(hostCode(value))
        , message(text.empty() ? std::string(defaultMessage(value)) : std::move(text))
        , detail(std::move(cause))
    {
    }

    bool ok() const noexcept
    {
        return code == 0;
    }

    explicit operator bool() const noexcept
    {
        return ok();
    }

    ErrorCode kind() const noexcept
    {
        return static_cast<ErrorCode>(code);
    }

    // The wire form diagnostics consume, so the runtime model keeps one status type.
    ToolStatus toStatus() const
    {
        return {code, message, detail};
    }
};

// One construction point for a host status: when the call site has nothing more specific to say,
// the code's own sentence is used, so no status ever ships an empty message. A forwarded tool
// status never goes through here - the host does not rewrite what a tool reported.
inline ToolStatus makeStatus(ErrorCode code, std::string message = {}, std::string detail = {})
{
    return ErrorInfo(code, std::move(message), std::move(detail)).toStatus();
}

} // namespace visionrt
