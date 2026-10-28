# VisionRT 工程文档索引

## 规格入口

- [最新交付：Contracts_dev 完整方案、Specs 与 Tickets](#)：本轮 T36 方案以打包件交付，见 `docs/VisionRT_VisionTools_Contracts_dev_完整方案_Specs_Tickets_20261027.zip`（含 00_dev基线与实施方案、02–05 Specs、06_Tickets、07_前版复核与验证清单、08_设计参考；解压后按条目对照 WORKLOG 执行记录）。
- [任务账本](../WORKLOG.md)：本地工作恢复与验证证据入口，不随代码提交。

## T36 交付基线（2026-10-27）

| 项 | 版本 / 提交 |
| --- | --- |
| Contracts 子模块（VisionRT 与 VisionTools 一致） | `14f6be8`（ABI 5） |
| spdlog 子模块 | `6fa36017`（v1.15.3） |
| Qt | 6.6.2（msvc2019_64） |
| OpenCV | 4.8.0（vc16） |
| Python | 3.12.13（`tools/dependencies.json` 指向 anaconda3/envs/py312） |
| CUDA | 11.8（推理模块运行依赖） |
| 编译器 | MSVC 2019（14.29.30133）+ Ninja |
| 工具包 | VisionTools `dist/visiontools-deploy`（rtx40-build，22 工具）；测试夹具 `build/tools-package`（21 工具精简包） |

未验证项：无（GPU 已于 RTX 4060 / sm89 用真实 yolov8n.wts 完成双阶段端到端验收，见 WORKLOG T36 第四批）。

## 计时口径

所有耗时来自同一个 steady_clock，是嵌套区间而不是三次独立测量。实现入口：[节点执行窗口](../src/runtime/NodeRuntime.cpp)、[流程/全流程派生](../src/runtime/RuntimeEngine.cpp)、[显示规则](../src/ui/DurationText.cpp)。

| 显示 | 区间 | 语义 |
| --- | --- | --- |
| 预处理 / 算子时间 / 后处理 | 节点内实际进入的阶段 | 只显示测到的阶段；`0.000 ms` 是有效值，“未测量”不显示 |
| 节点总计 | 输入准备开始 → 最后一个阶段结束 | 各阶段之和不超过总计，差额仅是阶段之间的调度帧 |
| 流程 | 该流程内已执行节点窗口的并集 | 单节点流程时与节点总计相同 |
| 全流程 | 本轮所有流程窗口的并集 | 单流程时与流程相同；多流程差额来自并发错峰 |
| 已运行 | 会话启动 → 结束 | 含准备、执行与收尾读回，因此永不小于全流程 |

Release、500 轮的实测（[基准证据](../build/evidence/runtime_benchmark.json)，`performance_contract --rounds=500`）给出同一模型的数值：引擎流程 p50 1.00 ms、最慢节点 p50 0.46 ms、节点间调度间隙 p50 0.53 ms、会话建立 p50 0.03 ms。多节点流程里约一半时间是节点之间的调度帧而不是算子本身——这正说明三层时间应当不同，而单节点单流程时必然相同。基准 harness 自身用忙轮询而不是 `sleep_for(1ms)`，否则 Windows 的 15.6 ms 定时器精度会被误当成运行时开销。

构建元数据与日志：

- [构建元数据生成器](../cmake/WriteBuildInfo.cmake)：链接完成阶段写 `build_info.json`（宿主）与 `contracts-build-info.json`（Contracts 产物时间）；工具包由 VisionTools 构建结束时写同名文件。关于弹窗按当前机器本地时区显示并带偏移，缺失即“未知”，组件之间不互相顶替。
- [日志服务](../src/log/LogService.cpp)：整行纯文本 `[时间] [级别] [线程] [来源] 内容`，窗口与文件同一格式器；按本地日期落盘到 `<exe>/logs/visionrt_YYYY-MM-DD.log`，同日重启追加；目录不可写时降级到 stderr 并计入失败。

## 界面约束

重构沿用用户确认的三张 VisionRT V3 截图作为外观基准，保持既有布局、配色、间距、边框、图标、控件与节点外观；不另建样式体系，不将结构重构变成视觉改版。

## 构建与模块

| 职责 | 唯一实现入口 |
| --- | --- |
| 构建与部署 | [脚本说明](../tools/README.md)、[CMake](../CMakeLists.txt) |
| 共享契约 | [VisionRTContracts](../3rdparty/VisionRTContracts/README.md)、[公共头文件](../3rdparty/VisionRTContracts/include/visionrt/contracts)、[共享数据与依赖保活](../3rdparty/VisionRTContracts/include/visionrt/contracts/DataRef.h) |
| 作者函数与参数 SDK | [函数注册与适配](../3rdparty/VisionRTContracts/include/visionrt/contracts/FunctionTool.h)、[输入读取与输出组装](../3rdparty/VisionRTContracts/include/visionrt/contracts/Parameters.h)、[函数/类入口契约](../3rdparty/VisionRTContracts/tests/test_sdk_function_tool.cpp)、[参数与状态契约](../3rdparty/VisionRTContracts/tests/test_sdk_parameters.cpp) |
| 工程与工具注册 | [core](../src/core) |
| 工具声明 JSON | [DefinitionJson](../src/core/DefinitionJson.h)、[声明模型](../src/core/ToolDefinition.h)、[值约束](../src/core/ToolValueValidation.h)、[声明契约](../tests/definition_json_contract.cpp) |
| 包索引、目录与交付校验 | [ModuleIndex](../src/core/ModuleIndex.h)、[ToolPackage](../src/core/ToolPackage.h)、[路径边界](../src/core/ToolPaths.h)、[双根发现契约](../tests/tool_discovery_contract.cpp)、[校验器](../tools/visionrt_validate_tool.cpp)、[JSON ZIP 部署](../tools/package_app.py)、[部署契约](../tests/test_package_app.py) |
| 值 JSON 编解码 | [ValueJson](../src/core/ValueJson.h)、[类型与数值契约](../tests/value_json_contract.cpp) |
| 输出发布与缓存值合同 | [节点合同](../src/core/NodeContracts.h)、[值校验](../src/core/ToolValueValidation.h)、[发布快照](../src/runtime/Snapshot.h)、[运行时契约](../tests/runtime_contract.cpp)、[共享可写值与工具存储策略契约](../tests/runtime_value_contract.cpp)、[原生 DLL 契约](../tests/plugin_contract.cpp)、[Python 契约](../tests/python_contract.cpp) |
| 工程编辑与 JSON 存取 | [工程模型](../src/core/Project.h)、[序号解析](../src/core/Sequence.h)、[ProjectEditor](../src/core/ProjectEditor.h)、[ProjectStorage](../src/core/ProjectStorage.h)、[配置值构造](../src/core/ProjectValues.cpp)、[编辑契约](../tests/project_editor_contract.cpp)、[存储契约](../tests/project_storage_contract.cpp)、[配置隔离与文件值矩阵](../tests/project_value_contract.cpp)、[真实资源另存与跨盘 Python 读取](../tests/ui_project_paths_contract.cpp)、[真实UNC路径保存/重开/读取](../tests/ui_unc_paths_contract.cpp)、[工程样例](../examples) |
| 输出复制与下层连接 | [候选查询](../src/core/ProjectEditor.h)、[名称列连接入口](../src/ui/AppControllerBindings.cpp)、[输出面板](../src/ui/qml/OutputPanel.qml)、[真实窗口契约](../tests/workbench_popups.cpp) |
| 节点配置同步、调度与接收 | [RuntimeEngine](../src/runtime/RuntimeEngine.h)、[实例配置与所有权](../src/runtime/RuntimeInternal.h)、[节点执行](../src/runtime/NodeRuntime.cpp)、[Receiver](../src/runtime/Receiver.h)、[接收预检](../src/runtime/Receiver.cpp)、[宽 ID 真实选择与引用修复](../tests/ui_receiver_identity_contract.cpp)、[真实插件与工作台生命周期契约](../tests/plugin_contract.cpp) |
| 目录状态与显式加载 | [公共注册接口](../3rdparty/VisionRTContracts/include/visionrt/contracts/Registry.h)、[ToolRegistry](../src/core/ToolRegistry.h)、[PluginLoader](../src/plugins/PluginLoader.h)、[CLI 契约](../tests/validator_cli_contract.py)、[目录重扫事务契约](../tests/plugin_contract.cpp) |
| 原生实例、模块所有权与失败诊断 | [原生适配器](../src/plugins/PluginLoader.cpp)、[运行节点](../src/runtime/NodeRuntime.cpp)、[真实模块契约](../tests/module_registration_contract.cpp)、[输出跨 DLL 生命周期](../tests/native_value_lifetime_contract.cpp)、[插件契约](../tests/plugin_contract.cpp)、[CPU 工具与原始解码异常](../tests/cpu_tools_contract.cpp) |
| Python 实例适配与包加载 | [PythonRuntime](../src/python/PythonRuntime.h)、[包加载器](../src/python/managed_packages.py)、[适配器契约](../tests/python_adapter_contract.cpp)、[惰性包加载契约](../tests/python_loading_contract.cpp)、[真实发行消费契约](../tests/python_contract.cpp)、[独立安装跨语言链路与裁剪所有权](../tests/installed_pipeline_contract.cpp)、[真实安装包Python作者工具](../tests/installed_python_author_contract.cpp) |
| Python 值桥接与生命周期 | [PythonInterop](../src/python/PythonInterop.h)、[图像存储桥接](../src/python/PythonImages.cpp)、[Python 对象保活](../src/python/PythonStorage.h)、[值契约](../tests/python_value_contract.cpp)、[解释器关闭契约](../tests/python_shutdown_contract.cpp) |
| 执行状态、阶段与独立生命周期诊断 | [快照与诊断模型](../src/runtime/Snapshot.h)、[最终发布](../src/runtime/NodeRuntime.cpp)、[界面上下文与 JSON](../src/ui/Diagnostics.h)、[输出预览与捕获](../src/ui/OutputModel.h)、[真实模块及工作台契约](../tests/plugin_contract.cpp) |
| 运行时管理、调度与目录失败状态 | [运行时事件](../src/runtime/RuntimeEngine.h)、[注册状态](../src/core/ToolRegistry.h)、[运行时契约](../tests/runtime_contract.cpp)、[真实释放诊断与队列溢出](../tests/runtime_diagnostics_contract.cpp)、[热路径无 JSON 重读与实例重建](../tests/runtime_hotpath_contract.cpp)、[运行期 Python 延迟创建](../tests/runtime_python_lazy_contract.cpp)、[真实停止与关窗事件](../tests/workbench_cancellation.cpp)、[真实停止/复位/关窗交错](../tests/workbench_lifecycle_contract.cpp)、[注册契约](../tests/tool_registry_contract.cpp) |
| 应用日志与诊断 | [日志服务](../src/log/LogService.h)、[有界日志模型](../src/log/LogModel.h)、[日志窗口](../src/ui/qml/LogDialog.qml)、[控制器诊断入口](../src/ui/AppControllerDiagnostics.cpp)、[日志行/每日文件/降级契约](../tests/log_contract.cpp)、[真实窗口契约](../tests/workbench_startup.cpp) |
| 节点元数据与画布路由 | [节点读回](../src/ui/AppController.cpp)、[读回与原生值契约](../tests/ui_value_contract.cpp)、[路由器](../src/ui/WireRouter.h)、[画布](../src/ui/qml/FlowCanvas.qml)、[路由契约](../tests/wire_router_contract.cpp)、[真实窗口契约](../tests/workbench_canvas.cpp)、[同轨迹分阶段基准](../tests/wire_routing_benchmark.cpp) |
| 工作台结构与样式 | [主窗口](../src/ui/qml/Main.qml)、[流程页签与画布](../src/ui/qml/FlowCanvas.qml)、[工具库](../src/ui/qml/ToolLibrary.qml)、[主题](../src/ui/Theme.h)、[ui](../src/ui)、[真实流程新增/切换/关闭](../tests/workbench_flows_contract.cpp)、[真实复制粘贴与内部连线](../tests/workbench_clipboard_contract.cpp)、[真实运行/成功/失败/取消状态与颜色](../tests/workbench_status_contract.cpp)、[真实工具树搜索/折叠/展开](../tests/workbench_tooltree_contract.cpp)、[真实改参与发布原子替换](../tests/workbench_publication_contract.cpp)、[真实窄窗与100/150/200%DPI布局](../tests/workbench_layout_contract.cpp)、[不可用工具添加拒绝](../tests/workbench_unavailable_tool_contract.cpp) |
| ROI 编辑与拒绝诊断 | [编辑入口](../src/ui/AppControllerRoi.cpp)、[图像视口](../src/ui/ImageViewport.h)、[绘制交互契约](../tests/viewport_contract.cpp)、[原生值工作台契约](../tests/ui_value_contract.cpp)、[真实窗口契约](../tests/workbench_roi.cpp) |
| 展示存储与读取阶段 | [显示转换](../src/ui/DisplayConverter.h)、[输出模型](../src/ui/OutputModel.h)、[读取调度](../src/ui/AppControllerReadback.cpp)、[运行时握手](../src/runtime/RuntimeEngine.h)、[异步文本与目录刷新完成顺序](../tests/ui_completion_contract.cpp)、[静默通知与像素读取契约](../tests/ui_notifications_contract.cpp)、[Windows 原生轮询探针](../tools/verify_ui_notifications.cjs)、[视口析构契约](../tests/viewport_lifetime_contract.cpp)、[执行中缓存帧与全文排队](../tests/ui_readback_contract.cpp)、[显示存储契约](../tests/display_storage_contract.cpp)、[工作台节点存储回收](../tests/ui_storage_contract.cpp)、[跨工程、端口类型与迟到显示隔离](../tests/ui_result_isolation_contract.cpp)、[输出值契约](../tests/output_value_contract.cpp)、[读取阶段契约](../tests/runtime_readback_contract.cpp) |
| 真实工作台安装夹具与负载基准 | [安装夹具](../tests/WorkbenchFixtures.h)、[工作台交互](../tests/workbench_contract.cpp)、[零复制与负载基准](../tests/performance_contract.cpp)、[进程内存采样](../tests/ProcessMemory.h)、[值桥接分阶段基准](../tests/value_bridge_benchmark.cpp)、[真实画布路由基准](../tests/wire_routing_benchmark.cpp) |
| 契约验证 | [tests](../tests)、[测试分组与聚合目标](../cmake/AddTest.cmake)、[测试注册](../CMakeLists.txt)、[按组构建与运行](../tools/build.cmd) |
| Windows 增量构建 | [构建入口](../tools/build.cmd)、[代码页与头文件依赖契约](../tests/build_incremental_contract.py) |
