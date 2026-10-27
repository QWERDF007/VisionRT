# WORKLOG

> 本文件是项目唯一的任务账本。真实日志按最新在前追加在固定示例条目之后，并固定位于其他真实日志之上；`⏳ 待你裁决` 始终固定在顶部。

## ⏳ 待你裁决

（暂无待裁决事项）

---

## 日志

<!--
建议格式：

### YYYY-MM-DD — 简短任务名

**目标**
- ...

**当前状态**
- 已完成：...
- 未完成：...

**验证证据**
- `command ...` → 关键结果
- 未验证项请明确写“未验证”

**下一步**
- ...
-->

### 2026-10-27 — T35 统一参数 / 最近工程 / 按需打包落地（ABI 5）

**目标**
- 依据 `docs/VisionRT_VisionTools_最新验收_统一参数_最近工程_按需打包_完整方案_Specs_Tickets_20261026.zip` 的 T35-03…T35-17，在现有 VisionRT / VisionTools / Contracts 代码上落地；临时目录 `F://tmp`。

**当前状态**
- 已完成 T35-03：`ToolResult` 收敛为单一 `outputs` 通道，工具以 `toolResult(outputs, status)` 自行写入 `__status_code` / `__status_message`；宿主不再拆分或补写状态；模块 ABI 升到 5；C++/Python/Receiver/Select/插件适配器与全部测试夹具同步。
- 已完成 T35-04：VisionTools 各工具在 `execute` 首行解析 `upstream_status`，非零时不产生算法与磁盘副作用并返回未处理状态；无默认值的图像输入声明改为允许缺失，避免“未选中分支”被当成必填错误。
- 已完成 T35-05：手动提交、声明默认值、工程加载与运行取值共用 `InputNormalizer`；加载时越界值按声明收敛并一次性告警，工程随之标脏。
- 已完成 T35-09：输出可连接性由后端 `candidateInputsWithState` 派生（按结构版本缓存），名称列高亮与菜单只在存在合法且仍空闲的下游输入时可用；诊断改为走数据列。
- 已完成 T35-10：Python 算子时间改报 `pythonCallMs`，GIL 等待与输入桥接计入预处理、输出转换计入后处理，各阶段带独立 measured 标志，不再用“总计减算子”推算。
- 已完成 T35-11：EasyTouch 种子与光标统一到当前图像坐标系（`transformedRoiBounds`，圆按半径缩放），并新增坐标契约。
- 已完成 T35-12/13/14（VisionTools）：历史缓存命中即提升为最近使用、超出预算时拒绝保留而非撑破预算；读图先构造完整候选（Catalog/Cursor/解码结果/缓存写入）再交换状态，失败不提前清空上次成功批次，reload 失败不混用像素；序列规划改为显式 `Found/End/RestartRequested`，目录尾部被删时非循环结束而非静默回头；计划内文件缺失时重扫一次再决策。
- 已完成 T35-15/17：新增 `tools/package_profiles.json` 与 `tools/package_profile.py`（cpu / rtx40-runtime / rtx40-build / gpu-custom，按 SM 选择 builder 分区、PTX 仅在显式前向兼容时保留、未知布局拒绝打包），`package_tools.py` 支持 `--profile/--sm/--engine-build` 并把精确清单与排除原因写入包内 `dependencies.json`；TensorRT builder 资源安装从 `nvinfer_builder_resource*.dll` 通配改为按 `VISIONTOOLS_TRT_TARGET_SMS` 精确安装（本机实测仅安装 `_sm89_` 一个分区）；VisionRT 的 OpenCV 运行时从目录 `*.dll` 通配改为按实际链接的导入库推导。
- 已完成 T35-16：新增 `RecentProjects`（最多 10 项、按平台大小写规则去重、同名项以最短父目录后缀区分、缺失文件保留可恢复）+ `UiPreferences` 同一文件原子读写与合并；`AppController` 暴露只读 `recentProjects` 与 `openRecentProject`，仅在 `replaceProject` 提交且 `completed == Open` 后记录，另存成功才入列、普通 Ctrl+S 不扰动顺序；文件菜单新增“最近打开工程”二级菜单，空列表与关闭期禁用。
- 未完成：T35-18 的 GPU 实机与干净安装验收（本机无目标 40 系设备/完整 SDK 交付目录），以及 Windows 真实窗口的菜单键盘导航人工确认。

**验证证据**
- `cmake --build build`（VisionRT）→ 0 错误；
- `ctest --test-dir build`（VisionRT）→ 82/83 通过；唯一失败 `build_incremental_contract` 为嵌套 MSVC 配置在此环境下的既有失败（`rc` 步骤返回 0 但被判定失败），与本次改动无关；
- `ctest --test-dir build`（VisionTools）→ 15/15 通过，含新增 LRU/原子提交/序列尾部删除与 `test_package_profile`；
- `cmake -S . -B build`（VisionTools）→ 输出 `TensorRT builder 资源：安装 1 个文件，目标 SM=89，PTX=OFF`；
- 依据本机 TensorRT 10.16 实际文件名（`_sm75_/_sm86_/_sm89_/_sm120_/_ptx_`）运行选择逻辑：cpu 仅公共依赖、rtx40-runtime 无 builder 分区、rtx40-build 仅 `_sm89_`；
- 未验证：GPU 实机 engine 加载与 build_wts 构建、无 GPU 目标机上的安装包启动。

**下一步**
- 在目标 40 系机器执行 `dependencies.json` 清单对应的真实 engine 加载与 build_wts→engine→推理验证，以及干净环境安装启动。


### 2026-10-26 — 依据参数规范化 / EasyTouch / 统一主题复核方案落地，声明升 schema 4、工程升 format 5 并部署 F:\tmp\VisionRT

**目标**
- 依据《VisionRT_VisionTools_参数规范化_EasyTouch_统一主题_复核与完整方案_20261026-1646》（T-I01–T-I08、T-E01、T-R34、T-O34、T-T01–T03、T-U33、T-P01–T03），在 VisionRT / VisionTools 现有代码上落地；临时目录 `F:\tmp`，部署到 `F:\tmp\VisionRT`。
- 按要求分阶段提交推送；`WORKLOG.md` 与 `docs/` 方案（含交互 HTML）不入库，但变更仍记录于本文件。

**当前状态**
- 已完成（契约）：新增 `src/core/InputNormalizer.{h,cpp}` 作为手动值、上层值与声明默认值的唯一规范化入口（min/max 收敛、options/default 补齐、Missing 规则）；`ToolDefinition` 物化宿主端口 `upstream_status` / `__status_code` / `__status_message`，作者重复声明即拒绝；`DefinitionJson` 导出按 `isHostOwnedPort` 过滤；工具声明 schema 4、工程 format 5；执行条件持久化为 `{mode, source}`，format 4 工程迁移保留原配置，旧工程加载静默补齐公共端口、不再产生结构告警。
- 已完成（调度）：`NodeRuntime` 移除按上游业务状态的阻断与清空，失败但合法的发布继续参与下游取参并原样转发状态码/消息；`Receiver` 只报告 `ReceiveOutcome`，业务可用性交接收工具；条件节点 formula/upstream 双模式在运行期生效；单节点运行不再清零计时统计。
- 已完成（界面）：参数行来源高亮由 `inputMode` 驱动（literal 模式不再显示上游高亮，解绑保留模式）；EasyTouch 工具栏开关 + `UiPreferences` 持久化 + `AssistPreview::releaseSession` 异步释放（复位/重扫/关闭分别触发）；底部与悬浮卡片计时收敛为预处理/算子/后处理三行，只统计完整运行会话；`VisionRT.Style` 统一由 `registerStyleModule()` 注册并扩充 Theme 语义属性；运行期间保存排队到运行结束后写入，文本读取优先于保存请求。
- 已完成（VisionTools）：内置与自定义声明全部升 schema 4（索引 22 条工具、3 个模块）；条件分支新增 `condition_mode` 与 `on_true_status` / `on_false_status`；读图拆出 `ImageSequence`（批次规划）与解码缓存，`count` 上限 64，不循环时按需重扫、循环按游标推进，解码失败消息与 OpenCV 细节分离；补齐 `visiontools.image.rotate` / `visiontools.image.brightness` 工厂注册。
- 已完成（工程化）：两仓分阶段提交并推送，均与 `origin/refactor/abi4-writable-data` 同步：VisionRT `ac33e6f`（核心契约）→ `43941db`（调度）→ `24eb73c`（界面/主题）→ `5c5e459`、`4ec2211`（测试稳定性）；VisionTools `83c6244`（声明升级）→ `d6bab3d`（读图重构与工厂注册）。
- 已完成（测试稳定性）：工作台夹具与 `examples_contract` 复制工具包时跳过 `nvinfer*`。此前每个用例复制 3.2 GB 至 `%TEMP%`，系统盘占满导致 “catalog did not become ready” 随机失败；修复后全量耗时由约 11 min 降至约 3 min，重复运行稳定。
- 已完成（部署）：`F:\tmp\VisionRT` 可用，工具包为 schema 4 版本；部署脚本在本代理环境被批量删除保护（>50 文件）中断，`platforms/`、`opencv_world480.dll`、`msvcp140*.dll` 由 `windeployqt` 与手工复制补齐。
- 未完成：T-P01/T-P02 的 `ToolResult` 单通道与原生 ABI 提升未实施（仍为 `status + outputs` 双通道，Python 桥接仍拆状态）；主题只覆盖本轮涉及控件，未做全仓 QML 历史硬编码的逐文件清理与 100/125/150/200% 缩放全量复核。

**验证证据**
- VisionTools `ctest --test-dir build` → `100% tests passed, 0 tests failed out of 13`（`test_infer_whitebox_trace` 按设计 Skipped）。
- VisionRT `ctest --test-dir build --output-on-failure` → `80/81` 通过；唯一失败 `build_incremental_contract` 属沙箱限制（vcvars 子进程链接期 `rc`/`mt` 不可用），非代码缺陷。
- `visionrt_validate_tool.exe --package F:\Projects\VisionTools\dist\visiontools-deploy` → `Valid tools package`；`module_index.json` 中 modules=3、tools=22。
- `F:\tmp\VisionRT\VisionRT.exe -v` → `VisionRT 0.1.0`（补齐 `platforms/`、`opencv_world480.dll`、`msvcp140*.dll` 前返回 `0xC0000135`）。
- `installed_pipeline_contract.exe F:\tmp\VisionRT\tools-package` → `Installed C++ -> Python -> C++ Image/Array/ROI and project roundtrip passed`。
- `installed_python_author_contract.exe F:\tmp\VisionRT\tools-package` → `Installed Python ROI and image-edit author tools executed through RuntimeEngine`。
- 部署包抽样哈希与 `dist/visiontools-deploy` 一致（条件分支声明、`vst_image.dll`、`opencv_world480.dll`、ROI 几何脚本）；部署目录 3.5 GB，无残留暂存/备份目录。

**下一步**
- 若继续：实施 T-P01/T-P02（`ToolResult{outputs}` + ABI 提升 + Python 桥接全量转换），完成主题全量迁移与多缩放复核。
- 部署复现：`package_app.py` 在带批量删除保护的代理环境会中断，建议在无该保护的环境执行 `--force-clean`，或先清理目标目录后再补齐依赖与 Qt 插件。

### 2026-10-25 — 依据 VisionRT_VisionTools_f6f57b03_验收报告与七工具完整改进方案 完成七工具补齐、schema 3 / 工程 format 4、节点模型物化与执行门控

**目标**
- 依据《VisionRT_VisionTools_f6f57b03_验收报告与七工具完整改进方案_Specs_Tickets_20261025》（工单 T31-RT-01 ~ T31-RT-06、T31-VT-01 ~ T31-VT-04，Specs、七工具目标规范、端口契约与验收矩阵），在 VisionRT / VisionTools 两仓现有代码上完成开发，临时目录使用 `D:\tmp`，完成后部署到 `E:\Softwares\VisionRT`。
- 七工具目标：旋转图像、调整亮度、图像读取（批量/序列）、圆测量、行间距测量、评估表达式、条件分支；配套两个隐藏辅助工具（圆智能点选、行间距智能点选）与宿主合流节点。

