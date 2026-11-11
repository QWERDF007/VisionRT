# VisionRT 工程文档索引

## 当前方案

- [R45 阅读入口](R45/00_阅读入口.md)：自定义工具、Schema 6、ABI 9、数字编号与完整方案。
- [完整方案](R45/03_自定义工具完整方案.md)：架构规范、生命周期与实施。
- [工单](R45/07_Tickets.md)：T45 工单实施与验收。
- [任务账本](../WORKLOG.md)：实际进度与验证记录。

## 实现与版本入口

- 构建环境及依赖：[构建说明](../tools/README.md)、[依赖配置](../tools/dependencies.json)、[CMake](../CMakeLists.txt)。
- Contracts版本以父仓gitlink为准；组件构建信息见实际产物的build_info.json，生成入口为[WriteBuildInfo](../cmake/WriteBuildInfo.cmake)。
- 测试范围以当前CTest注册为准，运行结果见任务账本及对应原始日志，不以历史总数作为当前通过证明。

## 计时口径

所有耗时来自同一个 steady_clock，是嵌套区间而不是三次独立测量。实现入口：[节点执行窗口](../src/runtime/NodeRuntime.cpp)、[流程/全流程派生](../src/runtime/RuntimeEngine.cpp)、[显示规则](../src/ui/DurationText.cpp)。

| 显示 | 区间 | 语义 |
| --- | --- | --- |
| 预处理 / 算子时间 / 后处理 | 节点内实际进入的阶段 | 只显示测到的阶段；`0.000 ms` 是有效值，“未测量”不显示 |
| 节点总计 | 输入准备开始 → 最后一个阶段结束 | 各阶段之和不超过总计，差额仅是阶段之间的调度帧 |
| 流程 | 该流程内已执行节点窗口的并集 | 单节点流程时与节点总计相同 |
| 全流程 | 本轮所有流程窗口的并集 | 单流程时与流程相同；多流程差额来自并发错峰 |
| 已运行 | 会话启动 → 结束 | 含准备、执行与收尾读回，因此永不小于全流程 |

Release、500 轮的实测（[基准证据](../build/evidence/runtime_benchmark.json)，`performance_contract --rounds=500`）给出同一模型的数值：引擎流程 p50 1.00 ms、最慢节点 p50 0.46 ms、轮时减最慢节点 p50 0.53 ms、会话建立 p50 0.03 ms。多节点流程里除最慢节点外仍有接近一半的耗时，其中既有其他节点的计算，也有节点之间的调度帧，因此“轮时减最慢节点”不能当作纯调度间隙。基准 harness 自身用忙轮询而不是 `sleep_for(1ms)`，否则 Windows 的 15.6 ms 定时器精度会被误当成运行时开销。

构建元数据与日志：

- [构建元数据生成器](../cmake/WriteBuildInfo.cmake)：链接完成阶段写 `build_info.json`（宿主）与 `contracts-build-info.json`（Contracts 产物时间）；工具包由 VisionTools 构建结束时写同名文件。关于弹窗按当前机器本地时区显示并带偏移，缺失即“未知”，组件之间不互相顶替。
- [日志服务](../src/log/LogService.cpp)：整行纯文本 `[时间] [级别] [线程] [来源] 内容`，窗口与文件同一格式器；按本地日期落盘到 `<exe>/logs/visionrt_YYYY-MM-DD.log`，同日重启追加；目录不可写时降级到 stderr 并计入失败。
- 日志的保证边界：首条错误立即入队、重复只聚合后续次数并保留原等级与时间；Critical 不聚合不延迟；换行在日志层转义成可见序列，按 UTF-8 字符边界截断；关闭时先封生产者入口再排空，默认 Qt handler 与自定义 handler 都还原。契约按职责见 [文本与分配](../tests/log_text_contract.cpp)、[聚合与每日文件](../tests/log_aggregation_contract.cpp)、[生命周期](../tests/log_lifecycle_contract.cpp)。

## 错误与状态

工具业务码、宿主错误码与“未执行”是三种不同的东西，各自只有一个来源：

