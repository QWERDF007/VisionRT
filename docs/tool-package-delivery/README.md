# 工具包交付规格与工单

- [规格](spec.md)：唯一的行为、契约、用户故事和测试场景定义；通用工具包边界见 D-01、D-02、D-05，样例与验收覆盖见 Testing Decisions。
- [实现导航与代码证据](../visiontools-bridge-architecture.md)：当前代码接续点、QuickTools 参考和开源入口。
- [Python 互操作开源依据](../interop-open-source-research.md)：上游语义与限制。

## 工单

| 工单 | 独立可验收的交付 |
| --- | --- |
| [01：统一真实工具包读取](tickets/01-unified-package-consumption.md) | 校验器与宿主通过同一语义读取、校验并执行真实目录包，先降低后续改动成本 |
| [02：SDK 可选 PythonInterop](tickets/02-sdk-python-interop.md) | 宿主实际执行 Python 工具时使用唯一 SDK 转换，核心 SDK 仍可独立消费 |
| [03：工具 ZIP 到宿主执行](tickets/03-zip-deployment.md) | 领域 DLL 经打包、ZIP 部署、元数据列表到实际执行形成闭环 |
| [04：Python 工具随 ZIP 交付](tickets/04-python-package-execution.md) | 脚本及必要资源离线安装、按索引展示并运行，不伪造 DLL |
| [05：平台节点声明外置](tickets/05-metadata-only-catalog.md) | 平台执行能力按包声明绑定，列表不再由宿主固定工具清单或目录扫描补齐 |
| [06：完整包更换与真实部署验收](tickets/06-offline-replacement.md) | 退出宿主后更换包、拒绝无效输入，并在脱离源码树的部署环境验证交付 |

每个文件是一个工单；具体阻塞关系、状态和验收项只在对应工单维护，不在这里复制。

## 评审与执行

待确认：规格的测试 Seam 是否合适；六片是否过粗或过细；阻塞关系是否仅包含真实前置条件；是否需要合并或拆分。

确认前不开始实现。确认后取所有阻塞工单均完成的 frontier；独立工单不人为串行化。每片以局部 TDD 开始，以真实资源消费和必要文档收敛结束，进度与验证证据只写仓库任务账本。