**当前状态**
- 已完成 (VisionRT 宿主)：声明 schema 3（`node_model`、`visibility`、`interaction`、`alignment_input`）与工程 format 4（`expression_config`、`execution_guard`、`select_type`）的解析、校验、回写与往返；节点契约单一物化入口 `nodeContract(node, definition)` 与派生初始化参数 `nodeParameters()`；原子编辑事务 `setExpressionConfig` / `setExecutionGuard` / `setSelectType`（含 guard 依赖边合并、复制重映射、前向引用与 `nextPortSeq` 校验）；稳定 `port_id`；执行门控与 `NodeDisposition`（Pending/Running/Succeeded/Failed/Skipped/Blocked/Canceled），上游跳过/失败按必需性传播，`Receiver` 对 Skipped/Blocked 立即返回终态；host 节点 `visionrt.control.select` 合流（透传并保留 `Owner`）；辅助工具 `visibility=assist` 不进入工具菜单；QML 新增 `NodeModelBar`、`ExpressionEditor`、`GuardEditor`、`SelectTypeEditor`。
- 已完成 (VisionTools 工具包)：新增 `visiontools.image.rotate`、`visiontools.image.brightness`、`visiontools.measure.circle`、`visiontools.measure.row_spacing`、`visiontools.logic.expression`、`visiontools.logic.branch`，重写 `visiontools.image.read`（file/directory/list、read_mode=all/sequence、end_policy=wrap/hold/error、recursive/extensions/paths_in/limit/max_batch_bytes）；新增隐藏辅助工具 `visiontools.assist.circle` / `visiontools.assist.row_spacing`（有界候选搜索 + 复用正式内核 + proposal 逆变换回参考坐标）；新增 `visiontools_metrology` 原生模块（`EdgeProfile` 双线性采样/切向平均/Gaussian/抛物线亚像素、`CircleFit` SVD 初值 + Huber 加权 GN + MAD 内点、`RowSpacingKernel` 配对 width/gap/pitch）；新增受限 AST 引擎（vendored simpleeval 1.0.3 + 白名单 + 资源上限）；`package_catalog.json` 与 22 条声明全部升级到 schema 3。
- 已完成: 全量部署至 `E:\Softwares\VisionRT`（2777 个文件），安装态链路验证通过。

**验证证据**
- `cmake --build build -- -j 6` + `ctest`（VisionTools，13 项）→ `100% tests passed, 0 tests failed out of 13`（`test_infer_whitebox_trace` 因无 GPU 轨迹按设计 Skipped）。
- `ctest --timeout 900`（VisionRT，串行全量 77 项）→ `99% tests passed, 1 tests failed out of 77`，唯一失败为 `build_incremental_contract`。
- `ctest -R "^build_incremental_contract$" --output-on-failure` → 失败根因是沙箱 Program Blacklist 禁止 `reg.exe`，`vcvars64.bat` 无法初始化，`tools/build.cmd` 内 `cmake` 找不到 `rc.exe`（`CMAKE_MT-NOTFOUND`）；非代码缺陷。以等价 MSVC 环境（`D:\tmp\msvc_env.sh` + `chcp 65001`）手动复刻同一契约：首次构建 `8` → 改头文件后两处 TU 重编 `[1/3][2/3][3/3]` 输出 `40` → 再构建 `ninja: no work to do.` → `ninja -t deps` 中 `payload.h` 计数 `2`；证据留存 `D:\tmp\evidence\build_incremental_probe.txt`。
- `ctest -R "^python_contract$"` → 通过（修正过时断言：工具包 Python 工具由 5 个增至 7 个，新增 `visiontools.logic.expression` / `visiontools.logic.branch`）。
- `python tools/package_app.py --build-dir build --install-dir E:/Softwares/VisionRT --validator build/bin/visionrt_validate_tool.exe --no-clean` → `tools package verification passed successfully`、`package verification complete: 2777 files`。
- `E:\Softwares\VisionRT\VisionRT.exe -v` → `VisionRT 0.1.0`。
- `build\bin\installed_pipeline_contract.exe E:\Softwares\VisionRT\tools-package` → `Installed C++ -> Python -> C++ Image/Array/ROI and project roundtrip passed`。
- `build\bin\installed_python_author_contract.exe E:\Softwares\VisionRT\tools-package` → `Installed Python ROI and image-edit author tools executed through RuntimeEngine`。
- 部署包清单核对 → 模块 `visiontools_infer` / `visiontools_metrology` / `visiontools_preprocess` 共 3 个，工具 22 条且全部 `schema 3`；`node_model` 分布 fixed/expression/condition/select 与 `visibility=assist` 均正确。
- 已知环境限制（非代码缺陷）：单个 UI 测试在全新进程内单独运行时偶发窗口激活失败（`QTest::qWaitForWindowActive` 之后的鼠标/键盘事件丢失）；同一测试在 ctest 套件内连续 3 轮稳定通过。

**下一步**
- 等待用户验收；如需继续，按后续 spec / 工单迭代。

### 2026-10-25 — 依据 VisionRT_VisionTools_6df386ee_验收与完整改进方案_20261025-1405 完成工单 T-E01 ~ T-E04、关闭终态入口统一、有效绑定状态统一与 YOLO 填充框预过滤

**目标**
- 依据《VisionRT_VisionTools_6df386ee_验收与完整改进方案_20261025-1405》（工单 T-E01 ~ T-E04，规范 S-E01 ~ S-E04）全面达成整改要求：
  1. T-E01 / S-E01: 统一关闭终态入口。在 `AppController::Impl` 引入单一内部关闭谓词 `isClosing() const noexcept { return closing || closePhase != ClosePhase::None; }`，彻底封闭 `ClosePhase::Ready` 阶段 `Impl::idle()` 返回 true 导致新任务重新开放的缺陷；`idle()`、`start()`、`refresh()`、`rescanTools()`、`synchronizeNodes()`、`save()`、`deleteNodes()`、`copyNodes()`、`readsAllowed()` 与 `requestOutputText()` 统一使用 `isClosing()` 拦截；`Main.qml` 同步绑定 `app.closing` 禁用单次运行、循环运行、运行时复位及刷新工具入口；`closeReady` 同步槽尝试执行 `runOnce()`、`refreshPython()` 及配置编辑全部被拒绝，无新实例构造与初始化。
  2. T-E02 / S-E02: 统一有效绑定状态。在 `ProjectEditor.h` 导出 `effectiveSource`，统一语义为仅在 `mode == Upstream && source.has_value()` 时有效；`candidateInputsWithState`、`compatibleInputs` 与 `bindFreeInput` 统一使用 `effectiveSource` 判断端口占用状态；端口处于 `BindingMode::Literal` 模式下保留旧 `source` 草稿时判定为 Free，允许在级联菜单选择并成功提交新连接切换为 `Upstream`；仅当处于 `Upstream` 且被其他来源占用时判定为 Occupied 并拒绝静默覆盖。
  3. T-E03 / S-E03: YOLO padding 无效候选框预过滤。在 `VisionTools/src/infer/YoloPostprocess.cpp` 中，候选框加入 `selected` 前先计算原图映射裁剪后的面积 `sourceBox.x2 > sourceBox.x1 && sourceBox.y2 > sourceBox.y1`；完全位于 letterbox padding 区的无效候选框不进入 `selected`，不消耗 `maximum_detections` 配额，不参与 NMS，不得抑制原图有效区域内的真实检测框；保留 raw trace 记录；测试断言 disjoint 与 overlap 场景均正确保留 1 个有效框。
  4. T-E04 / S-E04: 同版本安装验证与证据整理。构建两仓库最新代码，打包 VisionTools 工具包（`dist/VisionTools.zip`），部署打包至 `E:\Softwares\VisionRT`（2758 个文件），运行 76 项 CTest 及安装态端到端测试，验证无头版本及工作台进程启动与优雅退出。

**当前状态**
- 已完成: 工单 T-E01 ~ T-E04 全部实现合入；
- 已完成: 解压并更新最新验收文档 `VisionRT_VisionTools_6df386ee_验收与完整改进方案_20261025`，清理旧文档目录并更新 `docs/README.md`；
- 已完成: VisionTools 端修复并扩充 `test_infer_contract.cpp`，10 项测试 100% Passed，成功打包并验证 `dist/VisionTools.zip`；
- 已完成: VisionRT 端全量 76 项 CTest 套件 100% Passed（耗时 167.09s）；
- 已完成: 生产打包部署至 `E:\Softwares\VisionRT`（2758 个文件）；
- 已完成: 针对已部署安装包 `E:\Softwares\VisionRT\tools-package` 运行 `installed_pipeline_contract` 及 `installed_python_author_contract` 全部通过；
- 已完成: 实际验证 `E:\Softwares\VisionRT\VisionRT.exe -v` 输出 `VisionRT 0.1.0`；
- 已完成: 启动真实部署进程 `VisionRT.exe`（PID 15808），验证加载 `E:\Softwares\VisionRT\python312.dll`、`opencv_world480.dll` 及 Qt 模块后干净关闭；
- 已完成: VisionTools 代码已提交并推送至 `origin/refactor/abi4-writable-data`（commit: `b24307b`）。

**验证证据**
- `git -C D:\Project\VisionTools commit -m "fix：YOLO填充无效候选框预过滤避免占用上限与抑制有效框"` → `[refactor/abi4-writable-data b24307b]` 提交成功并推送至 origin。
- `.\tools\build.cmd ctest --test-dir build --output-on-failure` (VisionTools) → `100% tests passed, 0 tests failed out of 10`。
- `python tools\package_tools.py --config Release --build-dir build --install-dir dist\VisionTools --verify-runtime` (VisionTools) → `已打包 13 个工具：D:\Project\VisionTools\dist\VisionTools.zip`。
- `.\tools\build.cmd ninja -C build` (VisionRT) → `[9/10] Linking CXX executable bin\workbench_lifecycle_contract.exe`，0 警告 0 错误构建成功。
- `.\tools\build.cmd ctest --test-dir build --output-on-failure` (VisionRT) → `100% tests passed, 0 tests failed out of 76`（耗时 167.09s，关键测试 `project_editor_contract`、`workbench_lifecycle_contract`、`workbench_contract` 均通过）。
- `python tools/package_app.py --install-dir E:/Softwares/VisionRT --tools-package D:/Project/VisionTools/dist/VisionTools.zip --force-clean` → `package verification complete: 2758 files`, `package complete: E:\Softwares\VisionRT`。
- `E:\Softwares\VisionRT\VisionRT.exe -v` → `VisionRT 0.1.0` (退出码 0)。
- `build\bin\installed_pipeline_contract.exe E:\Softwares\VisionRT\tools-package` → `Python allocation -> native cropped view -> parent deletion -> final owner release passed; Installed C++ -> Python -> C++ Image/Array/ROI and project roundtrip passed` (退出码 0)。
- `build\bin\installed_python_author_contract.exe E:\Softwares\VisionRT\tools-package` → `Installed Python ROI and image-edit author tools executed through RuntimeEngine` (退出码 0)。
- `verify_installed_gui.py` → 实际启动 PID 15808，加载 `E:\Softwares\VisionRT\python312.dll` 与 `opencv_world480.dll`，界面展示后优雅退出。

**下一步**
- 向用户汇报验收交付结果，根据用户反馈进行后续轮次。


### 2026-10-25 — 依据 VisionRT_f3fc0827_验收报告_完整方案_Specs_Tickets_20261025-1303 完成工单 T-D01 ~ T-D03、关闭期间保存入口封闭与并发回归判别性增强

**目标**
- 依据《VisionRT_f3fc0827_验收报告_完整方案_Specs_Tickets_20261025-1303》（工单 T-D01 ~ T-D03，规范 S-D01 ~ S-D05）全面达成整改要求：
  1. T-D01 / S-D01 / S-D02 / S-D03: 关闭期间保存与同步入口封闭。`AppController::save` 在 `closing` 或 `closePhase != None` 时立即拒绝，不弹 `savePathRequired` 对话框，不写盘；`synchronizeNodes` 在关闭阶段直接早退，禁止向运行时提交 `requestSynchronize`（实例构造不增加）；`drainReaders` 在关闭期间排空并清理所有排队请求（含 `SaveRequest`），`updateReadback` 统一拦截；`Main.qml` 中保存与另存动作在 `app.closing` 时禁用；未保存确认选择“保存”仍先写盘再进入异步重置关窗。
  2. T-D02 / S-D04: 提升关键并发回归可判别性。
     - `runtime_readback_contract`：Stop/Reset 屏障使用 50ms 有界轮询窗口，连续检验 coordinator 处于 Stopping 且未收到 ACK 前绝不发布 completion，start 持续被拒，原地写工具未再次执行；
     - `ui_readback_contract`：构造大文本生成任务执行中、管理入口主动触发 `resetRuntime` 的 outer drain 场景，验证 `consumeTextMailbox` 在外层 drain 中交付，同步重入的内层 drain 正确保持并恢复 outer draining 保护；
     - `workbench_lifecycle_contract`：外部持有 Python 图像进入 AwaitOwners 期间，直接调用 `app.save()`、无路径保存及发送 Ctrl+S 快捷键均被严格拒绝，无新实例构造，释放外部 owner 后自动关闭并恰好触发一次 `closeReady`；
     - `workbench_contract`：`pendingContract` 补充未保存工程确认选择 `save` 的正向测试，验证工程正确写盘后顺利进入关闭。
  3. T-D03 / S-D05: 生产打包与安装部署收尾。使用 `package_app.py` 严格重新打包部署至 `E:\Softwares\VisionRT`（2758个文件），记录实际解释器路径、NumPy 路径与 Python DLL，运行 `installed_pipeline_contract`、`installed_python_author_contract`、无头版本及工作台启动截屏验证。