| 概念 | 唯一来源 | 落地位置 |
| --- | --- | --- |
| 工具业务码与中文消息 | 各工具的错误域枚举 | VisionTools仓库的 `src/*/errors/*.h` 与 `src/common/errors/ToolError.h` |
| 宿主失败分类（忙/不存在/前置条件/参数/内部） | `visionrt::ErrorCode` | [Error.h](../src/core/Error.h)、[Error.cpp](../src/core/Error.cpp) |
| 失败返回值与错误信息 | `ErrorInfo`、`ErrorCode` 与 `ToolStatus` | [Error.h](../src/core/Error.h)、[Tool.h](../3rdparty/VisionRTContracts/include/visionrt/contracts/Tool.h) |
| `__status_code` / `__status_message` 的写入 | SDK `toolResult` / `result()` 统一打包入 `outputs` | [Tool.h](../3rdparty/VisionRTContracts/include/visionrt/contracts/Tool.h)、[Parameters.h](../3rdparty/VisionRTContracts/include/visionrt/contracts/Parameters.h)、[NodeRuntime.cpp](../src/runtime/NodeRuntime.cpp) |

约定：工具返回 `ToolResult{outputs, detail}`（由 SDK `toolResult(outputs, status)` 构造），`outputs` 包含声明的业务端口及打包后的 `__status_code` 与 `__status_message`；`detail` 承载技术诊断。直接在业务中手动篡改状态端口属于保留违规。跳过、阻断、取消与框架失败不伪造工具结果。节点 Tooltip 仍只显示三阶段耗时，错误只经输出参数与既有诊断展示。

### 宿主错误码与产生点

宿主错误码与产生点唯一归属 [Error.h](../src/core/Error.h) 与 [Error.cpp](../src/core/Error.cpp)，仓库不维护第二份静态重复码表；所有宿主状态经 `makeStatus(code, message, detail)` 构造。

消费侧按码分支：`AppController` 与节点诊断显示 `message`，需要区分“找不到 / 状态不对 / 环境不可用”时读 `code`；不再解析消息文本。工具自己报的状态原样透传（`PythonLoadState`、`RegistryError` 携带的就是工具或加载器的原状态），宿主只补自己的框架错误。

仓库里没有第二份码表：新增状态先加枚举与中文默认消息，再在产生点用 `makeStatus`，测试断言用 `hostCode(ErrorCode::...)` 而不是数字。

Python 侧的形状同样是契约：函数与类的 `execute` 返回 `{'outputs': {...}, 'detail': '...'}`（由 SDK `result(outputs, status)` 统一封装打包，`outputs` 包含业务端口及 `__status_code` 与 `__status_message`），类的 `initialize`/`release` 只返回状态字典。工具避免不经 SDK 直接篡改状态端口。

## Python 环境

选择、检查、桥接创建与“能否执行 Python 工具”的判定全部归 VisionRT，VisionTools 不做环境检查。只有一份环境状态 `PythonEnvironmentStatus`（`unconfigured / probing / ready / invalid / backendFailed`，附一句中文原因），由 [PythonBackend](../src/python/PythonBackend.h) 持有：探测写入环境侧裁决，桥接创建写入后端侧裁决，界面、工具列表与节点诊断都读这一个值，不各自再推导一遍。

检查在 GUI 线程之外完成：[PythonProbe](../src/python/PythonProbe.h) 在 worker 上解析并以 `-I -B` 运行一次解释器，再把结果投递回对象线程。设置窗口在检查期间禁用提交，C++ 侧也拒绝并发提交，因此同一时刻只有一个探测进程。配置为空是合法状态（“未配置”）；配置存在但失效必须报告自己的原因（路径不存在 / 版本不兼容 / 缺 NumPy / 桥接缺失），不得回退成“未配置”。改解释器只写配置文件，下次启动才换运行时，运行中不热换 CPython。

工具侧闸口不变：`ToolRegistry::create` 在不可用状态直接抛 `RegistryError`，不调用工厂与 resolver；`NodeRuntime` 只写节点诊断，业务端口是 `Missing`。契约：[环境状态、原因与非阻塞提交](../tests/python_environment_contract.cpp)、[工厂闸口零调用](../tests/tool_registry_contract.cpp)、[探测正负例](../tests/python_probe_contract.cpp)。

