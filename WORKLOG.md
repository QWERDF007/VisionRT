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