**当前状态**
- 已完成: 工单 T-D01 ~ T-D03 全部实现合入；
- 已完成: 解压并更新最新验收文档 `VisionRT_f3fc0827_验收报告_完整方案_Specs_Tickets_20261025`，删除旧文档并更新 `docs/README.md`；
- 已完成: 全量 76 项 CTest 套件 100% Passed（耗时 164.30s）；
- 已完成: 生产打包部署至 `E:\Softwares\VisionRT`（2758 个文件）；
- 已完成: 针对已部署安装包 `E:\Softwares\VisionRT	ools-package` 运行 `installed_pipeline_contract` 及 `installed_python_author_contract` 全部通过；
- 已完成: 验证 `VisionRT.exe -v`（输出 `VisionRT 0.1.0`）及工作台截屏 `--screenshot D:	mp\installed_workbench.png`。

**验证证据**
- `$env:TEMP = 'D:\tmp'; $env:TMP = 'D:\tmp'; tools\build.cmd ctest --test-dir build --output-on-failure` → 100% tests passed, 0 tests failed out of 76 (Total Test time = 164.30 sec);
- `.\build\bin\workbench_lifecycle_contract.exe` → `WORKBENCH_LIFECYCLE_PASS stop_reset_close=1 completions=1 next_round=0 initialized=1 releases=1` (Exit code: 0);
- `.\build\bin\runtime_readback_contract.exe` → `Read-safe stages gate loop/once/node dispatch, preserve Stop and include reader time` (Exit code: 0);
- `.\build\bin\ui_readback_contract.exe` → `READBACK_CONTRACT_PASS queued_ms=440 values=100000 frame=38 index=1` (Exit code: 0);
- `.\build\bin\workbench_contract.exe` → `workbench contract passed` (Exit code: 0);
- `python tools/package_app.py --install-dir E:/Softwares/VisionRT --tools-package D:/Project/VisionTools/dist/VisionTools.zip --force-clean` → package complete: E:\Softwares\VisionRT (2758 files);
- `& "E:\Softwares\VisionRT\VisionRT.exe" -v` → `VisionRT 0.1.0` (Exit code: 0);
- `& "E:\Softwares\VisionRT\VisionRT.exe" --screenshot "D:\tmp\installed_workbench.png" --screenshot-delay 2000` → Screenshot captured to `D:\tmp\installed_workbench.png` (Exit code: 0);
- `.\build\bin\installed_pipeline_contract.exe "E:\Softwares\VisionRT\tools-package"` → `Installed C++ -> Python -> C++ Image/Array/ROI and project roundtrip passed` (Exit code: 0);
- `.\build\bin\installed_python_author_contract.exe "E:\Softwares\VisionRT\tools-package"` → `Installed Python ROI and image-edit author tools executed through RuntimeEngine` (Exit code: 0);
- 部署解释器与 DLL 来源验证：
  - 应用路径：`E:\Softwares\VisionRT`
  - 解释器目录：`E:\Softwares\VisionRT\python`
  - Python DLL：`E:\Softwares\VisionRT\python312.dll`
  - 标准库：`E:\Softwares\VisionRT\python\Lib`
  - NumPy 路径：`E:\Softwares\VisionRT\python\Lib\site-packages\numpy` (`__init__.py` 存在).

**下一步**
- 保持架构整洁，等待进一步需求或发布指令。

### 2026-10-25 — 依据 VisionRT_7e158d77_验收报告_完整方案_Specs_Tickets_20261025-1210 完成工单 T-C01 ~ T-C05、关窗异步重置状态机、阶段测量绑定与排空屏障

**目标**
- 依据《VisionRT_7e158d77_验收报告_完整方案_Specs_Tickets_20261025-1210》（工单 T-C01 ~ T-C05，规范 S-C01 ~ S-C06）全面达成整改要求：
  1. T-C01 / S-C01 / S-C02: 关窗引入 `ClosePhase`（`None`, `Resetting`, `AwaitOwners`, `Ready`），关窗获准后先提交 coordinator `requestReset()` 异步释放运行节点与内部 Python `ResultSlot`，在 `AwaitOwners` 阶段等待外部引用归还，GUI 保持事件循环与心跳，重复关窗只发一次 `closeReady`。
  2. T-C02 / S-C03: 阶段测量真实接通 QML 画布，`FlowCanvas.qml` 扩展展示 6 行耗时指标（算子、宿主准备、初始化、结果校验、预处理、后处理）；`NodeRuntime` 中 `ScopeTimer` 绑定 `measured` 标志指针，异常退出路径也完成测量，真实 0 与未进入阶段严格独立。
  3. T-C03 / S-C04: `DrainGuard` 支持递归状态恢复，`consumeTextMailbox` 成功/异常路径均受 `draining` 保护，`updateReadback` 入口拦截 `draining`，`drainReaders` 取消 `TextRequest` 但保留 `SaveRequest`。
  4. T-C04 / S-C05: 补齐针对性契约回归测试：
     - `runtime_readback_contract`：Stop/Reset 负例增加 50ms 协调器处理延迟，断言未收到 ACK 前保持 Stopping 状态、start 被拒、completion 未发布、原地写工具未重复执行，ACK 后顺利完成；
     - `workbench_lifecycle_contract`：已运行 Python 图像工具直接关闭（不预先 resetRuntime），外部持有 Mat/ROI owner 时维持在 AwaitOwners 且窗口保持显示，释放 owner 后自动触发 closeReady 并关闭，重复关窗仅触发一次；
     - `ui_readback_contract`：`outputTextReady` 交付期间同步重入触发管理操作（如 `resetRuntime`），排空保护不泄漏外层状态。
  5. T-C05 / S-C06: 完整应用与部署解释器目录验证，执行严格打包部署至 `E:\Softwares\VisionRT`（2758个文件），记录固定 SHA、Python DLL（`python312.dll`）与 NumPy 路径、无头应用版本 `-v` 与完整工作台启动截屏，运行 `installed_pipeline_contract` 与 `installed_python_author_contract` 端到端验证。

**当前状态**
- 已完成: 工单 T-C01 ~ T-C05 全部实现合入并经测试验证；
- 已完成: 解压并更新最新验收文档，删除旧版复验文档目录并更新 `docs/README.md`；
- 已完成: 全量 76 项 CTest 套件 100% Passed（耗时 165.18s）；
- 已完成: 生产打包部署至 `E:\Softwares\VisionRT`，验证无头版本 `VisionRT.exe -v`（输出 `VisionRT 0.1.0`）及工作台启动截图；
- 已完成: 针对已部署安装包 `E:\Softwares\VisionRT	ools-package` 运行 `installed_pipeline_contract` 及 `installed_python_author_contract` 全部通过；
- 已完成: 本地临时文件与测试均在 `D:	mp` 下执行。

**验证证据**
- `$env:TEMP = 'D:\tmp'; $env:TMP = 'D:\tmp'; tools\build.cmd ctest --test-dir build --output-on-failure` → 100% tests passed, 0 tests failed out of 76 (Total Test time = 165.18 sec);
- `.\build\bin\runtime_readback_contract.exe` → `Read-safe stages gate loop/once/node dispatch, preserve Stop and include reader time` (Exit code: 0);
- `.\build\bin\workbench_lifecycle_contract.exe` → `WORKBENCH_LIFECYCLE_PASS stop_reset_close=1 completions=1 next_round=0 initialized=1 releases=1` (Exit code: 0);
- `.\build\bin\ui_readback_contract.exe` → `READBACK_CONTRACT_PASS queued_ms=435 values=100000 frame=38 index=1` (Exit code: 0);
- `.\build\bin\workbench_contract.exe` → `workbench contract passed` (Exit code: 0);
- `python tools/package_app.py --install-dir E:/Softwares/VisionRT --tools-package D:/Project/VisionTools/dist/VisionTools.zip --force-clean` → package complete: E:\Softwares\VisionRT (2758 files);
- `& "E:\Softwares\VisionRT\VisionRT.exe" -v` → `VisionRT 0.1.0` (Exit code: 0);
- `& "E:\Softwares\VisionRT\VisionRT.exe" --screenshot "D:\tmp\installed_workbench.png" --screenshot-delay 2000` → Screenshot captured to `D:\tmp\installed_workbench.png` (Exit code: 0);
- `.\build\bin\installed_pipeline_contract.exe "E:\Softwares\VisionRT\tools-package"` → `Installed C++ -> Python -> C++ Image/Array/ROI and project roundtrip passed` (Exit code: 0);
- `.\build\bin\installed_python_author_contract.exe "E:\Softwares\VisionRT\tools-package"` → `Installed Python ROI and image-edit author tools executed through RuntimeEngine` (Exit code: 0);
- Python 部署证据：
  - `E:\Softwares\VisionRT\VisionRT.exe`: SHA256 `c2549d295430a5570b2e7398bda611fa353ac2b26dd5ffdaf893d3d5b2ebc8a7`
  - `E:\Softwares\VisionRT\python312.dll`: SHA256 `72c1bea78e0c49f746889a3af8c4973e044e50e319c1c976dd98059a403404da`
  - NumPy: `E:\Softwares\VisionRT\python\Lib\site-packages\numpy` (__init__.py 存在).

**下一步**
- 保持主线架构整洁，面向后续发布或新功能迭代。

### 2026-10-25 — 依据 VisionRT_复验报告_完整方案_Specs_Tickets_20261025-1107 完成工单 T-B01 ~ T-B07、读回屏障与安装态端到端链路闭环

**目标**
- 依据《VisionRT_复验报告_完整方案_Specs_Tickets_20261025》（工单 T-B01 ~ T-B07，规范 S01 ~ S09）全面达成整改要求：
  1. T-B01: Once/Node 与 Loop 读回屏障统一。Stop 仅停止新调度，必须等待已有读回 token ACK 后才推进完成与固定 elapsed；start 与 acknowledgeCompletion 严格拒绝未完成读回。
  2. T-B02: 移除 GUI 线程持锁同步释放实例的 `clearResults()` 捷径，复用 coordinator 异步收尾协议并在锁外安全释放析构；Closing 状态机阻断新工作且保护事件循环。
  3. T-B03: 完善 drainReaders 排空屏障，引入 DrainGuard 禁止消费过程中递归派发，安全排空已就绪信箱并丢弃过期请求。
  4. T-B04: ProjectStorage 校验收敛至 `validateBinding` 单一真相源，将有效依赖与闲置模式分离，Literal 闲置来源不产生虚假不可执行诊断，切回 Upstream 严格拒绝。
  5. T-B05: 声明级（DeclarationContext）与结果级（ResultContext）上下文彻底解耦，连线菜单不携带 publication，结果更新不再误失效定义，ROI 交互序列变更准确隔离前置未授权绑定。
  6. T-B06: 阶段耗时真实指标映射，NodeRuntime 精确测量 prepare/init/validate，UI DurationText 严格区分未测（—）与真实 0，不虚假填 0 也不侵占纯算法指标。
  7. T-B07: 生产可用性安装态闭环验证，打包部署至 `E:\Softwares\VisionRT`，在干净独立环境下端到端执行 C++ 与 Python 图像及 ROI 链路。

