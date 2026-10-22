# VisionRT 工程文档索引

## 规格入口

- [三仓统一完整实施方案与工单](VisionRT_统一完整方案_Specs_Tickets.zip)：以包内 `00_README.md` 为入口，协议见 `02_跨仓行为约定.md`、`03_JSON与模块协议.md`，验收见 `06_验收矩阵.md`。
- [任务账本](../WORKLOG.md)：本地工作恢复与验证证据入口，不随代码提交。

## 界面约束

重构沿用用户确认的三张 VisionRT V3 截图作为外观基准，保持既有布局、配色、间距、边框、图标、控件与节点外观；不另建样式体系，不将结构重构变成视觉改版。

## 构建与模块

| 职责 | 唯一实现入口 |
| --- | --- |
| 构建与部署 | [脚本说明](../tools/README.md)、[CMake](../CMakeLists.txt) |
| 共享契约 | [VisionRTContracts](../3rdparty/VisionRTContracts/README.md)、[公共头文件](../3rdparty/VisionRTContracts/include/visionrt/contracts) |
| 工程与工具注册 | [core](../src/core) |
| 工具声明 JSON | [DefinitionJson](../src/core/DefinitionJson.h)、[声明模型](../src/core/ToolDefinition.h)、[值约束](../src/core/ToolValueValidation.h) |
| 包索引、目录与交付校验 | [ModuleIndex](../src/core/ModuleIndex.h)、[ToolPackage](../src/core/ToolPackage.h)、[校验器](../tools/visionrt_validate_tool.cpp)、[JSON ZIP 部署](../tools/package_app.py)、[部署契约](../tests/test_package_app.py) |
| 值 JSON 编解码 | [ValueJson](../src/core/ValueJson.h) |
| 输出发布与缓存值合同 | [节点合同](../src/core/NodeContracts.h)、[值校验](../src/core/ToolValueValidation.h)、[发布快照](../src/runtime/Snapshot.h)、[运行时契约](../tests/runtime_contract.cpp)、[原生 DLL 契约](../tests/plugin_contract.cpp)、[Python 契约](../tests/python_contract.cpp) |
| 工程编辑与 JSON 存取 | [ProjectEditor](../src/core/ProjectEditor.h)、[ProjectStorage](../src/core/ProjectStorage.h)、[编辑契约](../tests/project_editor_contract.cpp)、[存储契约](../tests/project_storage_contract.cpp)、[工程样例](../examples) |
| 输出关联与拒绝诊断 | [绑定动作与候选查询](../src/core/ProjectEditor.h)、[工作台绑定入口](../src/ui/AppControllerBindings.cpp)、[真实窗口契约](../tests/workbench_popups.cpp) |
| 节点配置同步、调度与接收 | [RuntimeEngine](../src/runtime/RuntimeEngine.h)、[实例配置与所有权](../src/runtime/RuntimeInternal.h)、[节点执行](../src/runtime/NodeRuntime.cpp)、[Receiver](../src/runtime/Receiver.h)、[真实插件与工作台生命周期契约](../tests/plugin_contract.cpp) |
| 目录状态与显式加载 | [公共注册接口](../3rdparty/VisionRTContracts/include/visionrt/contracts/Registry.h)、[ToolRegistry](../src/core/ToolRegistry.h)、[PluginLoader](../src/plugins/PluginLoader.h)、[CLI 契约](../tests/validator_cli_contract.py) |
| 原生实例、模块所有权与失败诊断 | [原生适配器](../src/plugins/PluginLoader.cpp)、[运行节点](../src/runtime/NodeRuntime.cpp)、[真实模块契约](../tests/module_registration_contract.cpp)、[插件契约](../tests/plugin_contract.cpp) |
| Python 实例适配与值转换 | [PythonRuntime](../src/python/PythonRuntime.h)、[PythonInterop](../src/python/PythonInterop.h) |
| 执行状态、阶段与独立生命周期诊断 | [快照与诊断模型](../src/runtime/Snapshot.h)、[最终发布](../src/runtime/NodeRuntime.cpp)、[界面上下文与 JSON](../src/ui/Diagnostics.h)、[输出预览与捕获](../src/ui/OutputModel.h)、[真实模块及工作台契约](../tests/plugin_contract.cpp) |
| 运行时管理、调度与目录失败状态 | [运行时事件](../src/runtime/RuntimeEngine.h)、[注册状态](../src/core/ToolRegistry.h)、[运行时契约](../tests/runtime_contract.cpp)、[注册契约](../tests/tool_registry_contract.cpp) |
| 应用提示与诊断导出 | [统一诊断模型](../src/ui/Diagnostics.h)、[控制器诊断入口](../src/ui/AppControllerDiagnostics.cpp)、[文本对话框](../src/ui/qml/WorkbenchTextDialog.qml)、[真实窗口契约](../tests/workbench_startup.cpp) |
| 工作台结构与样式 | [主窗口](../src/ui/qml/Main.qml)、[主题](../src/ui/Theme.h)、[ui](../src/ui) |
| ROI 编辑与拒绝诊断 | [编辑入口](../src/ui/AppControllerRoi.cpp)、[真实窗口契约](../tests/workbench_roi.cpp) |
| 契约验证 | [tests](../tests)、[测试注册](../CMakeLists.txt) |