发布取证走一次无窗口运行：`VisionRT.exe --python-report <file>` 写出 JSON，包含探测得到的解释器、前缀、`sys.path` 起点、实际提供 NumPy 的文件，以及本进程模块表里真实加载的 `python3*.dll` 与 `numpy*`（未配置时该表为空）。它不打开窗口，只构造控制器并等待环境检查落定，因此“没有随包解释器、也没有隐式加载”的结论可以对着进程取证，而不是对着配置推断。返回码 `0` 表示环境可用，`3` 表示报告已写出但环境不可用。

这些报告由 [release_evidence.py](../tools/release_evidence.py) 汇总成一份发布记录：两仓与 Contracts 的提交、构建所用的编译器与配置、安装包的导入链证据，以及每个场景的环境报告。它不采信“写好了测试”，只采信互相一致的事实——Contracts 提交必须相同，`ready` 场景实际加载的 `python3*.dll` 与 NumPy 必须落在所选 prefix/base 内，未配置进程不得出现任何 Python 模块，安装包启动链不得导入 Python。任一缺失或矛盾即退出 2 并标记 `complete=false`，避免半份证据被当成发布依据。

## 数组与 Python 类型

宿主 Array 是一块平坦缓冲加 Dense/Ragged 结构，Python 侧看到的类型只有一条规则：

| 源 | Python 类型 | 说明 |
| --- | --- | --- |
| Dense 数值数组（uint8/int32/int64/float32/float64/bool） | 可写 `numpy.ndarray`，与原生缓冲共享 | 视图的 capsule 持有完整 Array，最后一个视图释放后才释放缓冲并卸载模块 |
| Ragged 数组、字符串数组 | 嵌套 `list` | 没有矩形 NumPy 形式 |
| 作者返回 | 符合声明的 `list` 或 `ndarray` | 只按声明的 dtype/rank 精确匹配，不做隐式转换 |

这是对早期“所有参数一律 list”约定的显式修订，而不是那条约定的实现：Dense 数值数组现在以 `ndarray` 直接暴露给作者，Ragged/字符串仍走嵌套 `list`，不存在按工具切换的隐式语义。实现入口：[PythonInterop](../src/python/PythonInterop.cpp)；契约：[值桥接](../tests/python_value_contract.cpp)、[视图生命周期](../tests/python_array_lifetime_contract.cpp)。逻辑工具在唯一输入归一入口把 ndarray 换成受限表达式能用的 list（VisionTools `python/builtin/logic/common/engine.py`），宿主侧不复制图像/数值数组。

## 界面约束

重构沿用用户确认的三张 VisionRT V3 截图作为外观基准，保持既有布局、配色、间距、边框、图标、控件与节点外观；不另建样式体系，不将结构重构变成视觉改版。

运行态只改变节点卡片的标题底色、标题文字与 2px 边框（`#1a7f37` / 白色），不新增“执行中”、错误诊断或任何卡片文字，也不改变卡片尺寸、图标、版本与耗时区域；选中仍用独立蓝色虚线，运行绿框不被选中覆盖。

## 安装与升级

安装清单只记录**本轮实际写入**的文件：exe、项目 DLL、依赖、windeployqt 产物、工具包与 `qt.conf` 都经 `InstallOwnership` 登记；`custom-tools/**`、`logs/**`、`ui-preferences.json` 永不进入清单。清单在验证通过后才写，失败不会留下宣称完成的记录。清理只删除旧清单内的文件；清单缺失、含用户路径、绝对路径、`..`、重复项或文件/目录冲突时整次拒绝，不删除任何文件。契约见 [test_package_app.py](../tests/test_package_app.py)（三代升级逐文件 hash、保护路径与越界负例）。

## 构建与模块

