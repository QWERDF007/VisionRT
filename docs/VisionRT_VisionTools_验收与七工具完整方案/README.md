# VisionRT / VisionTools 最新提交验收与七工具完整改进方案

日期：2026-10-25（Asia/Shanghai）。验收分支：`refactor/abi4-writable-data`。

- VisionRT：`f6f57b03adea1c732492e635a7c09bdc19cbe26c`
- VisionTools：`b24307b7bcb687a3b8f9fe2269aff528b054de6f`

**结论：三项既有修复的方向正确；YOLO 修复已有本次原生 C++ 复测证据，关闭终态和连线修复完成源码与测试审查、仍需 Windows/Qt 实跑。七工具与智能点选尚未交付，不能按该目标验收通过。**

本包是审核和设计交付，没有修改或推送产品代码，没有部署安装包。未把历史 76/76 或旧部署截图当作本次验证证据。

| 文档 | 内容 |
|---|---|
| [00_验收报告.md](00_验收报告.md) | 本次变化、修复结论、问题及实现差距 |
| [01_完整改进方案.md](01_完整改进方案.md) | 以当前代码为起点的架构和分阶段路线 |
| [02_VisionRT_Specs.md](02_VisionRT_Specs.md) | 宿主补充规范，含异步读者屏障 |
| [03_VisionTools_Specs.md](03_VisionTools_Specs.md) | 工具与算法补充规范、测试目标分层 |
| [04_VisionRT_Tickets.md](04_VisionRT_Tickets.md) | 宿主专项工单及原工单状态 |
| [05_VisionTools_Tickets.md](05_VisionTools_Tickets.md) | 工具专项工单及原工单状态 |
| [06_验证证据与验收门槛.md](06_验证证据与验收门槛.md) | 实测范围、未运行范围、发布标准 |
| [07_CPP设计与整体优化.md](07_CPP设计与整体优化.md) | 封装、多态、模板、并发与性能改进 |
| [七工具目标规范/README.md](七工具目标规范/README.md) | 完整七工具方案、27项规范、28项原工单、10份JSON草案 |
| [七工具目标规范/diagrams/index.html](七工具目标规范/diagrams/index.html) | 8张逐工具SVG图，离线可查看 |
| `evidence/` | 本轮可复现探针、日志、提交差异与目录清单 |
| `tickets.csv`、`tickets.json` | 本轮10项专项工单及原28项实施工单的当前状态 |

规范优先级：本轮 02/03 补充规范 → 七工具目标规范的具体规格 → 参考文档。七工具端口和算法仍只有一套目标定义，不在本轮报告重复维护第二份表。目标JSON仍是待实现格式，不是现有 schema 2 能直接运行的工具包。
