#pragma once

namespace visionrt {
struct ExecutionMetrics
{
    double totalMs          = 0; // 总墙钟
    double prepareMs        = 0; // 参数准备墙钟
    bool   prepareMeasured  = false;
    double initMs           = 0; // 初始化墙钟
    bool   initMeasured     = false;
    double adapterExecuteMs = 0; // 工具Adapter整次execute墙钟（含Python桥接）
    bool   toolCallMeasured = false; // 本次执行是否真正进入工具调用边界
    double validateMs       = 0; // 结果统一检查与格式验证墙钟
    bool   validateMeasured = false;
    // Python 包含项（已包含在 adapterExecuteMs 中，不能再累加到 totalMs）。桥接算子是 Python 实现，
    // 因此工具 API 耗时取 pythonCallMs，其余阶段各带独立 measured 标志。
    bool   pythonBridge        = false;
    bool   pythonInputMeasured = false; // 是否进入 GIL 等待与输入桥接
    bool   pythonCallMeasured  = false; // 是否进入真实函数调用
    bool   pythonOutputMeasured = false; // 是否进入输出转换
    double waitGilMs      = 0; // Python 获取 GIL 等待时间
    double pythonInputMs  = 0; // Python 输入映射耗时
    double pythonCallMs   = 0; // Python 函数/类执行耗时
    double pythonOutputMs = 0; // Python 输出复制与转换耗时
};

class IPythonExecutionMetrics
{
public:
    virtual ~IPythonExecutionMetrics()             = default;
    virtual ExecutionMetrics pythonMetrics() const = 0;
};

}
