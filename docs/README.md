# VisionRT 工程文档索引

## 规格与验收

- [工具包交付与 Python 互操作规格](tool-package-delivery/spec.md)：行为与验收要求的唯一规格。
- [工单索引](tool-package-delivery/README.md)：分片交付边界及工单状态。
- [实现导航与代码证据](visiontools-bridge-architecture.md)：三仓职责、测试 Seam 和 QuickTools 参考。
- [开源互操作依据](interop-open-source-research.md)：上游所有权、数组与解释器语义。
- [工具包交付验收](VisionRT-VisionTools-验收评估报告.md)：当前交付范围、证据及未运行项。
- [任务账本](../WORKLOG.md)：恢复工作的唯一进度入口。

## 构建与部署

- [工具脚本说明](../tools/README.md)：构建、依赖和宿主部署命令。
- [VisionTools](F:/Projects/VisionTools/README.md)：工具声明作者源、领域 DLL 与显式 ZIP 打包。
- [SDK 独立构建与安装消费](../3rdparty/VisionRTSDK/README.md)：核心 SDK 和可选 PythonInterop。

## 模块入口

| 职责 | 源码 |
| --- | --- |
| 包与声明合同 | [SDK](../3rdparty/VisionRTSDK/include/visionrt/sdk) |
| 工程与注册 | [core](../src/core) |
| DAG 调度与接收 | [runtime](../src/runtime) |
| DLL 加载与生命周期 | [plugins](../src/plugins) |
| Python 解释器与实例 | [python](../src/python) |
| 工作台呈现 | [ui](../src/ui) |
| 契约测试 | [tests](../tests)、[CMake 注册](../CMakeLists.txt) |

工具列表只来自安装包元数据；作者声明与实现位置统一由实现导航索引，不在宿主文档复制工具清单。