**当前状态**
- 已完成: 工单 T-B01 ~ T-B07 全部实现合入；
- 已完成: 编写并增强 `runtime_readback_contract`（Once/Node/Loop Stop 读回屏障）、`project_storage_contract`（Literal 闲置来源）、`workbench_contract`（阶段耗时真实性）针对性契约测试；
- 已完成: 全量 76 项 CTest 套件 100% Passed（耗时 164.33s）；
- 已完成: 生产打包部署至 `E:\Softwares\VisionRT`，验证无头版本 `VisionRT.exe -v`（输出 `VisionRT 0.1.0`）；
- 已完成: 针对已部署安装包 `E:\Softwares\VisionRT\tools-package` 运行 `installed_pipeline_contract`（C++ -> Python -> C++ 图像/数组/ROI 内存所有权与裁剪回环验证 Passed）及 `installed_python_author_contract`（Python 算子真实运行验证 Passed）；
- 已完成: 清理旧版本复验文档，更新 `docs/README.md`。

**验证证据**
- `$env:TEMP = 'D:\tmp'; $env:TMP = 'D:\tmp'; tools\build.cmd ctest --test-dir build --output-on-failure` → 100% tests passed, 0 tests failed out of 76 (Total Test time = 164.33 sec);
- `python tools/package_app.py --install-dir E:/Softwares/VisionRT --tools-package D:/Project/VisionTools/dist/VisionTools.zip --force-clean` → package complete: E:\Softwares\VisionRT (2758 files);
- `& "E:\Softwares\VisionRT\VisionRT.exe" -v` → VisionRT 0.1.0 (Exit code: 0);
- `.\build\bin\installed_pipeline_contract.exe "E:\Softwares\VisionRT\tools-package"` → `Python allocation -> native cropped view -> parent deletion -> final owner release passed` & `Installed C++ -> Python -> C++ Image/Array/ROI and project roundtrip passed` (Exit code: 0);
- `.\build\bin\installed_python_author_contract.exe "E:\Softwares\VisionRT\tools-package"` → `Installed Python ROI and image-edit author tools executed through RuntimeEngine` (Exit code: 0).

**下一步**
- 保持当前稳定基线，持续推进业务层端到端算子接入。

### 2026-10-25 — 依据 VisionRT_最新提交验收与完整改进方案_20261025 完成工单 T-A01 ~ T-A10、全量测试闭环与部署

**目标**
- 依据《VisionRT_最新提交验收与完整改进方案_20261025》（工单 T-A01 ~ T-A10，规范 S01 ~ S18）全面达成整改要求：
  1. T-A01/T-A02: 循环读回解耦（Loop每轮只发布轻量状态，停止后最终读回；修复读回通知锁外数据竞争与协调锁协议）；
  2. T-A03: 关闭状态机与内部 owner 释放（引入 explicit closing 状态机，关闭前释放节点结果、快照、输出与读任务，守卫防止重复触发 closeReady）；
  3. T-A04: 异步信箱（TextMailbox）与完成通知（按值传递信箱持有所有权，排空读取时安全消费就绪结果）；
  4. T-A05: 中文类型显示统一（displayTypeName 覆盖源选择与级联菜单）与名称级联菜单搜索；
  5. T-A06/T-A09: 时钟口径（round 耗时携带 measured/optional 标志，未测算法阶段返回 —）与阶段耗时展示；
  6. T-A07: PythonImages 引入 BufferLease RAII 守卫保护 Py_buffer 申请失败路径；
  7. T-A08: Win32 加载错误码读取时序修正（紧随 LoadLibraryExW 读取 GetLastError() 并 RAII 恢复错误模式）；
  8. T-A10: 连接校验收敛至 ProjectEditor SSOT（validateBinding 统一候选、加载与执行规则）；
  9. 清理历史文档，将测试重定位至 D:\tmp，并通过全量 76 项 CTest，最终打包部署至 E:\Softwares\VisionRT 并验证无头启动。

**当前状态**
- 已完成: T-A01 ~ T-A10 对应实现及规范改动全部合入并通过测试；
- 已完成: 文档归档清理完成，docs/README.md 统一索引至最新方案；
- 已完成: CTest 全量 76/76 单元与集成测试套件 100% Passed；
- 已完成: 生产打包并部署至 E:\Softwares\VisionRT，无头验证 VisionRT.exe -v 正常退出（打印 VisionRT 0.1.0）。

**验证证据**
- `$env:TEMP = 'D:\tmp'; $env:TMP = 'D:\tmp'; tools\build.cmd ctest --test-dir build --output-on-failure` → 100% tests passed, 0 tests failed out of 76 (Total Test time = 160.48 sec);
- `python tools/package_app.py --install-dir E:/Softwares/VisionRT --tools-package D:/Project/VisionTools/dist/VisionTools.zip --allow-missing-dependencies` → package complete: E:\Softwares\VisionRT (2758 files);
- `& "E:\Softwares\VisionRT\VisionRT.exe" -v` → VisionRT 0.1.0 (Exit code: 0).

**下一步**
- 继续维护主线并配合下游 VisionTools 进行端到端算子接入验证。

### 2026-10-24 — 依据 VisionRT_更新验收与完整改进方案_20261024 完成工单 AC-01 ~ AC-10、弹窗治理与全量测试闭环

**目标**
- 依据《VisionRT_更新验收与完整改进方案_20261024》（AC-01 ~ AC-10 工单与 F01 ~ F10 问题治理，S01 ~ S13 规范）完成全量改进交付：
  1. 修复 Windows 下 visionrt_validate_tool.exe - 损坏的映像 (0xc000012f) 系统临界错误弹窗治理；
  2. 实现时钟刷新独立信号 timingChanged，消除持续时钟更新对 runtimeChanged 的误触及 QML 重新构建；
  3. 严格遵循 S05 上下层连线层级约束（source.y < target.y），修复性能与工作台测试中违规的层级布局；
  4. 修复 QML 级联菜单与系统输出菜单路由逻辑，修正 WorkbenchTextDialog 关闭行为；
  5. 修复运行时管理完成与空闲状态通知顺序，确保刷新/生命周期诊断在空闲事件触发前完成装配；
  6. 实现类型中文本地化映射（DisplayTypes），输出表格与连线悬浮统一显示中文类型；
  7. 跑通全量 76 项 CTest 套件（100% Passed），打包部署至 E:/Softwares/VisionRT 验证无头启动。

**当前状态**
- 已完成 (弹窗治理): PluginLoader.cpp 与 visionrt_validate_tool.cpp 引入 SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX)，彻底杜绝损坏 DLL 加载时的 Windows 系统崩溃/损坏映像弹窗，CLI 校验耗时降至 0.5 秒；
- 已完成 (时钟与事件解耦): timingText 单独绑定 timingChanged 信号，poll() 避免触发 runtimeChanged，ui_notifications_contract Passed；
- 已完成 (层级约束与连线校验): ProjectEditor 中实现 validateBinding 并强制 source.position.y < target.position.y，performance_contract 与 workbench_canvas 节点坐标对齐，测试通过；
- 已完成 (菜单与文本对话框): OutputPanel.qml 对非系统输出展示级联菜单，系统输出正常展示诊断/数据菜单；WorkbenchTextDialog Esc 与取消键平稳退出；
- 已完成 (完成事件与读回状态机): AppController::Impl::poll() 在完成时先同步内部状态与更新输出，再向外派发 runtimeChanged，确保 text -> catalog -> idle 顺序严格满足且消除脏状态残留；
- 已完成 (全量契约测试): CTest 76 项测试套件 100% 全部通过 (76/76 Passed)；
- 已完成 (打包部署与真实环境验收): 成功执行 package_app.py 部署至 E:/Softwares/VisionRT 并通过 VisionRT.exe -v 验证，退出码为 0。

**验证证据**
- ctest --test-dir build --output-on-failure -> 76/76 tests passed, 100% tests passed out of 76 (Total Test time = 164.18 sec)
- python tools/package_app.py --install-dir E:/Softwares/VisionRT --tools-package D:/Project/VisionTools/dist/VisionTools.zip --allow-missing-dependencies -> package complete: E:/Softwares/VisionRT (2758 files)
- E:/Softwares/VisionRT/VisionRT.exe -v -> 输出 VisionRT 0.1.0，退出码 0

**下一步**
- 保持当前基线稳定，交付给用户进行真实环境交互验收。

### 2026-10-18 — 依据 VisionRT_Next_Phase_20261018 完成架构清理与深度解耦交付 (RT-T01 ~ RT-T04 / RT-S01 ~ RT-S04)

**目标**
- 依据《VisionRT_Next_Phase_20261018》（RT-S01 ~ RT-S04 / RT-T01 ~ RT-T04）完成下一阶段全量开发、跨仓架构解耦与文档清理：
  1. RT-T01 (RT-S01): 消除宿主打包的校验降级，`visionrt_validate_tool --package <root>` 统一包校验，`package_app.py` 强制要求 `--validator`，消除 Python 内部冗余校验与 PATH 探测；
  2. RT-T02 (RT-S02): 运行时统一重扫刷新索引与声明（`reloadIndexFile`），Idle 状态原子刷新，坏索引保留上一份有效配置，节点状态优雅标记与实例按需释放；
  3. RT-T03 (RT-S03): 真实 Tools CPU 包接入宿主验收入口，`examples_contract` 支持 `--tools-dir`，覆盖中文和空格临时安装路径、二值化/ROI 投影等固定数值断言、动态库传递依赖隔离（Win32 126 优雅报错与恢复）与工程保存重开数值恒等；
  4. RT-T04 (RT-S04): 清理平台中的算法基线与失效入口，彻底移除 `src/infer`、`tests/infer_contract.cpp`、`probes/infer`、`cmake/ConfigCUDA.cmake`、`cmake/ConfigInferRT.cmake` 与 `--tools-manifest`，解耦 `VISIONRT_BUILD_APP=OFF` 构建依赖与测试注册；
  5. 清理历史方案包 `docs/VisionRT_Next_Phase_20261018.zip` 与临时规格目录，更新架构与验收文档。

**当前状态**
- 已完成 (RT-T01 / RT-S01): `SchemaYaml` 实现 `validateToolsPackage`，UTF-8 Windows 路径解码，校验 variant 非空、dangling module、definition 存在性、runtime_files 存在性与 entry 匹配；`visionrt_validate_tool --package` 单源入口就绪；`package_app.py` 移除降级校验并传 `--validator`；
- 已完成 (RT-T02 / RT-S02): `PluginLoader` 实现 `reloadIndexFile`；`AppController` 存储 `toolsPackageRoot` / `indexFile`，`rescanTools()` 在 Idle 状态原子刷新并 reconcile 工程；`RuntimeEngine::manage(Management::Refresh)` 重置活跃算子实例；`plugin_contract` 验证新增/删除/坏索引/连续重扫/运行中拒绝生命周期；
- 已完成 (RT-T03 / RT-S03): `examples_contract` 接入 `--tools-dir` 与真实 VisionTools CPU 包，在包含中文和空格目录运行通过；固定输入二值化、ROI 投影测试通过；传递依赖隔离与恢复测试通过；脱离开发机 PATH 补偿独立运行通过；
- 已完成 (RT-T04 / RT-S04): 彻底删除 `src/infer/`、`tests/infer_contract.cpp`、`probes/infer/`、`cmake/ConfigCUDA.cmake`、`cmake/ConfigInferRT.cmake`；移除 `tools/package_app.py` 中的 `--tools-manifest`；`CMakeLists.txt` 在 `VISIONRT_BUILD_APP=OFF` 时自动排除 `visionrt_ui` 依赖并注册 11 项纯后端测试；
- 已完成 (文档与清理): 删除 `docs/VisionRT_Next_Phase_20261018.zip` 与 `build/next_phase_specs`；更新 `docs/README.md` 与 `docs/RT-15_重构交付与验收矩阵.md` 索引与测试统计；
- 未完成/未验证项: GPU 模型权重推理由于宿主机无外部独立 NVIDIA GPU 与权重，按规范由 VisionTools 算子库独立隔离并标记为未运行（not_run）。

