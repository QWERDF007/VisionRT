#include "core/Error.h"

namespace visionrt {

std::string_view defaultMessage(ErrorCode code) noexcept
{
    switch (code)
    {
    case ErrorCode::Ok:
        return "成功";
    case ErrorCode::Internal:
        return "内部错误";
    case ErrorCode::InvalidArgument:
        return "参数不合法";
    case ErrorCode::Busy:
        return "运行时忙";
    case ErrorCode::NotFound:
        return "目标不存在";
    case ErrorCode::Cancelled:
        return "操作已取消";
    case ErrorCode::NotExecuted:
        return "节点未执行";
    case ErrorCode::FailedPrecondition:
        return "当前状态不允许此操作";
    case ErrorCode::ReadbackMismatch:
        return "结果读回标识不匹配";
    case ErrorCode::Timeout:
        return "操作超时";
    case ErrorCode::IoError:
        return "文件读写失败";
    case ErrorCode::PluginLoadFailed:
        return "工具模块加载失败";
    case ErrorCode::PythonUnavailable:
        return "Python 运行环境不可用";
    case ErrorCode::DataInvalid:
        return "数据不符合契约";
    case ErrorCode::ResourceUnavailable:
        return "所需资源不可用";
    case ErrorCode::Unsupported:
        return "不支持的操作";
    }
    return "内部错误";
}

} // namespace visionrt
