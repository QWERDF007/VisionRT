# VisionRT 工程文档索引

## 规格入口

- [三仓统一完整实施方案与工单](VisionRT_统一完整方案_Specs_Tickets-新.zip)：以包内 `00_README.md` 为入口，协议见 `02_跨仓行为约定.md`、`03_JSON与模块协议.md`，验收见 `06_验收矩阵.md`。
- [任务账本](../WORKLOG.md)：本地工作恢复与验证证据入口，不随代码提交。

## 界面约束

重构沿用用户确认的三张 VisionRT V3 截图作为外观基准，保持既有布局、配色、间距、边框、图标、控件与节点外观；不另建样式体系，不将结构重构变成视觉改版。

## 构建与模块

| 职责 | 唯一实现入口 |
| --- | --- |
| 构建与部署 | [脚本说明](../tools/README.md)、[CMake](../CMakeLists.txt) |
| 共享契约 | [VisionRTContracts](../3rdparty/VisionRTContracts/README.md)、[公共头文件](../3rdparty/VisionRTContracts/include/visionrt/contracts)、[共享数据与依赖保活](../3rdparty/VisionRTContracts/include/visionrt/contracts/DataRef.h) |
| 工程与工具注册 | [core](../src/core) |
| 工具声明 JSON | [DefinitionJson](../src/core/DefinitionJson.h)、[声明模型](../src/core/ToolDefinition.h)、[值约束](../src/core/ToolValueValidation.h)、[声明契约](../tests/definition_json_contract.cpp) |
| 包索引、目录与交付校验 | [ModuleIndex](../src/core/ModuleIndex.h)、[ToolPackage](../src/core/ToolPackage.h)、[路径边界](../src/core/ToolPaths.h)、[双根发现契约](../tests/tool_discovery_contract.cpp)、[校验器](../tools/visionrt_validate_tool.cpp)、[JSON ZIP 部署](../tools/package_app.py)、[部署契约](../tests/test_package_app.py) |
| 值 JSON 编解码 | [ValueJson](../src/core/ValueJson.h)、[类型与数值契约](../tests/value_json_contract.cpp) |
| 输出发布与缓存值合同 | [节点合同](../src/core/NodeContracts.h)、[值校验](../src/core/ToolValueValidation.h)、[发布快照](../src/runtime/Snapshot.h)、[运行时契约](../tests/runtime_contract.cpp)、[共享可写值与工具存储策略契约](../tests/runtime_value_contract.cpp)、[原生 DLL 契约](../tests/plugin_contract.cpp)、[Python 契约](../tests/python_contract.cpp) |
| 工程编辑与 JSON 存取 | [工程模型](../src/core/Project.h)、[序号解析](../src/core/Sequence.h)、[ProjectEditor](../src/core/ProjectEditor.h)、[ProjectStorage](../src/core/ProjectStorage.h)、[配置值构造](../src/core/ProjectValues.cpp)、[编辑契约](../tests/project_editor_contract.cpp)、[存储契约](../tests/project_storage_contract.cpp)、[配置隔离与文件值矩阵](../tests/project_value_contract.cpp)、[真实资源另存与跨盘 Python 读取](../tests/ui_project_paths_contract.cpp)、[真实UNC路径保存/重开/读取](../tests/ui_unc_paths_contract.cpp)、[工程样例](../examples) |
| 输出关联与拒绝诊断 | [绑定动作与候选查询](../src/core/ProjectEditor.h)、[工作台绑定入口](../src/ui/AppControllerBindings.cpp)、[真实窗口契约](../tests/workbench_popups.cpp) |
| 节点配置同步、调度与接收 | [RuntimeEngine](../src/runtime/RuntimeEngine.h)、[实例配置与所有权](../src/runtime/RuntimeInternal.h)、[节点执行](../src/runtime/NodeRuntime.cpp)、[Receiver](../src/runtime/Receiver.h)、[接收预检](../src/runtime/Receiver.cpp)、[宽 ID 真实选择与引用修复](../tests/ui_receiver_identity_contract.cpp)、[真实插件与工作台生命周期契约](../tests/plugin_contract.cpp) |
| 目录状态与显式加载 | [公共注册接口](../3rdparty/VisionRTContracts/include/visionrt/contracts/Registry.h)、[ToolRegistry](../src/core/ToolRegistry.h)、[PluginLoader](../src/plugins/PluginLoader.h)、[CLI 契约](../tests/validator_cli_contract.py)、[目录重扫事务契约](../tests/plugin_contract.cpp) |
| 原生实例、模块所有权与失败诊断 | [原生适配器](../src/plugins/PluginLoader.cpp)、[运行节点](../src/runtime/NodeRuntime.cpp)、[真实模块契约](../tests/module_registration_contract.cpp)、[输出跨 DLL 生命周期](../tests/native_value_lifetime_contract.cpp)、[插件契约](../tests/plugin_contract.cpp)、[CPU 工具与原始解码异常](../tests/cpu_tools_contract.cpp) |
| Python 实例适配与包加载 | [PythonRuntime](../src/python/PythonRuntime.h)、[包加载器](../src/python/managed_packages.py)、[适配器契约](../tests/python_adapter_contract.cpp)、[惰性包加载契约](../tests/python_loading_contract.cpp)、[真实发行消费契约](../tests/python_contract.cpp)、[独立安装跨语言链路与裁剪所有权](../tests/installed_pipeline_contract.cpp) |
| Python 值桥接与生命周期 | [PythonInterop](../src/python/PythonInterop.h)、[图像存储桥接](../src/python/PythonImages.cpp)、[Python 对象保活](../src/python/PythonStorage.h)、[值契约](../tests/python_value_contract.cpp)、[解释器关闭契约](../tests/python_shutdown_contract.cpp) |
| 执行状态、阶段与独立生命周期诊断 | [快照与诊断模型](../src/runtime/Snapshot.h)、[最终发布](../src/runtime/NodeRuntime.cpp)、[界面上下文与 JSON](../src/ui/Diagnostics.h)、[输出预览与捕获](../src/ui/OutputModel.h)、[真实模块及工作台契约](../tests/plugin_contract.cpp) |
| 运行时管理、调度与目录失败状态 | [运行时事件](../src/runtime/RuntimeEngine.h)、[注册状态](../src/core/ToolRegistry.h)、[运行时契约](../tests/runtime_contract.cpp)、[真实释放诊断与队列溢出](../tests/runtime_diagnostics_contract.cpp)、[热路径无 JSON 重读与实例重建](../tests/runtime_hotpath_contract.cpp)、[真实停止与关窗事件](../tests/workbench_cancellation.cpp)、[真实停止/复位/关窗交错](../tests/workbench_lifecycle_contract.cpp)、[注册契约](../tests/tool_registry_contract.cpp) |
| 应用提示与诊断导出 | [统一诊断模型](../src/ui/Diagnostics.h)、[控制器诊断入口](../src/ui/AppControllerDiagnostics.cpp)、[文本对话框](../src/ui/qml/WorkbenchTextDialog.qml)、[真实窗口契约](../tests/workbench_startup.cpp) |
| 节点元数据与画布路由 | [节点读回](../src/ui/AppController.cpp)、[读回与原生值契约](../tests/ui_value_contract.cpp)、[路由器](../src/ui/WireRouter.h)、[画布](../src/ui/qml/FlowCanvas.qml)、[路由契约](../tests/wire_router_contract.cpp)、[真实窗口契约](../tests/workbench_canvas.cpp)、[同轨迹分阶段基准](../tests/wire_routing_benchmark.cpp) |
| 工作台结构与样式 | [主窗口](../src/ui/qml/Main.qml)、[流程页签与画布](../src/ui/qml/FlowCanvas.qml)、[工具库](../src/ui/qml/ToolLibrary.qml)、[主题](../src/ui/Theme.h)、[ui](../src/ui)、[真实流程新增/切换/关闭](../tests/workbench_flows_contract.cpp)、[真实复制粘贴与内部连线](../tests/workbench_clipboard_contract.cpp)、[真实运行/成功/失败/取消状态与颜色](../tests/workbench_status_contract.cpp)、[真实工具树搜索/折叠/展开](../tests/workbench_tooltree_contract.cpp)、[真实改参与发布原子替换](../tests/workbench_publication_contract.cpp)、[真实窄窗与100/150/200%DPI布局](../tests/workbench_layout_contract.cpp) |
| ROI 编辑与拒绝诊断 | [编辑入口](../src/ui/AppControllerRoi.cpp)、[图像视口](../src/ui/ImageViewport.h)、[绘制交互契约](../tests/viewport_contract.cpp)、[原生值工作台契约](../tests/ui_value_contract.cpp)、[真实窗口契约](../tests/workbench_roi.cpp) |
| 展示存储与读取阶段 | [显示转换](../src/ui/DisplayConverter.h)、[输出模型](../src/ui/OutputModel.h)、[读取调度](../src/ui/AppControllerReadback.cpp)、[运行时握手](../src/runtime/RuntimeEngine.h)、[异步文本与目录刷新完成顺序](../tests/ui_completion_contract.cpp)、[静默通知与像素读取契约](../tests/ui_notifications_contract.cpp)、[Windows 原生轮询探针](../tools/verify_ui_notifications.cjs)、[视口析构契约](../tests/viewport_lifetime_contract.cpp)、[执行中缓存帧与全文排队](../tests/ui_readback_contract.cpp)、[显示存储契约](../tests/display_storage_contract.cpp)、[工作台节点存储回收](../tests/ui_storage_contract.cpp)、[跨工程、端口类型与迟到显示隔离](../tests/ui_result_isolation_contract.cpp)、[输出值契约](../tests/output_value_contract.cpp)、[读取阶段契约](../tests/runtime_readback_contract.cpp) |
| 真实工作台安装夹具与负载基准 | [安装夹具](../tests/WorkbenchFixtures.h)、[工作台交互](../tests/workbench_contract.cpp)、[零复制与负载基准](../tests/performance_contract.cpp)、[进程内存采样](../tests/ProcessMemory.h)、[值桥接分阶段基准](../tests/value_bridge_benchmark.cpp)、[真实画布路由基准](../tests/wire_routing_benchmark.cpp) |
| 契约验证 | [tests](../tests)、[测试注册](../CMakeLists.txt) |
| Windows 增量构建 | [构建入口](../tools/build.cmd)、[代码页与头文件依赖契约](../tests/build_incremental_contract.py) |
