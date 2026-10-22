# VisionRT 工程文档索引

## 规格入口

- [三仓统一完整实施方案与工单](VisionRT_统一完整方案_Specs_Tickets.zip)：以包内 `00_README.md` 为入口，协议见 `02_跨仓行为约定.md`、`03_JSON与模块协议.md`，验收见 `06_验收矩阵.md`。
- [任务账本](../WORKLOG.md)：本地工作恢复与验证证据入口，不随代码提交。

## 构建与模块

| 职责 | 唯一实现入口 |
| --- | --- |
| 构建与部署 | [脚本说明](../tools/README.md)、[CMake](../CMakeLists.txt) |
| 共享契约 | [VisionRTContracts](../3rdparty/VisionRTContracts/README.md)、[公共头文件](../3rdparty/VisionRTContracts/include/visionrt/contracts) |
| 工程与工具注册 | [core](../src/core) |
| 值 JSON 编解码 | [ValueJson](../src/core/ValueJson.h) |
| 工程 JSON 存取 | [ProjectStorage](../src/core/ProjectStorage.h)、[工程样例](../examples) |
| 调度、快照与接收 | [runtime](../src/runtime) |
| 模块加载与生命周期 | [plugins](../src/plugins) |
| Python 实例适配 | [python](../src/python) |
| 工作台 | [ui](../src/ui) |
| 契约验证 | [tests](../tests)、[测试注册](../CMakeLists.txt) |