| 职责 | 唯一实现入口 |
| --- | --- |
| 构建与部署 | [脚本说明](../tools/README.md)、[CMake](../CMakeLists.txt) |
| 共享契约 | [VisionRTContracts](../3rdparty/VisionRTContracts/README.md)、[公共头文件](../3rdparty/VisionRTContracts/include/visionrt/contracts)、[共享数据与依赖保活](../3rdparty/VisionRTContracts/include/visionrt/contracts/DataRef.h) |
| 作者函数与参数 SDK | [函数注册与适配](../3rdparty/VisionRTContracts/include/visionrt/contracts/FunctionTool.h)、[输入读取与输出组装](../3rdparty/VisionRTContracts/include/visionrt/contracts/Parameters.h)、[函数/类入口契约](../3rdparty/VisionRTContracts/tests/tool_registration_test.cpp)、[参数与状态契约](../3rdparty/VisionRTContracts/tests/parameter_mapping_test.cpp) |
| 工程与工具注册 | [core](../src/core) |
| 自定义工具编辑与存储 | [存储与模板/复制](../src/core/CustomToolStore.h)、[端口模型与改型/关系候选](../src/ui/ToolPortListModel.h)、[编辑器控制器](../src/ui/ToolEditorController.h)、[改型与删除对账](../src/core/ToolDefinition.h)、[编辑对话框](../src/ui/qml/CustomToolDialog.qml)、[存储契约](../tests/custom_tool_store_contract.cpp)、[端口模型契约](../tests/tool_port_list_model_test.cpp)、[控制器契约](../tests/tool_editor_controller_test.cpp) |
| 工具声明 JSON | [DefinitionJson](../src/core/DefinitionJson.h)、[声明模型](../src/core/ToolDefinition.h)、[值约束](../src/core/ToolValueValidation.h)、[声明契约](../tests/definition_json_contract.cpp) |
| 包索引、目录与交付校验 | [ModuleIndex](../src/core/ModuleIndex.h)、[ToolPackage](../src/core/ToolPackage.h)、[路径边界](../src/core/ToolPaths.h)、[双根发现契约](../tests/tool_discovery_contract.cpp)、[校验器](../tools/visionrt_validate_tool.cpp)、[JSON ZIP 部署、安装清单所有权与 PE 导入取证](../tools/package_app.py)、[部署、升级与导入表契约](../tests/test_package_app.py)、[真实构建集成](../tests/test_build_metadata_integration.py) |
| 发布证据汇总与审计 | [release_evidence.py](../tools/release_evidence.py)、[审计规则](../tests/test_release_evidence.py)（`release_evidence_contract`，`package` 组） |
| 值 JSON 编解码 | [ValueJson](../src/core/ValueJson.h)、[类型与数值契约](../tests/value_json_contract.cpp) |
| 输出发布与缓存值合同 | [节点合同](../src/core/NodeContracts.h)、[值校验](../src/core/ToolValueValidation.h)、[发布快照](../src/runtime/Snapshot.h)、[运行时契约](../tests/runtime_contract.cpp)、[共享可写值与工具存储策略契约](../tests/runtime_value_contract.cpp)、[原生 DLL 契约](../tests/plugin_contract.cpp)、[Python 作者工具执行](../tests/python_tools_execution_test.cpp)、[隐藏图像不外泄](../tests/presentation_visibility_contract.cpp) |
| 工程编辑与 JSON 存取 | [工程模型](../src/core/Project.h)、[序号解析](../src/core/Sequence.h)、[ProjectEditor](../src/core/ProjectEditor.h)、[工程操作状态唯一拥有者](../src/ui/ProjectOperations.h)、[ProjectStorage](../src/core/ProjectStorage.h)、[配置值构造](../src/core/ProjectValues.cpp)、[编辑契约](../tests/project_editor_contract.cpp)、[存储契约](../tests/project_storage_contract.cpp)、[配置隔离与文件值矩阵](../tests/project_value_contract.cpp)、[真实资源另存与跨盘 Python 读取](../tests/ui_project_paths_contract.cpp)、[真实UNC路径保存/重开/读取](../tests/ui_unc_paths_contract.cpp)、[工程样例](../examples) |
| 输出复制与下层连接 | [候选查询](../src/core/ProjectEditor.h)、[名称列连接入口](../src/ui/AppControllerBindings.cpp)、[输出面板](../src/ui/qml/OutputPanel.qml)、[真实窗口契约](../tests/workbench_popups.cpp) |
| 节点配置同步、调度与接收 | [RuntimeEngine](../src/runtime/RuntimeEngine.h)、[实例配置与所有权](../src/runtime/RuntimeInternal.h)、[节点执行](../src/runtime/NodeRuntime.cpp)、[Receiver](../src/runtime/Receiver.h)、[接收预检](../src/runtime/Receiver.cpp)、[宽 ID 真实选择与引用修复](../tests/ui_receiver_identity_contract.cpp)、[真实插件与工作台生命周期契约](../tests/plugin_contract.cpp) |
| 目录状态与显式加载 | [公共注册接口](../3rdparty/VisionRTContracts/include/visionrt/contracts/Registry.h)、[ToolRegistry](../src/core/ToolRegistry.h)、[PluginLoader](../src/plugins/PluginLoader.h)、[CLI 契约](../tests/validator_cli_contract.py)、[目录重扫事务契约](../tests/plugin_contract.cpp) |
| 原生实例、模块所有权与失败诊断 | [原生适配器](../src/plugins/PluginLoader.cpp)、[运行节点](../src/runtime/NodeRuntime.cpp)、[真实模块契约](../tests/module_registration_contract.cpp)、[输出跨 DLL 生命周期](../tests/native_value_lifetime_contract.cpp)、[插件契约](../tests/plugin_contract.cpp)、[CPU 工具与原始解码异常](../tests/cpu_tools_contract.cpp) |
| Python 实例适配与包加载 | [PythonRuntime](../src/python/PythonRuntime.h)、[包加载器](../src/python/managed_packages.py)、[适配器契约](../tests/python_adapter_contract.cpp)、[惰性包加载契约](../tests/python_loading_contract.cpp)、[真实发行消费契约](../tests/python_tools_execution_test.cpp)、[独立安装跨语言链路与裁剪所有权](../tests/installed_pipeline_contract.cpp)、[真实安装包Python作者工具](../tests/installed_python_author_contract.cpp) |
| Python 环境与设置 | [设置模型](../src/ui/UiPreferences.h)、[环境状态](../src/python/PythonEnvironment.h)、[探测](../src/python/PythonProbe.h)、[后端](../src/python/PythonBackend.h)、[环境与原因契约](../tests/python_environment_contract.cpp)、[设置键与类型契约](../tests/ui_preferences_contract.cpp) |
| Python 值桥接与生命周期 | [PythonInterop](../src/python/PythonInterop.h)、[图像存储桥接](../src/python/PythonImages.cpp)、[Python 对象保活](../src/python/PythonStorage.h)、[值契约](../tests/python_value_contract.cpp)、[视图生命周期契约](../tests/python_array_lifetime_contract.cpp)、[解释器关闭契约](../tests/python_shutdown_contract.cpp) |
| 执行状态、阶段与独立生命周期诊断 | [快照与诊断模型](../src/runtime/Snapshot.h)、[最终发布](../src/runtime/NodeRuntime.cpp)、[界面上下文与 JSON](../src/ui/Diagnostics.h)、[输出预览与捕获](../src/ui/OutputModel.h)、[真实模块及工作台契约](../tests/plugin_contract.cpp) |
| 运行时管理、调度与目录失败状态 | [运行时事件](../src/runtime/RuntimeEngine.h)、[节点活动事件与有界邮箱](../src/runtime/NodeActivity.h)、[邮箱契约](../tests/node_activity_mailbox_test.cpp)、[执行窗口契约](../tests/runtime_node_activity_contract.cpp)、[注册状态](../src/core/ToolRegistry.h)、[运行时契约](../tests/runtime_contract.cpp)、[真实释放诊断与队列溢出](../tests/runtime_diagnostics_contract.cpp)、[热路径无 JSON 重读与实例重建](../tests/runtime_hotpath_contract.cpp)、[运行期 Python 延迟创建](../tests/runtime_python_lazy_contract.cpp)、[真实停止与关窗事件](../tests/workbench_cancellation.cpp)、[真实停止/复位/关窗交错](../tests/workbench_lifecycle_contract.cpp)、[注册契约](../tests/tool_registry_contract.cpp) |
| 应用日志与诊断 | [日志服务](../src/log/LogService.h)、[有界日志模型](../src/log/LogModel.h)、[日志窗口](../src/ui/qml/LogDialog.qml)、[控制器诊断入口](../src/ui/AppControllerDiagnostics.cpp)、[日志文本契约](../tests/log_text_contract.cpp)、[日志聚合契约](../tests/log_aggregation_contract.cpp)、[日志生命周期契约](../tests/log_lifecycle_contract.cpp)、[真实窗口契约](../tests/workbench_startup.cpp) |
| 节点元数据与画布路由 | [节点读回](../src/ui/AppController.cpp)、[读回与原生值契约](../tests/ui_value_contract.cpp)、[路由器](../src/ui/WireRouter.h)、[画布](../src/ui/qml/FlowCanvas.qml)、[路由契约](../tests/wire_router_contract.cpp)、[真实窗口契约](../tests/flow_canvas_scenarios.cpp)、[同轨迹分阶段基准](../tests/wire_routing_benchmark.cpp) |
| 工作台结构与样式 | [主窗口](../src/ui/qml/Main.qml)、[流程页签与画布](../src/ui/qml/FlowCanvas.qml)、[节点模型与角色通知](../src/ui/NodeListModel.h)、[工具库](../src/ui/qml/ToolLibrary.qml)、[主题](../src/ui/Theme.h)、[ui](../src/ui)、[真实流程新增/切换/关闭](../tests/workbench_flows_contract.cpp)、[真实复制粘贴与内部连线](../tests/workbench_clipboard_contract.cpp)、[节点卡片](../src/ui/qml/NodeDelegate.qml)、[真实运行/成功/失败/取消状态与颜色](../tests/workbench_status_contract.cpp)、[运行期交互收口](../tests/workbench_interaction_contract.cpp)、[真实工具树搜索/折叠/展开](../tests/workbench_tooltree_contract.cpp)、[真实改参与发布原子替换](../tests/workbench_publication_contract.cpp)、[真实窄窗与100/150/200%DPI布局](../tests/workbench_layout_contract.cpp)、[不可用工具添加拒绝](../tests/workbench_unavailable_tool_contract.cpp) |
| 运行期交互 | [唯一可用条件与停止入口](../src/ui/AppController.h)、[停止/保存拒绝/单次/循环/单节点真实契约](../tests/workbench_interaction_contract.cpp) |
| ROI 编辑与拒绝诊断 | [编辑会话唯一拥有者](../src/ui/RoiEditSession.h)、[编码、语义视图与构造/编辑](../3rdparty/VisionRTContracts/include/visionrt/contracts/ROI.h)、[视图与几何契约](../3rdparty/VisionRTContracts/tests/test_roi_view_contract.cpp)、[构造与读回成本基准](../tests/roi_view_benchmark.cpp)、[编辑入口](../src/ui/AppControllerRoi.cpp)、[图像视口](../src/ui/ImageViewport.h)、[绘制交互契约](../tests/viewport_contract.cpp)、[原生值工作台契约](../tests/ui_value_contract.cpp)、[真实窗口契约](../tests/workbench_roi.cpp) |
| 展示存储与读取阶段 | [显示转换](../src/ui/DisplayConverter.h)、[非8位批量转换与同条件基准](../tests/display_conversion_benchmark.cpp)、[输出模型](../src/ui/OutputModel.h)、[读取调度](../src/ui/AppControllerReadback.cpp)、[运行时握手](../src/runtime/RuntimeEngine.h)、[异步文本与目录刷新完成顺序](../tests/ui_completion_contract.cpp)、[静默通知与像素读取契约](../tests/ui_notifications_contract.cpp)、[Windows 原生轮询探针](../tools/verify_ui_notifications.cjs)、[视口析构契约](../tests/viewport_lifetime_contract.cpp)、[执行中缓存帧与全文排队](../tests/output_text_readback_test.cpp)、[显示存储契约](../tests/display_storage_contract.cpp)、[工作台节点存储回收](../tests/ui_storage_contract.cpp)、[跨工程、端口类型与迟到显示隔离](../tests/ui_result_isolation_contract.cpp)、[输出值契约](../tests/output_value_contract.cpp)、[读取阶段契约](../tests/result_readback_barrier_test.cpp) |
| 真实工作台安装夹具与负载基准 | [安装夹具](../tests/WorkbenchFixtures.h)、[工作台交互](../tests/workbench_contract.cpp)、[零复制与负载基准](../tests/performance_contract.cpp)、[进程内存采样](../tests/ProcessMemory.h)、[值桥接分阶段基准](../tests/value_bridge_benchmark.cpp)、[真实画布路由基准](../tests/wire_routing_benchmark.cpp) |
| 契约验证 | [tests](../tests)、[测试分组与聚合目标](../cmake/AddTest.cmake)、[测试注册](../CMakeLists.txt)、[按组构建与运行](../tools/build.cmd) |
| 测试分组 | 见下方“测试分组”一节；分组表在 [AddTest.cmake](../cmake/AddTest.cmake) |
| Windows 增量构建 | [构建入口](../tools/build.cmd)、[代码页与头文件依赖契约](../tests/build_incremental_contract.py) |

