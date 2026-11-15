#include "core/Error.h"

namespace visionrt {

std::string_view defaultMessage(ErrorCode code) noexcept
{
    switch (code)
    {
#define VISIONRT_ERROR(name, code, message) case ErrorCode::name: return message;
#include "core/Errors.def"
#undef VISIONRT_ERROR
    }
    return "内部错误";
}

} // namespace visionrt
