# VisionRT 工程文档索引

## 规格入口

- [最新交付：三仓审核完整方案、Specs 与 Tickets](#)：当前方案以打包件交付，见 `docs/VisionRT_三仓审核_完整方案_Specs_Tickets_20261028.zip`（00 阅读说明、01 现状与优先级、02 代码证据、03 附件审核、04 目标方案、05 Specs、06 Tickets、07 验证矩阵；逐条执行记录见 WORKLOG）。
- [任务账本](../WORKLOG.md)：本地工作恢复与验证证据入口，不随代码提交。

## T36 交付基线（2026-10-27）

| 项 | 版本 / 提交 |
| --- | --- |
| Contracts 子模块（VisionRT 与 VisionTools 一致，跟踪 `main`） | `72198d6`（ABI 6） |
| spdlog 子模块 | `6fa36017`（v1.15.3） |
| Qt | 6.6.2（msvc2019_64） |
| OpenCV | 4.8.0（vc16） |
| Python | 3.12.13（`tools/dependencies.json` 指向 anaconda3/envs/py312） |
| CUDA | 11.8（推理模块运行依赖） |
| 编译器 | MSVC 2019（14.29.30133）+ Ninja |
| 工具包 | VisionTools `dist/visiontools-deploy`（rtx40-build，22 工具）；测试夹具 `build/tools-package`（`--profile cpu` 的 21 工具包，无推理依赖） |

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
- 日志的保证边界：首条错误立即入队、重复只聚合后续次数并保留原等级与时间；Critical 不聚合不延迟；换行在日志层转义成可见序列，按 UTF-8 字符边界截断；关闭时先封生产者入口再排空，默认 Qt handler 与自定义 handler 都还原。契约见 [log_contract](../tests/log_contract.cpp)。

## 错误与状态

工具业务码、宿主错误码与“未执行”是三种不同的东西，各自只有一个来源：

| 概念 | 唯一来源 | 落地位置 |
| --- | --- | --- |
| 工具业务码与中文消息 | 各工具的错误域枚举 | VisionTools `src/*/errors/*.h`、[公共错误词汇](../src/common/errors/ToolError.h) |
| 宿主失败分类（忙/不存在/前置条件/参数/内部） | `visionrt::ErrorCode` | [Error.h](../src/core/Error.h)、[Error.cpp](../src/core/Error.cpp) |
| 失败返回值 | `ErrorInfo`，需要值时用 `Result<T>` | [Error.h](../src/core/Error.h)、[Result.h](../src/core/Result.h) |
| `__status_code` / `__status_message` 的写入 | 宿主在工具正常返回后物化 | [ToolValueValidation.cpp](../src/core/ToolValueValidation.cpp)、[NodeRuntime.cpp](../src/runtime/NodeRuntime.cpp) |

约定：工具返回 `ToolResult{outputs, status}`，`outputs` 只含声明里的业务端口；工具写状态端口即契约错误。跳过、阻断、取消与框架失败不伪造工具结果。节点 Tooltip 仍只显示三阶段耗时，错误只经输出参数与既有诊断展示。

### 宿主错误码与产生点

宿主只有 [ErrorCode](../src/core/Error.h) 一张表，所有宿主状态经 `makeStatus(code, message, detail)` 构造；同一条诊断在不同子系统不再各写一份数字。

| 码 | 含义 | 产生点（谁会说这个码） |
| --- | --- | --- |
| 0 | 成功 | 各阶段正常返回 |
| -1 | 内部错误 | 意外异常：插件初始化/释放抛错、Python 实例创建抛错、运行时未预期异常 |
| -2 | 参数不合法 | 工厂描述符非法、契约解析失败、工具初始化参数不合法 |
| -3 | 忙 | 同路径模块重入加载（只有这一处是真正的“忙”） |
| -4 | 不存在 | 工具定义缺失、模块已加载但未注册该工厂、Python 实现/脚本缺失 |
| -5 | 已取消 | 运行取消 |
| -6 | 未执行 | 跳过或上游未选中 |
| -7 | 前置条件不满足 | 实例已释放/已初始化、模块尚未加载、未初始化就执行 |
| -8 | 读回不匹配 | 读回标识与请求不一致 |
| -9 | 超时 | 受控等待超时 |
| -10 | 读写失败 | Python 包索引构建时的文件系统失败 |
| -11 | 模块加载失败 | 库文件缺失、DLL 目录配置失败、缺入口符号、ABI/结构体不匹配、空实例、重复工厂 ID |
| -12 | Python 不可用 | 解释器关闭、运行时刷新导致实例跨代、脚本导入/加载失败 |
| -13 | 数据不符合契约 | 工具未关联模块、Python 入口不是 `.py` 或缺少 symbol、工具输出违反声明 |
| -14 | 资源不可用 | 宿主绑定未注册、C++ 实现不可用（无插件加载器）、工厂返回空实例 |
| -15 | 不支持 | 能力未实现 |

消费侧按码分支：`AppController` 与节点诊断显示 `message`，需要区分“找不到 / 状态不对 / 环境不可用”时读 `code`；不再解析消息文本。工具自己报的状态原样透传（`PythonLoadState`、`RegistryError` 携带的就是工具或加载器的原状态），宿主只补自己的框架错误。

仓库里没有第二份码表：新增状态先加枚举与中文默认消息，再在产生点用 `makeStatus`，测试断言用 `hostCode(ErrorCode::...)` 而不是数字。

Python 侧的形状同样是契约：函数与类的 `execute` 返回 `{'status': {'code', 'message', 'detail'}, 'outputs': {...}}`，类的 `initialize`/`release` 只返回状态字典。工具不得写 `__status_code` / `__status_message`，这两个端口由宿主在工具成功返回后物化；写了就是契约错误。

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
| 测试分组 | 见下方“测试分组”一节；分组表在 [AddTest.cmake](../cmake/AddTest.cmake) |
| Windows 增量构建 | [构建入口](../tools/build.cmd)、[代码页与头文件依赖契约](../tests/build_incremental_contract.py) |

## 测试分组

每个测试恰好属于一个组；`tests-<组>` 构建该组需要的一切，`ctest -L <组>` 只运行该组。配置阶段会打印每个组的实际成员，组为空时直接失败（`sdk` 由 Contracts 子工程注册，`bridge`/`ui` 在 `VISIONRT_BUILD_APP=OFF` 下明确报告“不存在”而不是假装通过）。

| 组 | 范围 | 构建入口 |
| --- | --- | --- |
| `backend` | 宿主核心、运行时、工具包与 Python 契约 | `tests-backend` |
| `bridge` | 宿主到 Qt Quick 的桥接、视口、工作台窗口 | `tests-bridge` |
| `ui` | 真实窗口交互（同一时间只允许一个窗口用例） | `tests-ui` |
| `sdk` | Contracts 自身契约与隔离安装消费者 | `tests-sdk` |
| `installed` | 安装态工具包链路 | `tests-installed` |
| `package` | 校验器、JSON ZIP 部署、依赖清单、增量构建 | `tests-package` |
| `gpu` | 真实设备推理；缺设备或缺 YOLO fixture 时按 SKIP 返回码 77 报 NOT RUN | `tests-gpu` |
| `perf` | 基准与性能采样 | `tests-perf` |

日常只构建并运行改动涉及的组；发布前一次全量 `tests-all` 加 `ctest -L installed`。`ctest` 默认把空选择当成功，跨仓验收时加 `--no-tests=error` 或引用上面的分组打印。