## 测试分组

测试在 `AddTest.cmake` 中可被赋予多个组标签（例如 `ui` 与 `bridge` 存在交集）；全量注册测试项清单以 CTest 实际注册为唯一真相源（可通过 `ctest -N` 动态列出），`tests-<组>` 构建该组标签覆盖的目标，`ctest -L <组>` 只运行携带该标签的测试项。配置阶段会打印每个组的实际成员，组为空时直接失败（`sdk` 由 Contracts 子工程注册，`bridge`/`ui` 在 `VISIONRT_BUILD_APP=OFF` 下明确报告“不存在”而不是假装通过）。

| 组 | 范围 | 构建入口 |
| --- | --- | --- |
| `backend` | 宿主核心、运行时、工具包与 Python 契约 | `tests-backend` |
| `bridge` | 宿主到 Qt Quick 的桥接、视口、工作台窗口 | `tests-bridge` |
| `ui` | 真实窗口交互（同一时间只允许一个窗口用例） | `tests-ui` |
| `sdk` | Contracts 自身契约与隔离安装消费者 | `tests-sdk` |
| `installed` | 安装态工具包链路 | `tests-installed` |
| `package` | 校验器、JSON ZIP 部署、依赖清单、增量构建 | `tests-package` |
| `integration` | 需要真实递归编译的验收（六个嵌套 Git+CMake 元数据用例）；`ctest -L package -LE integration` 是包相关快速回归 | `tests-integration` |
| `gpu` | 真实设备推理；缺设备或缺 YOLO fixture 时按 SKIP 返回码 77 报 NOT RUN | `tests-gpu` |
| `perf` | 基准与性能采样 | `tests-perf` |

工作台的每个场景是一个独立 CTest 条目，不再有“聚合入口 + 独立条目”重复执行同一场景；`core` 与 `short-window` 也是独立条目。DPI/外观截图由 `-DVISIONRT_TEST_VISUAL=ON` 显式注册，默认回归只跑功能断言、不落截图。

VisionTools 侧同样按成本拆分：`image_read_cache_test` 用三张小图验证 LRU、reload、失败回滚与第二实例隔离，`image_read_cache_benchmark` 才跑 1600×900 的冷/暖/长循环并写 `read_cache_evidence.json`（`LABELS perf`，尺寸、图数、轮次均可显式传入）；`infer_trace_geometry` 与 `infer_trace_uninitialized` 是常规场景，`infer_trace_initialized` 需要引擎或权重，单独 `SKIP_RETURN_CODE 77` 并归 `gpu`。

日常只构建并运行改动涉及的组；发布前一次全量 `tests-all` 加 `ctest -L installed`。`ctest` 默认把空选择当成功，跨仓验收时加 `--no-tests=error` 或引用上面的分组打印。