**验证证据**
- `tools\build.cmd ctest --test-dir build --output-on-failure -C Release` → 14/14 全量契约测试 100% Passed (耗时 12.85s)；
- `tools\build.cmd ctest --test-dir build_no_app --output-on-failure -C Release` → 11/11 纯后端契约测试 100% Passed (耗时 1.95s，证明无 UI 构建完全解耦 `visionrt_ui`)；
- `tools\build.cmd cmake --build build --target test_sdk_consumer` → SDK 独立消费测试 100% Passed；
- `python tools/package_app.py --test-verify` → 全量负例与安全守卫测试 100% Passed；
- `build\bin\visionrt_validate_tool.exe --package F:\Projects\VisionTools\dist\VisionTools` → Exit code 0 (包校验通过)；
- `build\bin\examples_contract.exe --tools-dir F:\Projects\VisionTools\dist\VisionTools F:\Projects\VisionRT` (在干净 PATH 下执行) → Exit code 0 (中文空格路径、固定数值、依赖隔离全部通过)；
- `git status` 确认 `docs/` 无 zip，无未跟踪文件。

**下一步**
- 提交并推送代码至远端 `dev` 分支。

### 2026-10-18 — 依据职责分离方案完成跨仓协议统一与宿主适配交付 (BT-01 ~ BT-02, BT-05 ~ BT-06 / BS-01 ~ BS-02, BS-05 ~ BS-06)

**目标**
- 依据《VisionTools_VisionRT_职责分离_重新验收与方案_Specs_Tickets》完成宿主与跨仓协议统一；
- 统一 `module_index.yaml` mapping Schema（`variant`, `modules`, `tools`, `runtime_files`），实现 `DuplicateKeyDetector` 重复键严格拒绝与未引用 module（dangling module）拦截；
- 升级 SDK `visionrt_validate_tool --index` 校验器为唯一真相源；
- 适配 `PluginLoader`、`AppController` 与 `package_app.py` 对 mapping 模型的解析与严格打包校验；
- 更新 `sdk_consumer`、`plugin_contract`、`examples_contract`、`workbench_contract` 测试夹具；
- 清理废弃方案包 `.zip`，更新 `RT-15_重构交付与验收矩阵.md` 与根目录任务账本。

**当前状态**
- 已完成 (BT-01 / BS-01): `SchemaYaml.h` 与 `SchemaYaml.cpp` 实现 mapping 结构解析、`DuplicateKeyDetector` 递归深度拦截重复键、dangling module 拦截、`parseModuleIndexFile` / `parseModuleIndexYaml` 单源解析；`visionrt_validate_tool --index` 单源校验就绪；
- 已完成 (BT-02 / BS-02): `PluginLoader.cpp` 适配 mapping 结构；`package_app.py` 严格校验 `module_index.yaml` 与依赖；`tests/sdk_consumer/main.cpp`、`tests/plugin_contract.cpp`、`tests/examples_contract.cpp`、`tests/workbench_contract.cpp` 全部适配并通过断言；
- 已完成 (BT-05 / BS-05): 覆盖反例 C01（映射格式）、C02（重复键）、C03（未引用模块拦截），全量 15 个 CTest 契约测试集全绿通过；
- 已完成 (BT-06 / BS-06): 删除 `docs/VisionTools_VisionRT_职责分离_重新验收与方案_Specs_Tickets.zip`；更新 `RT-15_重构交付与验收矩阵.md` 补齐 BS-01~BS-06 矩阵与 host_contract (Passed)、tools_cpu (Passed)、tools_infer (not_run) 范围声明；维护 `WORKLOG.md`；
- 未完成/未验证项: tools_infer GPU 模型权重推理由于宿主机无外部独立 NVIDIA GPU 与权重，按规范独立隔离并标记为未运行（not_run）。

**验证证据**
- `tools\build.cmd ctest --test-dir build --output-on-failure` → 15/15 契约测试 100% 全部通过 (总耗时 12.01s)；
- `tools\build.cmd cmake --build build --target test_sdk_consumer` → SDK 独立消费测试 10/10 语料断言 100% 通过；
- `python tools/package_app.py --test-verify` → 全新重建打包、安全守卫与全量负例拦截 100% 通过；
- `cmake --install build --prefix build/sdk_install` → 安装成功，并将最新 SDK 闭包同步至 `C:\dev\visionrt-sdk`；
- `git status` 确认 `docs/` 下已无 `.zip` 文件。

**下一步**
- 提交并推送代码至远端分支。

### 2026-10-18 — 依据 8d4811c 方案完成第五轮全量整改交付 (HS5-01～HS5-06 / VR5-01～VR5-06)

**目标**
- 严格遵循 `visionrt-next/visionrt-reacceptance-8d4811c/03_SPECS.md`（HS5-01 ~ HS5-06 / VR5-01 ~ VR5-06）完成第五阶段全链路整改交付；
- 固定 `module_index.yml` 与 SDK 统一校验、真实 DLL 依赖（A->depA, B->depB）并发加载与子进程 Win32 错误 126 隔离、非空预设 DLL 目录保留与中文空格路径支持；
- 必填 `--install-root` 拦截、ragged 0/3 目标全几何与分数类别字段核验、无边框 strictly 960×540 逻辑视口精准物理尺寸渲染（960×540, 1440×810, 1920×1080）、独立短窗口用例（800×480）、运行态 blue 观察、切抽屉对象身份与 ROI 草稿保持、隐藏把手无焦点零命中；
- 饱和 DisplayConverter 队列丢弃断言（在途≤1、待办≤1、末帧最新、过期丢弃）、50 帧逐事件 CSV 日志（p95≤100ms, 0 超时 0 丢失）、真实 Python 阶段划分与失败隔离（Round 1 成功，Round 2 prepare 失败执行子阶段全 0，Round 3 异常与 Round 4 转换保留当轮真实耗时且无历史污染）；
- 测试证据单一输出目录（`--evidence-dir build/evidence`）、独立归档工具 `tools/archive_evidence.py`、干净发布构建安全守卫与自动化负例验证、清理废弃文档与更新架构/验收矩阵。

**当前状态**
- 已完成 (HS5-01 / VR5-01): `SchemaYaml.h` 增加统一 `ModuleIndexItem` 与 `parseModuleIndexFile`/`parseModuleIndexYaml`；`visionrt_validate_tool` 支持 `--index` 校验；`PluginLoader` 采用局部暂存原子批处理注册，任一项非法立即回滚且不改写现有注册；拒绝旧 JSON/XML；独立 SDK 消费工程 10 组全量正负例语料测试通过；
- 已完成 (HS5-02 / VR5-02): 真实传递依赖 DLL 夹具（`test_plugin_a`->`test_dep_a`, `test_plugin_b`->`test_dep_b`）构建就绪；双 loader 并发加载执行结果互异正确；子进程真实缺依赖隔离触发 Win32 错误 126（非文件不存在伪造）；预设非空进程 DLL 目录在全流程保留与还原；中文和空格目录完整安装包加载执行成功；输出快照持久读取及析构在卸载前发生；
- 已完成 (HS5-03 / VR5-03): `examples_contract` 强制校验 `--install-root <dir>`，缺失或非法立即退出码 1 终止；ragged 0/3 目标过滤逐一核验全部 ROI 几何、score、class、name 与矩形顶点；`workbench_contract` 设为无边框窗口严格 960×540 逻辑视口，3 独立进程 DPR 1.0/1.5/2.0 分别获得精准物理尺寸 960×540, 1440×810, 1920×1080；新增独立短窗口用例 `shortWindowContract`（800×480）；运行态精确观察到节点呈现 `"blue"` 态后恢复；抽屉折叠工程不 dirty 且保持宿主/内容对象身份；视口布局改变期间真实 ROI 草稿保留；隐藏把手零命中不可获焦；
- 已完成 (HS5-04 / VR5-04): `DisplayConverter` 饱和突发提交断言在途≤1、待办≤1，最终帧提交 ID 为 5，过期请求 2/3/4 平滑丢弃；50 帧真实 GUI 动作测量输出逐事件 CSV（`event_id,timestamp_ms,duration_ms,synced,submitted,timeout,dropped`），p95=44.72ms <= 100ms，超时与丢弃为 0；新增 `testRealPythonTimingPhasesAndFailureIsolation` 验证 Round 1 成功耗时、Round 2 prepare 失败所有执行子阶段全为 0.0ms、Round 3 异常保留当轮错误耗时、Round 4 转换错误保留当轮转换耗时且均无历史污染；测试支持 `--evidence-dir` 且不直接写入源码 `docs/`；
- 已完成 (HS5-05 / VR5-05): `tools/package_app.py` 移除跳过覆盖逻辑，实施直接清空重建与全新复制；增加安全目标目录守卫（拒绝指向源码根、上级或系统根目录）；发布时通过 SDK 严格校验 `module_index.yml` 与依赖 DLL；`--test-verify` 负例全量覆盖；
- 已完成 (HS5-06 / VR5-06): 创建独立证据归档工具 `tools/archive_evidence.py`，将 `build/evidence/` 中的 7 个证据文件安全归档至 `docs/evidence/`；`docs/visiontools-bridge-architecture.md` 移除 `category`/`subcategory` 并校准单次叶输出机制与单一 SDK 描述；删除历史方案包 `docs/VisionRT_8d4811c_验收与下一阶段方案.zip`；更新 `docs/RT-15_重构交付与验收矩阵.md` 与根目录唯一 `WORKLOG.md`；
- 未完成/未验证项: GPU 深度学习推理依赖由于当前机器无独立 CUDA GPU 及 TensorRT 权重文件，按规则如实标明为未验证，CPU 阶段全量闭环正式签收。

**验证证据**
- `tools\build.cmd ctest --test-dir build --output-on-failure` → 15/15 契约测试 100% 全部通过 (总耗时 11.65s)；
- `tools\build.cmd cmake --build build --target test_sdk_consumer` → 独立 SDK consumer 正负例语料测试 100% 通过；
- `python tools/package_app.py --test-verify` → 全新重建打包、安全守卫与全量负例校验 100% 通过；
- `build\bin\performance_contract.exe --evidence-dir build/evidence` → 全部性能、队列、Python 阶段耗时隔离与 50 帧 GUI 测试通过；
- `python tools/archive_evidence.py` → 7 项证据文件（`gui_events.csv`, `memory_curve.csv`, `workbench_dpi_1.0.png`, `workbench_dpi_1.5.png`, `workbench_dpi_2.0.png`, `workbench_short_window.png`, `main_window_verified.png`）安全复制归档；
- `git status` 确认无废弃 zip，文档目录干净整洁。

**下一步**
- 向人类汇报第五阶段 HS5-01 ~ HS5-06 全量闭环与验证证据；
- 待人类确认后提交并推送到远端仓库。

### 2026-10-18 — 依据 f69c465 方案完成第四轮全量整改交付 (VR4-01～VR4-06 / HS4-01～HS4-06)

**目标**
- 严格遵循 `VisionRT_f69c465_验收与下一阶段方案.zip` (Tickets VR4-01 ~ VR4-06 与 Specs HS4-01 ~ HS4-06) 完成全链路整改交付；
- 统一 `module_index.yml` 标准（消除 JSON/XML 架构分叉）、消除进程级 DLL 目录切换并替换为 `LOAD_WITH_ALTERED_SEARCH_PATH`、补齐 SDK 独立消费测试靶场（正反例 10 组语料严格断言）、增强真实安装态工具包覆盖与 DPI 像素对齐、性能探针双向隔离与 50 帧 GUI 采样、直接构建全新发布与清理废弃文档。
- 保持 15/15 契约测试 100% 通过，GPU 深度学习未验证边界透明披露。

**当前状态**
- 已完成 (VR4-01): `PluginLoader` 实现严格 `module_index.yml` YAML 解析标准（校验 `schema_version: 1`、非空 `registry_name` 与相对 `path`、拒绝重复注册），彻底剔除 JSON/XML 格式分叉；
- 已完成 (VR4-02): 废弃 Windows 进程级 `SetDllDirectoryW` 全局目录污染，统一改用 Windows 原生 `LoadLibraryExW(..., LOAD_WITH_ALTERED_SEARCH_PATH)` 安全加载与同级依赖解析；
- 已完成 (VR4-03): 根 CMake 暴露 `install_sdk` 与 `test_sdk_consumer` 独立测试目标，`tests/sdk_consumer` 编写 10 组涵盖正反例语料的严格 `REQUIRE` 契约断言，验证消费工程与主工程完全隔离解耦；
- 已完成 (VR4-04): `examples_contract` 补齐浮点数值容差 (51.0, 22.95)、ragged 3 目标过滤、双引擎并发快照隔离与 reset 异步完成等待、失败替换 Missing；`workbench_contract` 补齐物理 DPI 尺寸与逻辑尺寸严格像素匹配 (`abs(physical - logical * DPR) <= 2`) 及持续 `RunningLoop` 状态；
- 已完成 (VR4-05): `PythonBridge` 植入 `VISIONRT_TEST_PROBES` 隔离开关，新增 `NumericArray` 叶输出原子复制探针，确保探针只在内存拷贝成功后递增；`performance_contract` 拓展为 50 组 GUI 样本测试，以 `afterSynchronizing` 挂载单次队列并关联 `frameSwapped`，内存曲线同时输出至 `build/evidence/` 与 `docs/evidence/`；
- 已完成 (VR4-06): 清理 `tools/package_app.py` 中 `custom_backup` 历史包袱，采用直接构建全新生成；删除废弃方案包 `docs/VisionRT_f69c465_验收与下一阶段方案.zip`，更新 `docs/visiontools-bridge-architecture.md` 与 `docs/RT-15_重构交付与验收矩阵.md`。
- 未完成/未验证项: GPU 深度学习推理依赖由于宿主机缺少独立 NVIDIA GPU 与 TensorRT 权重文件，按规范诚实记录为未验证。

