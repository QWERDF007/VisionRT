#pragma once

// A value or the reason there is none. Deliberately minimal: no monadic chains, no async, no
// throwing accessor. A caller checks ok() (or the pointer accessors) and branches on the code.

#include "core/Error.h"

#include <utility>
#include <variant>

namespace visionrt {

template<class T>
class [[nodiscard]] Result
{
public:
    Result(T value)
        : storage_(std::move(value))
    {
    }

    Result(ErrorInfo error)
        : storage_(std::move(error))
    {
    }

    bool ok() const noexcept
    {
        return std::holds_alternative<T>(storage_);
    }

    explicit operator bool() const noexcept
    {
        return ok();
    }

    // Precondition: ok().
    const T &value() const
    {
        return std::get<T>(storage_);
    }

    T &&take()
    {
        return std::move(std::get<T>(storage_));
    }

    // Precondition: !ok().
    const ErrorInfo &error() const
    {
        return std::get<ErrorInfo>(storage_);
    }

    const T *valueIf() const noexcept
    {
        return std::get_if<T>(&storage_);
    }

    const ErrorInfo *errorIf() const noexcept
    {
        return std::get_if<ErrorInfo>(&storage_);
    }

private:
    std::variant<T, ErrorInfo> storage_;
};

} // namespace visionrt
