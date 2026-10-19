# 工具包交付实现导航与代码证据

- [规格](tool-package-delivery/spec.md)：唯一的行为、契约和测试要求；通用工具包的后端、依赖与资源边界见 D-01、D-02、D-05，验收范围见 Testing Decisions。
- [工单索引](tool-package-delivery/README.md)：逐片交付与评审入口；阻塞关系只在对应工单维护。
- [开源互操作依据](interop-open-source-research.md)：上游引用、内存和解释器语义。

本文只维护实现接续点与研究证据，不重复规格。TimesAI 机制以用户提供的分析为输入，本轮未重新逆向其程序。

## 当前实现接续点

| 职责 | 源入口 |
| --- | --- |
| 工具声明及实现绑定 | [VisionTools 声明](F:/Projects/VisionTools/assets/tools)、[模块绑定](F:/Projects/VisionTools/tools/module_bindings.yaml) |
| 领域库 | [预处理目标](F:/Projects/VisionTools/src/preprocess/CMakeLists.txt)、[推理目标](F:/Projects/VisionTools/src/infer/CMakeLists.txt) |
| 构建与依赖清单 | [VisionTools 构建入口](F:/Projects/VisionTools/CMakeLists.txt)、[依赖配置](F:/Projects/VisionTools/tools/dependencies.yaml) |
| 生成与工具打包 | [生成器](F:/Projects/VisionTools/tools/generate_tool_contracts.py)、[package_tools.py](F:/Projects/VisionTools/tools/package_tools.py) |
| SDK 包读取与校验 | [SchemaYaml Interface](../3rdparty/VisionRTSDK/include/visionrt/sdk/SchemaYaml.h)、[实现](../3rdparty/VisionRTSDK/src/SchemaYaml.cpp)、[CLI](../3rdparty/VisionRTSDK/tools/visionrt_validate_tool.cpp) |
| SDK 值与模块契约 | [Value.h](../3rdparty/VisionRTSDK/include/visionrt/sdk/Value.h)、[Tool.h](../3rdparty/VisionRTSDK/include/visionrt/sdk/Tool.h)、[Module.h](../3rdparty/VisionRTSDK/include/visionrt/sdk/Module.h) |
| 节点输出类型 | [NodeContracts](../src/core/NodeContracts.h)、[实现](../src/core/NodeContracts.cpp)：编辑、持久化和执行共用；声明属性来自 SDK Schema |
| 宿主部署 | [package_app.py](../tools/package_app.py) 的 deploy_tools_package；[构建所选 Python](../cmake/ConfigPython.cmake) |
| 宿主工具发现与呈现 | [AppController](../src/ui/AppController.cpp)、[ToolRegistry](../src/core/ToolRegistry.cpp)、[ToolTreeModel](../src/ui/ToolTreeModel.cpp) |
| 库加载与实例 | [PluginLoader](../src/plugins/PluginLoader.cpp) |
| 平台接收节点及能力 | [Receiver Interface](../src/runtime/Receiver.h)、[实现](../src/runtime/Receiver.cpp)、[平台工具上下文](../src/core/Tool.h) |
| Python 转换与运行时 | [SDK PythonInterop](../3rdparty/VisionRTSDK/include/visionrt/sdk/PythonInterop.h)、[实现](../3rdparty/VisionRTSDK/src/PythonInterop.cpp)、[PythonRuntime](../src/python/PythonRuntime.cpp) |

SDK 源仓库为 `F:/Projects/VisionRTSDK`；父工程中的 SDK 是锁定版本的子模块。上述源码位置以当前工程为准，未来重构后只更新这份导航。

## 现有测试 Seam 的代码入口

| 验证面 | 既有入口 |
| --- | --- |
| 工具库声明生成与交付 | [生成测试](F:/Projects/VisionTools/tests/test_generator.py)、[打包测试](F:/Projects/VisionTools/tests/test_package.py)、[发行链测试](F:/Projects/VisionTools/tests/test_distribution.py)、[真实消费者](F:/Projects/VisionTools/tests/test_installation_consumer.cpp) |
| SDK 独立消费 | [消费测试](../3rdparty/VisionRTSDK/tests/test_sdk_consumer.cpp)、[隔离安装](../3rdparty/VisionRTSDK/tests/test_isolated_installation.py)、[CLI 测试](../3rdparty/VisionRTSDK/tests/test_cli.py) |
| 真实工具包到宿主执行 | [examples_contract](../tests/examples_contract.cpp)、[plugin_contract](../tests/plugin_contract.cpp) |
| 列表与平台接收 | [tool_registry_contract](../tests/tool_registry_contract.cpp)、[runtime_contract](../tests/runtime_contract.cpp)、[workbench_contract](../tests/workbench_contract.cpp) |
| Python 值与生命周期 | [python_contract](../tests/python_contract.cpp)、[runtime_contract](../tests/runtime_contract.cpp) |

具体场景与验收归属见规格的 Testing Decisions，不在此重复测试矩阵或历史通过数量。

## QuickTools 参考证据

参考版本 `f906b50`；本轮只读参考，不修改 QuickTools。

| 观察点 | 源证据与审查结论 |
| --- | --- |
| Mat 到 NumPy | [PythonInterface.h](F:/Projects/QuickTools/src/core/include/core/PythonInterface.h) 的 `toNumpy`；shape 和 `img.step` 映射可作为参考 |
| 无 base 的数组构造 | [该仓 pybind11 数组构造](F:/Projects/QuickTools/3rdparty/pybind11/include/pybind11/numpy.h) 的 `ptr && !base` 分支调用 `PyArray_NewCopy_`；不能把这一调用方式视为零拷贝 |
| NumPy 到 Mat | 同一 PythonInterface 中 `fromNumpy` 返回裸指针 Mat，未保留 NumPy owner 且未传入真实 strides；不能直接照搬 |
| Python 引用清理 | [PythonInterface.cpp](F:/Projects/QuickTools/src/core/PythonInterface.cpp) 的 `release` 与 `reloadModule` 丢弃 `.release()` 返回的引用；语义依据见开源互操作索引 |
| 实例复用 | [Yolov8 调用方](F:/Projects/QuickTools/src/deeplearning/detection/yolov8/Yolov8.cpp) 缓存模型实例，作为实际调用先例 |

## 开源入口

| 主题 | 一手来源 |
| --- | --- |
| ZIP | [CPython zipfile](https://github.com/python/cpython/blob/main/Lib/zipfile/__init__.py)、[标准库文档](https://docs.python.org/3/library/zipfile.html) |
| NumPy 绑定 | [pybind11](https://github.com/pybind/pybind11)；具体引用及版本适用范围见开源互操作索引 |
| 完整应用更新候选 | [WinSparkle](https://github.com/vslavik/winsparkle)、[Qt Installer Framework](https://doc.qt.io/qtinstallerframework/ifw-overview.html) |

选用范围与未采用机制统一由规格决定；研究来源不构成已完成迁移或已通过运行验证的证据。