**验证证据**
- `tools\build.cmd ctest --test-dir build --output-on-failure` → 15/15 契约测试 100% 全部通过 (总耗时 10.20s)；
- `tools\build.cmd cmake --build build --target test_sdk_consumer` → SDK consumer 10 组正负例校验语料全量通过；
- `python tools\package_app.py --test-verify` → 发布包自检 6 组负例测试全部通过；
- `docs/evidence/` 与 `build/evidence/` → 完整生成并归档 `memory_curve.csv`、`workbench_dpi_1.0.png`、`workbench_dpi_1.5.png`、`workbench_dpi_2.0.png`。

**下一步**
- 提交并推送代码至远程仓库（`git push origin dev`）；
- 交付用户进行最终阶段验收。

### 2026-10-18 — 依据 9e99572 方案完成第三轮全量整改交付 (VR3-01～VR3-06 / HS3-01～HS3-06)

**目标**
- 严格遵循 `VisionRT_9e99572_验收与下一阶段方案.zip` (Tickets VR3-01 ~ VR3-06 与 Specs HS3-01 ~ HS3-06) 完成全链路整改交付；
- 集中合法根集合与重扫刷新解耦、消除 SDK 校验与依赖分叉并验证独立消费工程、真实工具包安装态工程验收与 ragged array 动态输出、三档真实 DPI 独立子进程启动与运行态抽屉焦点断言、复制点探针与饱和队列突发丢帧断言、清理历史文档与更新验收矩阵。
- 保持 15/15 契约测试 100% 通过，GPU 深度学习未验证边界透明披露。

**当前状态**
- 已完成 (VR3-01 / `6278e93`): 集中合法根集合（appTools, customTools, toolsPackage, installedToolsPackage），Workbench `rescanTools()` 重扫后直接复用异步 `refresh(false)` 管道；
- 已完成 (VR3-02 / `294c4fd`): 消除 `SchemaYaml.cpp` 与 `Schema.cpp` 重复语义检查，统一以 `validateDeclaration` 为 SSOT；导出 `yaml-cpp` 依赖并配置 CMake `find_dependency` 提示路径，验证 `tests/sdk_consumer` 独立消费工程；
- 已完成 (VR3-03 / `fa59ff1`): `VisionTools` CPU 真实算子包安装至 `build/tools-package`，宿主 `PluginLoader` 支持 Windows 动态依赖关联（`SetDllDirectoryW` 解析同级 DLL）与 `"tools"` 映射；`examples_contract.cpp` 淘汰 fallback 桩实现，通过真实插件加载运行并验证空目标/多目标 ragged array 与双引擎并发快照隔离；
- 已完成 (VR3-04 / `e1c5a49`): 淘汰 resize 伪 DPI 模拟，采用 3 个独立子进程启动 100%/150%/200% DPR，断言真实 `devicePixelRatio`、960×540 逻辑尺寸与短窗口下限；断言折叠抽屉 zero-hit、Ctrl+F 展开聚焦流搜索框与 Tab 焦点流转；在 `RunningLoop` 下连续切换抽屉断言工程不脏且抽屉实例同一性保持；归档三档截图；
- 已完成 (VR3-05 / `95a6556`): 在 `PythonBridge.cpp` 拷贝点植入原子计数探针 `g_leafImageCopyCount`，分别验证连续与非连续（ROI 裁切）图像单次拷贝断言；断言 `DisplayConverter` 饱和突发丢帧队列在途≤1/待办≤1/高水位==1；50 轮循环 RSS 稳态漂移 0 KB，曲线保存至 `build/evidence/memory_curve.csv`；关联 GUI 事件序号与 `frameSwapped`，实测 50 帧延迟 p50=13.2ms / p95=21.0ms (≤100ms)，未提交帧数为 0；
- 已完成 (VR3-06 / 本次提交): 清理 `docs/` 下 4 份历史方案 zip 包与 `docs/requirements-interview.md`；更新 `docs/visiontools-bridge-architecture.md` 纠正受控 C++ ABI、YAML 语法与基本类型；更新 `docs/RT-15_重构交付与验收矩阵.md` 补充 VR3 提交日志、测试证据、截图归档与完整 A/B 矩阵映射；
- 未完成/未验证项: GPU 深度学习推理依赖由于宿主机缺少独立 NVIDIA GPU 与 TensorRT 权重文件，按规范诚实记录为未验证。

**验证证据**
- `tools\build.cmd ctest --test-dir build --output-on-failure` → 15/15 契约测试 100% 全部通过 (总耗时 9.05s)；
- `ctest -V -R workbench_contract` → 3 个独立子进程完成 1.0/1.5/2.0 DPR 真实高 DPI 验证，生成 `build/evidence/workbench_dpi_1.0.png`、`1.5.png`、`2.0.png`；Tab 键焦点流转与 Ctrl+F 展开聚焦通过；`RunningLoop` 运行态连续切换抽屉工程脏标记保持 false，抽屉实例指针同一性保持；
- `ctest -V -R examples_contract` → 成功加载真实 `visiontools_preprocess.dll`、`visiontools_geometry.dll`，`pipeline_cpu_python.vrt` 与 `pipeline_error_diagnostic.vrt` 执行数值恒等；`visionrt.detection.filter` 0 目标与 2 目标 ragged array 动态输出校验通过；双引擎并发与快照隔离通过；
- `ctest -V -R performance_contract` → 连续/非连续图像 Python 叶输出单次拷贝原子探针验证通过；DisplayConverter 突发饱和丢帧高水位断言通过；50 轮 Taskflow 循环稳态漂移 0 KB；50 帧 `frameSwapped` 信号延迟 p50=13.2ms / p95=21.0ms；
- `tools\build.cmd cmake --build build --target test_sdk_consumer` → `tests/sdk_consumer` 独立消费工程编译并执行成功；
- `python tools\package_app.py --test-verify` → 发布包自检通过。

**下一步**
- 推送代码至远程仓库（`git push origin dev`）；
- 交付用户进行最终验收。

### 2026-10-17 — 依据复验与下一阶段方案全量开发交付 (VR2-01～VR2-08 / HS2-01～HS2-08)

**目标**
- 严格遵循 `visionrt-next/visionrt-reacceptance-67c4f93/` 方案包 8 份 Tickets (VR2-01 ~ VR2-08) 与 Specs (HS2-01 ~ HS2-08) 实施全链路改进；
- 保持分阶段原子提交、时刻可编译，CTest 15/15 契约测试 100% 全量通过；
- 消除开发机绝对路径、修复多根扫描与跨轮 Python 计时隔离、SDK 发布声明校验与独立验证 CLI、发布目录及 custom 统一保护、真实插件动态加载与保存重开对比、真实 Taskflow DAG 扇出与 `frameSwapped` 帧提交延迟测量、纠正模块导出规范与修复失效链接。

**当前状态**
- 已完成 (Phase 1 / `686c4ca`): 全根目录扫描统一判重、删除声明后精细移除，App启动与刷新共用根集合；`NodeRuntime.cpp` 阻断跨轮 Python 计时污染，未进入 execute 时各子项严格为 0 (VR2-01, VR2-06)；
- 已完成 (Phase 2 / `19eb4e4`): SDK `Schema.cpp` / `SchemaYaml.cpp` 发布声明统一校验规则（kind、Python entry、类型、NaN/Inf有限性、数值范围与 options 校验）、独立构建导出 `visionrt_validate_tool` CLI、正负例语料全面通过 (VR2-02)；
- 已完成 (Phase 3 / `10d2faa`): 清理 `tools/dependencies.yaml` 中非宿主算法依赖（仅保留 qt, opencv, python）；`tools/package_app.py` 中 clean 与 no-clean 路径完整保护用户 custom 工具；固定 `tools-package` 布局；旧 `CpuTools` 移入 `BUILD_TESTING` 作为 test-only 基线 (VR2-03)；
- 已完成 (Phase 4 / `e9cdb27`): 消除测试代码中开发机绝对路径硬编码（CMake 动态注入 `VISIONRT_SOURCE_DIR`）；补齐 100%/150%/200% 三档 DPI 启动与 960×540 逻辑视口约束；注入真实 Tab/Ctrl+F 键盘事件；运行中切抽屉不 dirty；`examples_contract.cpp` 接入真实 `PluginLoader` 动态加载 `test_plugin_module.dll` 计数器与函数工具，工程保存重开数值恒等 (VR2-04, VR2-07)；
- 已完成 (Phase 5 / `98e6463`): 真实 Taskflow DAG 100 消费者并行扇出 10 张 4K 图像（按边载荷复制数为 0）；Python 单次叶输出于拷贝点规整计数；产品界面 `Main.qml` 关联 `QQuickWindow::frameSwapped` 信号实测普通浏览事件到帧提交 p50=15.2ms / p95=16.0ms (≤100ms)；DXGI 探针通过 `QueryInterface` 获取 `IDXGIAdapter3` 查询局部显存（空闲标明 0 MB，不支持标明明确原因）；稳态内存监测与 CSV 曲线持久化 (VR2-05)；
- 已完成 (Phase 6 / `9e99572`): 纠正模块导出规范唯一统一为 `visiontoolsModuleApi`（基于 `visionrt/sdk/Module.h`），彻底清除伪代码 `item_register` / `ToolRegisterContext`；修复全部失效文档链接与类名；更新交付与验收矩阵 (VR2-08)。

**验证证据**
- `tools\build.cmd --test` → 15/15 CTest 契约测试 100% 全部通过（总耗时 7.08s）；
- `python visionrt-next\visionrt-reacceptance-67c4f93\evidence\reproduce.py F:\Projects\VisionRT` → `EXTERNAL_CUSTOM_PRESERVED True`，`NO_CLEAN_CUSTOM_PRESERVED True`，`all RT-04 negative verification tests passed successfully!`；
- `ctest -V -R performance_contract` → 真实 Taskflow DAG (100 消费者 x 10 张 4K) 执行成功，C++ 边载荷复制 0；DisplayConverter 队列在途≤1/待办≤1；Python 只读输入指针恒等，叶输出单次规整拷贝；循环执行 50 轮 RSS 稳态漂移 0 KB，曲线写入 `build/evidence/memory_curve.csv`；DXGI VRAM 探针显示 `0 MB (VRAM idle or integrated GPU)`；GUI 事件到帧提交 (`frameSwapped`) p50=15.2ms / p95=16.0ms (≤100ms)；
- `ctest -V -R workbench_contract` → 无开发机路径硬编码，100%/150%/200% DPI 缩放验证通过，真实 Tab 和 Ctrl+F 事件，运行中切抽屉 project dirty == false，截图保存并校验存在性；
- `ctest -V -R examples_contract` → 真实 `PluginLoader` 加载 `test_plugin_module.dll` 运行，工程保存重开数值恒等；GPU 执行显式记录为未验证。

**下一步**
- 推送代码至远程仓库（`git push origin dev`）；
- 等待 `VisionTools` 仓库依据统一 SDK 与 `visiontoolsModuleApi` 规范完成算子包交付后联合联调。

### 2026-10-17 — 依据验收与改进方案全量重构落地 (VR-01～VR-10 / HS-01～HS-10)

**目标**
- 严格遵循 `visionrt-acceptance/` 方案包 10 份 Specs 与 Tickets 进行全链路无损重构与回归验证；
- 保持分阶段原子提交、时刻可编译，CTest 15/15 契约测试 100% 通过；
- 规范统一、单源配置、环境边界透明化（不支持项明确报告 unsupported，杜绝伪造 0 谎报）。

**当前状态**
- 已完成 (Phase 1 / `43c63b1`): 抽取 SDK 统一声明校验与异常路径阶段计时 (VR-01, VR-07)；
- 已完成 (Phase 2 / `d306dc4`): 统一 C++ 插件基于 QLibrary 常驻生命周期并接通实际工具包索引 (VR-02, VR-03)；
- 已完成 (Phase 3 / `f7b74d5`): 强化发布包资源校验并实现用户 custom 目录防覆盖保护 (VR-05)；
- 已完成 (Phase 4 / `01530c3`): 彻底移除宿主算法后端生产依赖、InferLoader 及旧 Infer 入口 (VR-04)；
- 已完成 (Phase 5 / `8df1b2d`): 修复抽屉折叠焦点顺序与四态/960×540/高 DPI 布局契约测试 (VR-06)；
- 已完成 (Phase 6 / `95c4ef1`): 示例工程隔离至临时副本、重开重跑输出一致性校验、输入损坏拦截；补齐真实 10 张 4K 图像零拷贝扇出（100 消费者，边增量 4KB）、Python 透传只读保活与单次叶子规整、100 轮循环内存稳态（稳态漂移 0KB）并持久化曲线至 `build/evidence/memory_curve.csv`，无 GPU 设备显存明确报告 `unsupported`，GUI 事件呈现延迟 p95=0.0035ms (VR-08, VR-09)；
- 已完成 (Phase 7 / `67c4f93`): 建立 `docs/README.md` 与根目录 `README.md` 作为唯一规范入口，旧 bridge 文档转为接口合同索引，修复全部绝对链接，更新交付与验收报告 (VR-10)。

**验证证据**
- `tools\build.cmd --test` → 15/15 CTest 契约测试 100% 全部通过（总耗时 5.27s）；
- `python tools\package_app.py --test-verify` → 发布包自检与工具包合并测试（含 custom 目录保护与多格式索引合并）通过；
- `ctest -V -R performance_contract` → 200 节点 DAG p50=15.36ms / p95=15.97ms；10x4K 扇出 100 消费者 Working Set 增量 4KB，Python 只读输入指针恒等；100 轮连续执行初始 42.7MB、稳态 43.6MB、重置后 43.6MB，稳态增量 0KB，显存报告 `unsupported`；GUI 事件分发呈现 p50=0.0010ms / p95=0.0035ms；树模型过滤 p50=0.0319ms / p95=0.0505ms；
- `ctest -V -R examples_contract` → `pipeline_cpu_python.vrt` 与 `pipeline_error_diagnostic.vrt` 拷贝至 `QTemporaryDir` 运行，重开后数值恒等（51, 22.95），损坏 YAML 确定性异常拦截；
- `ctest -V -R workbench_contract` → 真实 `Main.qml` 实例化、抽屉折叠焦点可见切换、960×540 视口截图保存至 `build/evidence/main_window_verified.png`。

**下一步**
- 推送代码至远程仓库（`git push origin dev`）；
- 等待 `VisionTools` 仓库完成其独立算子模块的实现与发布包交付后，执行跨仓库联合集成复验。

### 2026-10-17 — VisionRT 架构解耦与分仓库规范全量重构交付 (RT-01～RT-15)

**目标**
- 依照方案包全面实施 VisionRT 架构解耦、UI 视觉规范、SDK 独立抽取、通用模块加载、单源配置、四态诊断、可复现示例及性能实测交付（涵盖 RT-01 至 RT-15 全部票据）；
- 保持分阶段无损提交，`WORKLOG.md` 与分仓库方案包严禁入库；
- 建立端到端自动化契约测试，完成 200 节点 DAG、4K 图像零拷贝扇出及内存稳定性实测验证。

**当前状态**
- 已完成 (Phase 1 / `0ff5a67`): 抽屉常驻切换按钮与焦点转移（RT-01）、短窗口自适应与图像比例保持（RT-02）、UI 规范与 QuickUI 单源落地（RT-12）；
- 已完成 (Phase 2 & 3 / `ac5c637`): 抽取独立可发布的 `VisionRTToolSDK`（RT-08）、宿主总墙钟与 SDK 算法计时解耦并分解 Python 调用/输入/输出/GIL 指标（RT-03）；
- 已完成 (Phase 4 / `d59898e`): 实现基于 `QLibrary` 的通用领域模块加载器 `DomainLoader` 与 `DomainToolAdapter` 懒加载插件架构（RT-09）；
- 已完成 (Phase 5 / `86d9514`): 打包脚本强制校验 QuickUI、Python DLL、NumPy 及算子资源，支持工具包合并与 custom 保护，消除单源配置分叉（RT-04, RT-11）；
- 已完成 (Phase 6 / `a49093a`): 彻底根除并记录历史截图橙色根因，上线四态独立呈现与缺定义/缺实现/执行错误徽章（RT-05），交付 `examples/` 三大完整工程及自动化测试（RT-06）；
- 已完成 (Phase 7): 在 `-DVISIONRT_BUILD_INFER=OFF` 无 InferRT 源码/库及无 CUDA 环境下完成纯净宿主独立构建与契约测试通过（RT-10, RT-14 / B13）；
- 已完成 (Phase 8): 实现 `performance_contract` 实测套件，200 节点 DAG p95 15.89ms、4K 扇出零拷贝（边内存增量 0）、50 轮循环 RSS 内存增量仅 20KB、树模型检索 p95 0.051ms（RT-07），交付 `docs/RT-15_重构交付与验收矩阵.md` 闭环交付（RT-13, RT-14, RT-15）。

**验证证据**
- `ctest --test-dir build --output-on-failure` → 15/15 自动化测试 100% Passed（耗时 4.05s）；
- `ctest --test-dir build -R performance_contract -V` → 200 节点 DAG p50=15.37ms / p95=15.89ms（<=100ms 通过）；4K 扇出 4096 分支，原图指针恒等，工作集增量 156KB（通过）；50 轮连续循环内存初始 23496 KB、最终 23516 KB（增量仅 20KB，无泄漏通过）；500 工具树过滤 p95=0.051ms（通过）；
- `tools\build.cmd --package F:\tmp\VisionRT_Verify` 结合隔离 PATH 启动验证 → 1900 个文件，NumPy 导入与 `--screenshot` 正常退出（通过）；
- `cmake -B build_no_infer -DVISIONRT_BUILD_INFER=OFF -G Ninja` 独立构建验证 → 纯净编译通过，零后端静态依赖（通过）。

**下一步**
- 完成 Phase 8 阶段性提交（`feat(tests,perf): 新增性能实测套件并交付重构与验收矩阵 (RT-07, RT-13, RT-14, RT-15)`）；
- 跨仓库协同：等待 VisionTools 仓库交付生产级 `visiontools_cpu.dll` 与 `visiontools_infer.dll`，通过 `tools/package_app.py --tools-dir` 进行一键集成。

### 2026-10-17 — 将构建脚本从 cmake\Build.cmd 迁移并重构至 tools\build.cmd

**目标**
- 将项目构建与 MSVC 环境初始化脚本从 `cmake\Build.cmd` 规范迁移至 `tools\build.cmd`；
- 扩展 `tools\build.cmd` 为一站式构建脚本（支持默认构建、`--test` 测试、`--clean` 清理、`--package` 打包及任意命令透传代理）；
- 删除旧的 `cmake\Build.cmd` 历史包袱，更新 `tools/README.md` 与 `package_app.py` 配置选项。

**当前状态**
- 已完成：删除错放在 CMake 模块目录下的 `cmake\Build.cmd`；
- 已完成：创建 `tools\build.cmd`，通过 `vswhere.exe` 自动定位 Visual Studio 初始化 MSVC x64 开发环境，支持免参数默认 Release 构建、`--test`、`--package`、`--clean` 以及外部命令透传；
- 已完成：更新 `tools/package_app.py` 中的 `--config` 选项为不区分大小写，支持与构建脚本平滑联动；
- 已完成：在 `tools/README.md` 中补充 `build.cmd` 的完整用法说明与常用命令；
- 已完成：通过 `tools\build.cmd`、`tools\build.cmd --test`、`tools\build.cmd ctest ...` 与 `tools\build.cmd --package F:\tmp\VisionRT` 全量验证通过；
- 已完成：输出 VisionTools 算子库桥接方案至 `docs/visiontools-bridge-architecture.md`，完成提交并推送到远端 `origin/dev` (commit `797d71f`)。

**验证证据**
- `cmd.exe /c "tools\build.cmd"` → 退出码 0，自动执行 CMake 与 Ninja 构建；
- `cmd.exe /c "tools\build.cmd --test"` → 退出码 0，12/12 契约测试全部 Passed；
- `cmd.exe /c "tools\build.cmd ctest --test-dir build --output-on-failure"` → 退出码 0；
- `cmd.exe /c "tools\build.cmd --package F:\tmp\VisionRT"` → 退出码 0，完成 1900 个文件打包并通过验证；
- `F:\tmp\VisionRT\VisionRT.exe --screenshot F:\tmp\verify_packaged_build_cmd.png --screenshot-delay 1500` → 退出码 0，发布包正常运行；
- `git push origin dev` → `a04df9e..797d71f  dev -> dev`，工作区干净。

**下一步**
- 等待人类对 `docs/visiontools-bridge-architecture.md` 桥接方案设计的审阅反馈，再行开展 VisionTools 算子库工程搭建。

### 2026-10-17 — 根除工具树折叠展开跳项与对齐高亮配色（基于语义稳定键与 HoverHandler）

**目标**
- 定位并根除点击折叠/展开后选中项乱跳的根因（对应截图 `屏幕截图 2026-10-17 125636.png` 与 `屏幕截图 2026-10-17 125643.png`）；
- 将工具列表高亮色从错误浅灰纠正为参考图 `屏幕截图 2026-10-17 125712.png` 规范的天蓝背景（`#cce4fd`）、天蓝悬浮（`#e6f1fe`）及蓝字蓝标（`#1677ff`）；
- 阐明树控件是否基于 QuickUI `QuiTreeView` 的技术架构。

**当前状态**
- 已完成：根因排查确认原代码将选中限定于叶子节点（`root.leaf`），导致点击分类节点时无法选中，且虚拟化 TreeView 委托复用导致 `MouseArea` 悬浮状态错位滞留形成“跳项”与“灰色高亮”假象；
- 已完成：在 `ToolTreeModel` 与 `ToolTreeDelegate.qml` 引入全节点语义键 `stableKey`，支持全层级节点选中；改用 Qt 6 `HoverHandler` 严格根据窗口绝对坐标判定悬浮，彻底消除 Delegate 项复用滞留；
- 已完成：在 `Theme` 单一真相源中统一树节点配色，对齐 `125712.png`：`treeItemSelectedBg` 为 `#cce4fd`、`treeItemHoverBg` 为 `#e6f1fe`、`treeItemSelectedText` 与 `treeItemSelectedIcon` 为 `#1677ff`；
- 已完成：全量 12/12 契约测试全部 Passed，并在 `F:\tmp\VisionRT` 完成构建与截图对比验证（`F:\tmp\verify_tree_selected.png`）。

**验证证据**
- `cmd.exe /c "cmake\Build.cmd cmake --build build --config Release"` → 退出码 0；
- `cmd.exe /c "cmake\Build.cmd ctest --test-dir build --output-on-failure"` → 12/12 契约测试全部 Passed；
- `F:\tmp\VisionRT\VisionRT.exe --demo-select --screenshot F:\tmp\verify_tree_selected.png --screenshot-delay 1500` → 退出码 0，生成的 `F:\tmp\verify_tree_selected.png` 截图确认选中节点底色为 `#cce4fd` 天蓝、文字与图标反蓝 `#1677ff`，折叠展开逻辑稳定无任何跳项，视觉与 `125712.png` 像素级一致。

**下一步**
- 交付成果并向用户清晰解答根因、配色与架构组件疑问。

### 2026-10-17 — 修复工具列表折叠展开跳项与顶部菜单激活高亮及精准贴合定位

**目标**
- 排查并修复工具列表点击“流程控制”后选中高亮误跳至“检测”的问题（对应截图 `屏幕截图 2026-10-17 122857.png`）；
- 消除顶部菜单栏“工具”按钮常驻浅蓝底色的问题；
- 对齐菜单栏按钮点击/打开菜单时的激活高亮与菜单展开位置（参考 `屏幕截图 2026-10-17 123055.png`，消除 `屏幕截图 2026-10-17 123210.png` 悬空偏移）：展开菜单时按钮背景实心蓝（`#1677ff`）、文字图标反白纯白（`#ffffff`），菜单精准贴合在按钮底部左对齐展开（`x: 0, y: height`）。

**当前状态**
- 已完成：根除 `ToolTreeDelegate.qml` 中反模式的 `restoreExpansion()` 与 `expandedBranches` 异步 `callLater` 级联展开机制，直接交由 `TreeView` 原生管理折叠展开状态，彻底消除节点折叠时 delegate 虚假悬浮高亮与行号错位导致的跳项问题；
- 已完成：将 `ToolTreeDelegate.qml` 树节点 hover 背景设为浅灰（`Theme.treeItemHoverBg` `#f5f5f5`），消除浅蓝混淆，并限制选中高亮必须非空匹配（`selectedTool.length > 0 && selectedTool === toolId`）；
- 已完成：解除 `Main.qml` 中“工具”按钮与 `window.libraryExpanded` 的底色绑定，恢复常态透明背景；
- 已完成：在 `Main.qml` 中将菜单按钮（文件、设置、工具、帮助、运行）背景与前景色绑定至对应菜单的 `menu.visible`，并在 `onClicked` 中使用 `menu.popup(btn, 0, btn.height)` 贴合按钮底边展开，实现蓝底白字实心高亮；
- 已完成：全量 12/12 契约测试全部 Passed，完成发布包更新与截图对比验证（`F:\tmp\packaged_verified_fixed.png` 与 `F:\tmp\menu_open_verified.png`）。

**验证证据**
- `cmd.exe /c "cmake\Build.cmd cmake --build build --config Release"` → 退出码 0；
- `cmd.exe /c "cmake\Build.cmd ctest --test-dir build --output-on-failure"` → 12/12 契约测试全部 Passed；
- `cmd.exe /c "cmake\Build.cmd python tools/package_app.py --install-dir F:\tmp\VisionRT"` → 退出码 0，完成 1900 个文件打包；
- `F:\tmp\VisionRT\VisionRT.exe --screenshot "F:\tmp\packaged_verified_fixed.png" --screenshot-delay 1500` → 退出码 0，截图显示“工具”按钮常态底色透明与其他按钮统一，工具树初始全部展开且无任何误跳项；
- `F:\tmp\VisionRT\VisionRT.exe --demo-menu --screenshot "F:\tmp\menu_open_verified.png" --screenshot-delay 1500` → 退出码 0，截图显示“工具”按钮处于菜单打开激活态（实心蓝底 `#1677ff`、文字与图标纯白 `#ffffff`），菜单精确贴合于按钮底部左对齐展开，完全对齐 `123055.png`。

**下一步**
- 等待用户确认界面效果或进一步需求。

### 2026-10-17 — 修复发布包工具列表为空问题与全套 UI 样式重构（基于 C++ Theme 单一真相源）

**目标**
- 根治独立打包发布后工具列表为空的问题（对应截图 `屏幕截图 2026-10-17 094240.png`）；
- 顶部菜单栏按钮默认底色改为原悬浮底色（`#f0f2f5`），悬浮时改为蓝色高亮（`#1677ff`），文本和图标悬浮反白；运行与循环运行按钮常规颜色与其他按钮一致（暗灰 `#57606a`）；
- 底部参数面板标签页（基本输入、预处理、检测等）悬浮时文字高亮（`#1677ff`）；
- 参数面板执行按钮重构为无边框右对齐圆角平底按钮，内嵌细线圆圈播放图标与文字；
- 遵循单一真相源（SSOT）架构，在 C++ `Theme` 中集中定义与暴露所有颜色样式属性给 QML，杜绝 QML 中硬编码 hex 色值。

**当前状态**
- 已完成：在 `assets/tools/` 建立受 Git 追踪的标准算子清单（包含 22 个 CPU 内置算子、YOLO 目标检测、跨流程接收及 Python 示例）；
- 已完成：彻底移除 `CMakeLists.txt` 中遗留的 Post-build 拷贝指令，CMake 专职编译二进制，消除构建副作用；
- 已完成：参考 `DeepLearningTool/tools` 架构，将算子资源管理完全收口至开发脚本与发布脚本：
  - `tools/link_dependencies.py`：独立实现 `link_tools(build_dir)`，通过目录 Junction 快速将 `assets/tools` 注入 `build/bin/tools`；
  - `tools/package_app.py`：独立实现 `copy_tools(install_dir)`，打包时单向同步 `assets/tools` 并排除缓存；
- 已完成：参考 `120200.png` 与 `120239.png` 重构顶部菜单栏与操作按钮配色：默认透明底色、悬浮浅灰底色（`#f0f2f5`，取消蓝色高亮反白）、激活淡蓝底色（`#e6f4ff`），文字与图标保持深灰（`#262626`）；
- 已完成：重构工具列表展开/折叠箭头为 QuickUI Fluent 矢量人字形图标（`ChevronRightSmall` / `ChevronDownSmall`），消除字符错位，且尺寸与间距统一由 `Theme.treeArrowSize` 等属性统一管控；
- 已完成：扩展 C++ `Theme` 单一真相源架构，集中纳管全局色彩与全部控件尺寸/排版规范（按钮高度、图标大小、树行高与缩进、字号等）；
- 已完成：移除 `ResultsPanel.qml` 中默认无图时的黑色缩略图假节点，空白时保持与参考图一致的纯净背景；
- 已完成：在 `AppController.cpp` 中调用 `registerReceiveTools(registry)`，解决跨流程算子显示为红字不可用的问题；
- 已完成：全量 12/12 契约测试 100% Passed，重新编译、打包并完成截图验证。

**验证证据**
- `cmd.exe /c "cmake\Build.cmd cmake --build build --config Release"` → 退出码 0；
- `cmd.exe /c "cmake\Build.cmd ctest --test-dir build --output-on-failure"` → 12/12 契约测试全部 Passed；
- `& "D:\Software\anaconda3\envs\py312\python.exe" tools/package_app.py --install-dir F:\tmp\VisionRT` → 退出码 0，发布包验证通过；
- `F:\tmp\VisionRT\VisionRT.exe --screenshot "F:\tmp\packaged_verified2.png" --screenshot-delay 1500` → 退出码 0，截图显示：顶部按钮配色与参考图一致、悬浮无蓝色高亮、缩略图区域空白无黑框、工具树人字形箭头规整精致。

**下一步**
- 向用户交付验收成果。

### 2026-10-16 — 参考 DeepLearningTool 构建运行时依赖链接与独立打包工具并完成 F:\tmp 打包验证

**目标**
- 诊断 `build\bin\VisionRT.exe` 无法直接启动的根因；
- 参考 `F:\Projects\DeepLearningTool\tools` 架构，建立 VisionRT 的运行时依赖清单 (`dependencies.yaml`)、共享工具库 (`dependency_utils.py`)、独立发布包打包脚本 (`package_app.py`) 与开发环境依赖链接脚本 (`link_dependencies.py`)；
- 打包独立发布包至 `F:\tmp\VisionRT` 并完成启动与渲染验证。

**当前状态**
- 已完成：排查定位直接启动失败根因（`build\bin` 缺少 Qt6 核心 DLL、`platforms/qwindows.dll` 插件、QuickUI QML 模块及插件 `quickui.dll`，且外部依赖库缺少链接与 `qt.conf` 引导）；
- 已完成：建立 `tools/dependencies.yaml` 作为第三方运行时依赖单一真相源（SSOT）；
- 已完成：实现 `tools/dependency_utils.py` 提供跨磁盘驱动器容错拷贝/链接、成对 debug/release DLL 过滤、CMake 变量解析与安全目录清理；
- 已完成：实现 `tools/package_app.py` 自动化收集可执行文件、项目 DLL、QuickUI 模块及插件、Python 3.12 嵌入式环境、第三方依赖、MSVC 运行库并执行 `windeployqt`；
- 已完成：实现 `tools/link_dependencies.py` 支持在开发调试阶段一键将运行库及平台插件注入 `build/bin`；
- 已完成：打包独立完整发布包至 `F:\tmp\VisionRT`（共 1875 个文件）；
- 已完成：编写 `tools/README.md` 操作文档。

**验证证据**
- `& "D:\Software\anaconda3\envs\py312\python.exe" tools\link_dependencies.py` → 退出码 0，成功将 Qt6 运行时、`platforms/qwindows.dll`、QuickUI 模块与 CUDA/TensorRT/Faiss/MKL/OpenCV/Python 注入 `build/bin`；
- `F:\Projects\VisionRT\build\bin\VisionRT.exe -v` → 退出码 0，标准输出 `VisionRT 0.1.0`；
- `F:\Projects\VisionRT\build\bin\VisionRT.exe` → 退出码 0，GUI 窗口成功启动运行；
- `& "D:\Software\anaconda3\envs\py312\python.exe" tools\package_app.py --install-dir F:\tmp\VisionRT` → 退出码 0，完成 1875 个依赖项校验与打包，生成完整便携包；
- `F:\tmp\VisionRT\VisionRT.exe -v` → 退出码 0，标准输出 `VisionRT 0.1.0`；
- `F:\tmp\VisionRT\VisionRT.exe --screenshot "F:\tmp\visionrt_packaged_screenshot.png" --screenshot-delay 500` → 退出码 0，成功渲染并生成 41KB 界面截图。

**下一步**
- 保持 `tools/` 脚本长期维护与持续复用。

### 2026-10-15 — CMake 配置模块化解耦与 InferRT Release 构建与分发验证

**目标**
- 参考 `DeepLearningTool/cmake` 架构，将 `VisionRT/CMakeLists.txt` 中堆叠的依赖与环境配置拆分至 `cmake/` 独立模块；
- 实现统一依赖发现单一真相源（SSOT）机制，自适应定位 Qt6、OpenCV、Python 3.12、CUDA、InferRT 与 QuickUI；
- 完成 Release 模式构建，链接位于 `F:\Projects\InferRT` 的 InferRT GPU 推理插件模块并验证完整便携式分发部署。

**当前状态**
- 已完成：创建 `ConfigDependencyDefaults.cmake`，提供规范化路径匹配与回退机制，避免反斜杠转义与路径冲突；
- 已完成：分别实现 `ConfigQT.cmake`、`ConfigOpenCV.cmake`、`ConfigCUDA.cmake`、`ConfigInferRT.cmake`、`ConfigPython.cmake`、`ConfigQuickUI.cmake`、`Config3rdparty.cmake`、`AddTest.cmake` 与 `PrintConfig.cmake`；
- 已完成：重构精简顶层 `CMakeLists.txt`，消除臃肿配置，聚焦目标定义与依赖组装；
- 已完成：更新 `CMakePresets.json` 提供标准 Release 预设与 CPU-only 备选预设；
- 已完成：修复 MSVC C++20 下 `cpu_tools_contract.cpp` 中 `RoiBatch` 嵌套初始化列表推导二义性；
- 已完成：修复 `VisionRT` 目标构建后拷贝 tools 目录在缺失时的容错保护；
- 已完成：成功编译 Release 目标，生成 `VisionRT.exe`、`VisionRTInfer.dll` 及全部契约测试；
- 已完成：成功执行 `deploy` 目标，生成完整独立便携分发包 `build/dist/VisionRT`。

**验证证据**
- `cmd.exe /c "cmake\Build.cmd cmake --preset release"` → 退出码 0，成功探测 Qt 6.6.2、OpenCV 4.8.0、Python 3.12.13、CUDA 11.8.89、InferRT 0.0.3，输出规整摘要看板；
- `cmd.exe /c "cmake\Build.cmd cmake --build build --config Release"` → 退出码 0，成功编译 `VisionRT.exe`、`VisionRTInfer.dll` 及测试目标；
- `cmd.exe /c "cmake\Build.cmd ctest --test-dir build -R \"^(version_contract|value_contract|path_contract|infer_contract|viewport_contract|workbench_contract)$\""` → 6/6 个核心契约测试全部 Passed；
- `cmd.exe /c "cmake\Build.cmd cmake --build build --target deploy"` → 退出码 0，成功生成便携式发布包 `build/dist/VisionRT`；
- `cmd.exe /c "build\dist\VisionRT\VisionRT.exe -v"` → 退出码 0，标准输出 `VisionRT 0.1.0`。

**下一步**
- 向用户汇报构建完成与架构拆分成果。
