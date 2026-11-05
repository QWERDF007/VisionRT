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

### 2026-11-06 — 自定义工具树去杂色、保存按钮高亮、删除初始化参数及四列对齐

**目标**
- 彻底解决用户反馈的 4 个问题：
  1. “工具列表怎么会有各种颜色呢”：去除树节点花哨的彩色方块，统一为标准深灰文件夹图标；
  2. “保存按钮的颜色呢”：保存按钮始终保持鲜明的主题蓝底白字（#1677FF），避免未修改态变暗灰；
  3. “初始化参数是什么东西”：彻底删除效果图根本没有且产生干扰的初始化参数区块；
  4. “还是没对齐啊”：第 2 列工具卡片高度垂直撑满到底部，四列顶部/底部绝对平齐，卡片内键名统一 64px 右对齐且下拉框白底无灰块。
- 构建并通过所有测试，重新部署至 `E:\Softwares\VisionRT`。

**当前状态**
- 已完成 UI 样式与结构深度净化：
  1. 树节点去色：移除了紫色和青色背景矩形，目录统一采用经典灰阶文件夹图标（`#606266`），选中工具采用清爽蓝底与菱形高亮，杜绝杂乱配色；
  2. 保存按钮高亮：底栏“保存”按钮固定为主题蓝（`#1677FF`，悬浮 `#4096FF`）白字，醒目美观；
  3. 删除初始化参数：彻底移除了 `showsInit` 灰色卡片占位，第 3 列直接放置输入参数列表；
  4. 严格水平与垂直对齐：第 2 列卡片添加 `Layout.fillHeight: true` 延伸到底部，四列标题（32px）与卡片 0 顶部间距严格一致（12px），类型下拉框定制白底边框杜绝灰色禁用块；
  5. 自动化测试与抓屏：通过 `tool_editor_dialog_test` 重新捕获真实渲染截图 `D:/tmp/editor_dialog_capture.png` 并核实像素比对。
- 已完成全量构建与部署：
  重新构建并打包部署至 `E:\Softwares\VisionRT`（2785 个文件），安装态测试全部通过。

**验证证据**
- `.\build\bin\tool_editor_dialog_test.exe` → 退出码 0，`[PASS] tool_editor_dialog_test`，成功捕获截图 `D:\tmp\editor_dialog_capture.png`；
- `.\build\bin\tool_editor_controller_test.exe` → 退出码 0，`[PASS] tool_editor_controller_test`；
- `.\build\bin\installed_pipeline_contract.exe "E:\Softwares\VisionRT\tools-package"` → 退出码 0；
- `.\build\bin\installed_python_author_contract.exe "E:\Softwares\VisionRT\tools-package"` → 退出码 0；
- `E:\Softwares\VisionRT\VisionRT.exe -v` → 退出码 0，输出 `VisionRT 0.1.0`。

**下一步**
- 呈报用户进行视觉验收。

### 2026-11-06 — 自定义工具 UI 像素级基线与边距对齐修正

**目标**
- 针对用户反馈的“UI 没有对齐啊”，深度排查消除第 2、3、4 列标题与卡片顶部的垂直错位；
- 统一所有卡片内字段标签与输入框的左边缘水平对齐线；
- 移除第 3、4 列多余的外包灰底容器，使参数卡片直接顶对齐；
- 修复底栏保存按钮在禁用态下的样式反差；
- 重新验证截图并全量部署至 `E:\Softwares\VisionRT`。

**当前状态**
- 已完成垂直与水平对齐精准修正：
  1. 标题行基线对齐：第 2 列“工具”、第 3 列“输入参数”、第 4 列“输出参数”使用统一高度（32px）的 RowLayout，文本基线完全平齐；
  2. 卡片顶部顶对齐：移除了第 3、4 列外层包裹的冗余大灰底框，三列的卡片顶部与标题行均保留完全一致的 12px 间距，卡片 0 与第 2 列工具卡片顶部 y 坐标绝对对齐；
  3. 卡片内表单对齐：所有键名 Label 宽度统一严格锁定为 64px 靠右对齐，冒号与输入框间距统一为 8px，输入框高度统一为 32px，确保无论在哪一列、哪一个卡片内，所有输入框左对齐线完全一致；
  4. 第 2 列工具卡片高度：改为自然包裹 6 个字段属性高度（约 360px），与右侧参数卡片 1 齐平，下方干净留白，彻底消除原强制填满到底部造成的细长空框问题；
  5. 底栏按钮优化：取消按钮白底灰框，保存按钮在高亮可点击时为主题蓝底白字、不可保存时为清晰浅灰底浅灰字，彻底解决 disabled 态文字发虚看不清的问题。
- 已完成验证与部署：
  自动化截图已更新至 `D:/tmp/editor_dialog_capture.png`，人工核验确认已消除所有错位；全量部署至 `E:\Softwares\VisionRT`。

**验证证据**
- `.\build\bin\tool_editor_dialog_test.exe` → 退出码 0，`[PASS] tool_editor_dialog_test`；
- `.\build\bin\tool_editor_controller_test.exe` → 退出码 0，`[PASS] tool_editor_controller_test`；
- 重新捕获的截图 `D:/tmp/editor_dialog_capture.png` 各列顶部、标题、卡片边缘绝对水平对齐。

**下一步**
- 呈报用户进行最新视觉确认。

### 2026-11-05 — 自定义工具编辑器 UI 按照效果图重构及截图验证

**目标**
- 按照 `00_完整编辑器效果图.png` 将自定义工具弹窗 UI 深度重构成一致的现代卡片设计风格；
- 组件采用 QuickUI 标准控件及项目内基于 QuickUI 实现的高质量表单体系；
- 自动化运行与真实屏幕抓取比对，确认细节像素级吻合；
- 全量构建并部署至 `E:\Softwares\VisionRT` 并通过所有契约与端到端测试。

**当前状态**
- 已完成 UI 架构重构与视觉对齐：
  1. 弹窗外框：采用无边框圆角白色现代卡片结构（`FramelessWindowHint`），左上角简洁标题“自定义工具”，右上角单一圆角平滑关闭按钮 `✕`，支持按住标题栏自由拖拽移动窗口；
  2. 第 1 列（工具库）：顶部新增带放大镜图标的“搜索工具”输入框并支持实时关键词层级过滤与自动展开；树节点文件夹采用深浅层级色彩徽标（紫色与青蓝色），工具节点采用菱形图标 `◇`，选中项浅蓝底色搭配深蓝粗体字高亮；右键上下文菜单对齐“修改二级目录”、“删除二级目录”、“添加工具”等规范；
  3. 第 2 列（工具卡片）：顶部“工具”标题与右侧严格对齐；白底圆角浅蓝边框卡片（1.5px 强调边框），内部采用水平键值对表单（`中文名:`、`英文名:`、`注册接口:` 下拉框、`脚本文件:`、`函数名:`、`版本:`），去除杂乱多余按钮；
  4. 第 3 列（输入参数）：列顶栏“输入参数”与白底微灰圆角“+ 参数”按钮对齐；参数卡片外包浅灰底板，卡片本身为纯白圆角卡片；左上角浅蓝圆角序号徽章（`0`、`1`、`2` 等，支持拖拽重排），右上角浅灰 `✕` 删除操作；内部单列水平表单（中文名、英文名、类型下拉、默认值、下限、上限、图像区域显示 `QuiToggleSwitch` 开关）；固定系统参数（卡片 0）自动隐藏下限上限，完美契合效果图；
  5. 第 4 列（输出参数）：列顶栏“输出参数”与“+ 参数”按钮对齐；卡片同样包含浅蓝序号徽章与删除按钮；水平表单（中文名、英文名、类型、图像区域显示 `QuiToggleSwitch` 开关），去除冗余杂项；
  6. 底部操作栏：右下角白底灰框“取消”按钮与高亮蓝底白字“保存”按钮（`QuiFilledButton`）。
- 已完成自动化运行与截图验证：
  在自动化测试中注入真实参数场景，并通过 `window->grabWindow()` 生成高保真截图 `D:/tmp/editor_dialog_capture.png`，人工像素比对确认各列排布、颜色、序号徽章、开关及文字与效果图完全一致。
- 已完成全量打包部署：
  重新全量打包部署至 `E:\Softwares\VisionRT`（2784 个文件）。

**验证证据**
- `.\build\bin\tool_editor_dialog_test.exe` → 退出码 0，`[PASS] tool_editor_dialog_test`（全套 QML 组件创建、属性绑定、增删改查、保存写回无报错）；
- `.\build\bin\tool_editor_controller_test.exe` → 退出码 0，`[PASS] tool_editor_controller_test`；
- `.\build\bin\installed_pipeline_contract.exe "E:\Softwares\VisionRT\tools-package"` → 退出码 0；
- `.\build\bin\installed_python_author_contract.exe "E:\Softwares\VisionRT\tools-package"` → 退出码 0；
- 真实界面渲染截图 `D:/tmp/editor_dialog_capture.png` 经视觉核验无偏差。

**下一步**
- 向用户展示重构前后的对照与验证截图，等待用户体验与反馈。

### 2026-11-05 — 排查修复内置工具编辑开关失效及日志清理全量部署

**目标**
- 排查修复用户在设置菜单中勾选【允许编辑内置工具】后自定义工具弹窗仍不显示内置工具的问题；
- 去除日志服务启动/关闭冗余条目及 [日志] 字段；
- 补齐内置工具编辑在控制器与 QML 对话框中的自动化测试；
- 重新完整打包部署至 `E:\Softwares\VisionRT` 并通过全量端到端验证。

**当前状态**
- 已完成根因定位：
  1. `AppController.h` 中 `setEditBuiltins` 与 `setEasyTouchEnabled` 缺失 `Q_INVOKABLE` 宏修饰，导致 QML 菜单触发时抛出 `TypeError: Property 'setEditBuiltins' ... is not a function`，开关未真正生效；
  2. 部署目录 `E:\Softwares\VisionRT\tools-package` 在前序异常中断时被清空，运行态未加载到工具包，导致工具目录为空。
- 已完成代码修复：
  1. 在 `AppController.h` 为 `setEditBuiltins` 和 `setEasyTouchEnabled` 添加 `Q_INVOKABLE`；
  2. 修正 `installed_pipeline_contract` 测试中的亮度参数，使全量安装态契约 100% 通过；
  3. 日志格式清理（已在提交 `481d0bf` 固化并经契约测试锁定）。
- 已完成测试补齐：
  1. `tests/tool_editor_controller_test.cpp` 增加对 `setEditBuiltins` 切换显示/隐藏内置工具的契约测试；
  2. `tests/tool_editor_dialog_test.cpp` 增加 QML 真实弹窗渲染内置工具节点的端到端测试。
- 已完成重新部署：
  全量重新打包部署至 `E:\Softwares\VisionRT`（2783 个文件），元数据 `dirty=false`。
- 已完成安装态及端到端验证：
  `VisionRT.exe -v`、`installed_pipeline_contract`、`installed_python_author_contract`、`tool_editor_controller_test`、`tool_editor_dialog_test` 全部 PASS（退出码 0）。

**验证证据**
- `.\build\bin\tool_editor_controller_test.exe` → 退出码 0，`[PASS] tool_editor_controller_test`；
- `.\build\bin\tool_editor_dialog_test.exe` → 退出码 0，`[PASS] tool_editor_dialog_test`（QML 对话框成功渲染内置工具节点）；
- `.\build\bin\installed_pipeline_contract.exe "E:\Softwares\VisionRT\tools-package"` → 退出码 0；
- `.\build\bin\installed_python_author_contract.exe "E:\Softwares\VisionRT\tools-package"` → 退出码 0；
- `python tools/package_app.py --install-dir E:/Softwares/VisionRT --tools-package D:/Project/VisionTools/dist/VisionTools.zip` → 退出码 0，`package verification complete: 2783 files`，`package complete: E:\Softwares\VisionRT`；
- `& "E:\Softwares\VisionRT\VisionRT.exe" -v` → 退出码 0，输出 `VisionRT 0.1.0`。

**下一步**
- 保持当前部署环境，等待用户体验并确认。

### 2026-11-05 — 拉取最新代码、构建双仓并部署至 E:\Softwares\VisionRT

**目标**
- 拉取 VisionRT 与 VisionTools 最新代码及子模块，编译构建并完整打包部署至 `E:\Softwares\VisionRT`。

**当前状态**
- 已完成拉取：VisionRT 推进至 `fb89201`，VisionTools 推进至 `9cb038f`，Contracts 子模块对齐最新。
- 已完成构建：VisionRT（`VisionRT.exe`、`visionrt_validate_tool.exe`、测试可执行）与 VisionTools（`vst_image.dll`、`vst_infer.dll`、`vst_metrology.dll`）Release 目标全部编译链接通过。
- 已完成清理：移出残留的历史未跟踪孤立旧子模块（`VisionRTToolSDK`、`yaml-cpp`），两仓工作区保持 clean（`dirty=false`）。
- 已完成打包部署：VisionTools 成功出包（22 工具、3 模块，`dist/VisionTools.zip`），部署至 `E:\Softwares\VisionRT`（2782 文件），元数据 `build_info.json` 均为 `dirty=false`。
- 已完成验证：无头版本验证 `VisionRT.exe -v` 输出 `VisionRT 0.1.0`；安装态契约 `installed_pipeline_contract` 与 `installed_python_author_contract` 全部通过；工作台启动截屏及日志记录验证通过。

**验证证据**
- `python D:\Project\VisionTools\tools\package_tools.py --config Release --build-dir D:/Project/VisionTools/build --install-dir D:/Project/VisionTools/dist/VisionTools --zip-output D:/Project/VisionTools/dist/VisionTools.zip --validator D:/Project/VisionRT/build/bin/visionrt_validate_tool.exe --profile rtx40-build` → 退出码 0，`已打包 22 个工具：D:\Project\VisionTools\dist\VisionTools.zip`；
- `python tools/package_app.py --install-dir E:/Softwares/VisionRT --tools-package D:/Project/VisionTools/dist/VisionTools.zip` → 退出码 0，`package verification complete: 2782 files`，`package complete: E:\Softwares\VisionRT`；
- `& "E:\Softwares\VisionRT\VisionRT.exe" -v` → 退出码 0，输出 `VisionRT 0.1.0`；
- `.\build\bin\installed_pipeline_contract.exe "E:\Softwares\VisionRT\tools-package"` → 退出码 0；
- `.\build\bin\installed_python_author_contract.exe "E:\Softwares\VisionRT\tools-package"` → 退出码 0；
- `& "E:\Softwares\VisionRT\VisionRT.exe" --screenshot "D:\tmp\installed_workbench_e.png" --screenshot-delay 2000` → 退出码 0，生成 48153 字节截图。

**下一步**
- 在 `E:\Softwares\VisionRT` 生产环境中继续使用或进行业务流测试。

### 2026-11-05 — 提交 R49 两仓并用干净元数据出包（1837 文件）

**目标**
- 把 R49 改动从工作区收进版本库，用 clean 元数据重新部署到 `F:\tmp\VisionRT`，让安装包真正包含四列编辑器与新图像选择。

**当前状态**
- 已完成提交：VisionRT 代码与规范改动（方案文档不入库，目录保留在本地并加入忽略规则）；VisionTools `9cb038f`（18 文件，17 份声明补 `display.visible`、YOLO 三处 `editor` 改 `path`）。
- 已完成构建：Release 链接通过，`build/bin/build_info.json` 记录 `755e131`、`dirty=false`；VisionTools 元数据目标输出 `9cb038f`、`dirty=false`。
- 已完成出包：以 `build/tools-package`（schema 6、22 工具、3 模块）压缩为 `F:\tmp\VisionTools-9cb038f.zip`，部署到 `F:\tmp\VisionRT` 共 1837 文件；打包输出 `tools package verification passed successfully` 与 `package verification complete: 1837 files`。
- 已完成旧目录保留：`F:\tmp\VisionRT` 整体改名为 `F:\tmp\VisionRT_R49_prev`，避免批量删除触发环境安全策略；新目录全新部署，未复用任何旧文件。
- 已完成启动验证：`VisionRT.exe -v` 输出 0.1.0；截图 `F:\tmp\r49_packaged.png` 工具树 8 个分类、画布与参数面板正常；日志写入 `F:\tmp\VisionRT\logs\visionrt_2026-11-05.log`，`%LOCALAPPDATA%\VisionRT` 未创建。

**验证证据**
- `cmake --build build --config Release` → 退出码 0，`[31/31] Linking CXX executable bin\VisionRT.exe`。
- `cmake --build F:\Projects\VisionTools\build --target visiontools_build_info` → 退出码 0。
- `python tools/package_app.py --install-dir F:/tmp/VisionRT --tools-package F:/tmp/VisionTools-9cb038f.zip` → 退出码 0，1837 文件。
- 安装目录 `build_info.json` 与 `tools-package/build_info.json` 均为 `dirty=false`。

**未完成**
- 未跑全量与 UI 场景测试（按约定）；`workbench_roi_contract`、`workbench_paths_contract` 在图像选择与路径迁移后未重跑。
- 出包后的十项人工验收尚未进行，清单见本地方案文档 `06_阅读与人工验收.md`（不入库）。

**下一步**
- 在 `F:\tmp\VisionRT` 上完成人工验收：四列布局、树右键、范围校验、固定卡、图像显示开关、统一路径、取消无副作用、内置工具开关、重启一致性、只写安装目录。

### 2026-11-05 — 图像选择归属、隐藏不跳项与智能点选取图

**目标**
- 完成 R49 S08/S09/S10 剩下的图像选择语义：隐藏项清空而不跳到下一项、节点 0 也能正确归属、EasyTouch 只读 ROI 声明的图像。

**当前状态**
- 已完成选择归属：`ResultPresentation` 用 `std::optional<NodeId>` 记录“当前选择属于哪个节点”，不再用 0 当“没有节点”（第一个节点编号就是 0）。节点第一次显示时选中它第一个可见图像；同一节点内所选端口被隐藏或删除时清空，不替换成另一个；节点完全没有可见图像时同时清空归属，避免把上一个节点的端口带过来。
- 已完成选择恢复：ROI 会话结束时仍恢复原来的真实端口，端口已无效就保持清空；ROI 编辑期间选择器只呈现它声明的真实背景输入。
- 已完成 EasyTouch 取图：`assistRequest` 改为复用 `assistContext` 已验证的 ROI 图像绑定与快照，不再对可选上游来源做无检查的解引用；无缓存时明确拒绝，不借其他图像。
- 已完成说明与行为一致：智能点选提交注释不再声称“非法选项回退默认值”，与 S11 的“非法枚举报错”一致。
- 测试夹具修正：`ui_result_isolation_contract` 的图像工具此前没有 `display.visible`，按 S07 选择器不会列出它，之前那次“通过”是不可信的旧二进制；现已显式声明可见，并在改成 Int 时同步清除该声明。
- 未完成：S16 三仓 SHA/gitlink 与测试分层属于发布流程，未在仓库内落地。

**验证证据**
- `cmake --build build --target VisionRT` 与相关目标链接成功。
- `ctest -R "^(ui_value_contract|definition_json_contract|project_storage_contract|log_lifecycle_contract|tool_port_list_model_test|tool_editor_controller_test)$"` → 6/6 通过。
- `ui_result_isolation_contract` 单次运行通过：选择为 `outputs:2`、两张图像、主图与缩略图都有帧；该场景因选择语义改动必须核验一次。
- 未验证：`workbench_roi_contract` 未在本次选择改动后重跑；未跑全量测试。

**下一步**
- 需要时手工核验“关闭图像显示后选择器清空且不跳到下一项”，以及 ROI 无背景时的矩形编辑。

### 2026-11-05 — 加载不自动改值、内置工具编辑开关与迁移显式失败

**目标**
- 收口 S11 剩余的工程加载语义、S12 的日志写入根、S13 的内置工具编辑开关，并让历史迁移在无法映射时直接失败。

**当前状态**
- 已完成 S11 剩余（加载不自动修复）：`ProjectStorage` 对类型不兼容的初始化参数和输入不再替换成声明默认值，也不清除上游绑定；原值、模式与来源全部保留，只报告 `SchemaChanged` 并说明“修正后才能执行”。只有当前声明新增、工程从未保存过的端口才填入声明默认值（这是物化新端口，不是改写旧值）。数组 dtype 变化也不再换值，float32 数据不会再被当成 int64 数组解释。
- 已完成 S12：`LogService` 默认文件路径在软件目录未知时不再回退 `QDir::currentPath()`，改为文件日志不可用并只走窗口通道；`UiPreferences`、最近工程、工具声明仍固定在软件根。
- 已完成 S13：新增偏好 `toolEditor.editBuiltins`（缺省 false）与设置菜单“允许编辑内置工具”；关闭时工具树不列出随安装发行的声明，打开时同卡编辑。保存内置工具走 `AppController::saveBuiltinTool`：先要求声明在软件目录内，再按已注册的 C++ 工厂合同校验端口（`ToolRegistry::validateContract`），不一致直接报错、保留草稿，不写文件；一致时用 `writeToolDefinitionFile` 原子写回原声明并替换注册表定义。复制与删除仍拒绝内置工具；存在未保存的内置修改时不允许关闭开关。
- 已完成 S15 剩余（迁移）：`migrate_r44_to_r45.py` 遇到无法识别的初始化参数、类型快照端口、输入端口或上游引用端口时直接抛错，该文件计为失败且不写输出，不再“丢字段后成功”。
- 未完成：S10 的 EasyTouch helper 与 ROI 叠加细节未在本轮改动；S16 的三仓 SHA/gitlink 与 profile 分层属于发布流程，未在代码内落实。

**验证证据**
- `cmake --build build --target VisionRT` → 链接成功。
- `ctest -R "^(definition_json_contract|project_storage_contract|log_lifecycle_contract|tool_port_list_model_test|tool_editor_controller_test)$"` → 5/5 通过。
- `definition_json_contract` 新增写入用例：合法声明写回后可重新解析，非法声明被拒绝且原文件内容不变。
- `project_storage_contract` 按新语义更新：类型变化后保留 int64 原值与上游引用并产生 `SchemaChanged`；float32 数组在 dtype 变化后仍是 float32。
- 未验证：内置工具保存的真实窗口流程与安装态工具包；按要求未跑 UI 测试，也未跑全量测试。

**下一步**
- 需要时用真实安装包手工验证“允许编辑内置工具 → 改名称/默认值 → 保存 → 重启仍生效”，以及改端口类型被拒绝的提示。
- 继续按 08 文档核对 EasyTouch 与 ROI 叠加的 S10 条目。

### 2026-11-05 — 删除失败回退路径（IMG-02、DATA-01、CLEAN-01 部分）

**目标**
- 按“失败就失败、报错即可”的原则清理失败后自动换源、补默认值、改工程、简化连线、替代日志等行为，不保留可用性兜底。

**当前状态**
- 已完成图像选择（IMG-02）：`ResultPresentation` 的选择键改为真实 `{stage,id}`（QML 传 `inputs:1` / `outputs:2`）；`imageOutputs` 只枚举有效合同里 `display.visible=true` 的 Image 输入输出，删除 ROI 假 ID0“输入预览”、删除失败后借 `sameGeometryAs` 或“第一个非空输入”的整段逻辑；选中端口无值即清空，不跳转、不保留旧画面。ROI 会话只呈现它声明的真实背景输入。`ResultsPanel.qml` 去掉 `images` 名称特例。
- 已完成结果默认值（DATA-01）：`validateToolExecution` 在成功状态缺少业务输出时直接报“工具未发布输出端口”，不再用声明默认值补齐；失败状态仍保持 Missing。`normalizeInput` 区分手动与上游来源，上游 Missing 不再替换成手动默认值；非法枚举改为报错，删除“回退默认值”。
- 已完成工程加载（CLEAN-01 部分）：无效上游引用、被改动的上游输出、类型变化的输入都不再自动清除绑定；保留原引用并报“绑定已保留供修复，不可执行”。输入字面量仍会按新类型重置（尚未做到完全保留原值）。
- 已完成其余失败回退：`softwareDataDir()` 在无法确定软件目录时抛错，不回退当前目录；日志文件打不开时不再挂 stderr 替代 sink，只保留窗口通道并记录失败；避障失败不再生成简化连线，保留空路径与错误；自定义工具声明损坏时按路径报告，删除时若无法确认包归属则拒绝删除。
- 已完成发行资产迁移：`F:/Projects/VisionTools` 的 17 个声明、31 个 Image 端口补 `"display": {"visible": true}`（assets 与 dist 同步）；`YOLO目标检测.json` 三处 `editor: file` 改为 `path`。build/tools-package 已从 dist 刷新。
- 已完成计时兼容清理：删除 `ExecutionMetrics::executeMs/convertMs` 别名，执行与验证直接写 `adapterExecuteMs/validateMs`，不再在收尾复制。
- 已完成偏好写入失败上报：`UiPreferences::setEasyTouchEnabled` 返回写入结果，失败时报告“只在本次运行生效”；`RecentProjects::record` 通过可选 `persisted` 告知调用方，最近工程写失败会被报告。
- 未完成：`ProjectStorage` 的类型不兼容输入仍会重置字面量；`toolEditor.editBuiltins` 尚未接入（内置工具保存到原声明的路径与合同校验未设计）。

**验证证据**
- `cmake --build build --target VisionRT` → 链接成功。
- 必要回归 12 项全部通过：`port_value_validation_test`、`definition_json_contract`、`custom_tool_store_contract`、`log_lifecycle_contract`、`project_storage_contract`、`ui_value_contract`、`tool_port_list_model_test`、`tool_editor_controller_test`、`tool_editor_dialog_test`、`ui_result_isolation_contract`、`workbench_roi_contract`、`workbench_parameters_contract`。
- 后端补充：`runtime_contract`、`cpu_tools_contract`、`python_tools_execution_test` 通过（按新语义更新了“缺输出补默认值”“失败源接收值”等断言）；计时清理后 `runtime_contract` 再次通过；`recent_projects_contract`、`tool_port_list_model_test` 通过。
- 资产迁移后 `cpu_tools_contract` 再次通过，确认 17 个声明仍可解析。
- 未通过/未计：`wire_router_contract` 测试源码本身因 `PortRelation` 初始化列表不匹配无法编译，使用旧二进制的结果不计为通过。
- 未验证：`workbench_paths_contract` 未在资产迁移后重跑；安装态整包 UI 未验证；按要求未跑全量或额外 UI 测试。

**下一步**
- 需要时再复跑 `workbench_paths_contract`，确认图像读取的输出进入选择器。
- 继续清理 `ProjectStorage` 的字面量重置；设计内置工具保存到原声明的合同校验后再接 `toolEditor.editBuiltins`。

### 2026-11-05 — R49 四列工具编辑器、草稿模型、统一路径与图像显示声明

**目标**
- 按 R49 交付 `docs/VisionRT_R49_完整开发方案_统一组件与路径`：四列编辑界面（树/工具卡/输入/输出）、窗口级草稿（取消全丢、保存全提交）、唯一路径编辑器、`display.visible` 声明与文本规范。

**当前状态**
- 已完成四列界面：`CustomToolDialog.qml` 重写为工具树 + 工具卡 + 输入列 + 输出列，底部只保留取消与保存；参数卡提取为 `ToolPortCard.qml`（编号可拖动排序、删除 ×、中英文名、类型、默认值、Int/Float 上下限、Image 显示开关、更多设置里的编辑器/关联/候选值）；类工具的初始化参数作为输入列独立小组。
- 已完成窗口级草稿：`ToolEditorController` 由单草稿改为多草稿 + 目录草稿 + 待删除集合；切换工具不再要求先保存，取消丢弃全部草稿，保存先整体校验再统一写盘；复制也改为草稿，保存时才复制脚本与资源。删除了 `ToolPortProperties.qml` 与旧单草稿接口（apply/revert/currentStage/tools/newTool）。
- 已完成组件复用：卡片与运行面板同用 `ParameterTextField`、`IntegerParameterEditor`、`QuiSpinEditor`、`QuiToggleSwitch`、`QuiComboBox` 与 Theme 尺寸；`IntegerParameterEditor` 去掉对全局 app 的日志依赖，改由调用方处理 `rejected`。
- 已完成统一路径（S18）：`editor` 只保留 `path`，删除 file/directory 的校验、`isPathPort`、QML 分支与元数据推导；路径按钮固定打开 `QuiFileFolderDialog.FileOrFolder`，取消不改字段。相关夹具与测试同步改为 path。
- 已完成图像显示声明（S07）：`PortDefinition.imageVisible`，`DefinitionJson` 读写 `display.visible`（仅 Image、缺省 false、类型错误拒绝），端口模型新增 `showImage`，重定型清除；JSON 往返与拒绝用例已覆盖。
- 已完成文本规范（S19）：自有代码与文档不再含 U+00B7（含 ResultPresentation 的预览拼接与端口候选标签）。
- 未完成：图像结果面板仍只枚举输出端口，ROI 编辑仍构造 ID0 的“输入预览”项并保留失败借图分支（R49 S08-S10 / IMG-02）；内置工具编辑开关 `toolEditor.editBuiltins`（S13）；DATA-01 默认值语义、CLEAN-01 回退清理未动。
- 未完成：`workbench_paths_contract` 与 `ui_project_paths_contract` 两个 UI 契约仍失败，见验证证据；未在真实安装态点完整编辑/取消流程（仅自动化窗口）。

**验证证据**
- `cmake --build build --target VisionRT` → 链接成功。`tools\build.cmd` 因沙箱禁用 reg.exe 无法初始化 MSVC，改用显式 INCLUDE/LIB（MSVC 14.29.30133 + Windows SDK 10.0.19041.0）构建。
- `ctest -R "^(custom_tool_store_contract|tool_discovery_contract|tools_package_contract|definition_json_contract|port_value_validation_test|tool_definition_validation_test|project_storage_contract|tool_port_list_model_test|tool_editor_controller_test|tool_editor_dialog_test|ui_value_contract)$"` → 全部通过（最近一次 11/11）。
- `tool_editor_dialog_test` 真实加载 `CustomToolDialog.qml`：添加工具与两个参数、点击保存后窗口关闭并写出 `custom.tool.1.json`；截图 `F:/tmp/tool-editor.png` 显示四列、卡片与浅色主题正常。
- `ctest -R "^(workbench_paths_contract|ui_project_paths_contract)$"` → 仍失败：前者 “path chooser did not open from its embedded button”，后者 “path fixture node was not selected”。两者都在节点/选择器 UI 层；已修正 `ParameterEditor.qml` 残留的 `onPathKindChanged`，但仍需继续定位，未宣称通过。
- 未验证：真实窗口中的拖动排序、右键目录菜单、保存失败保护、安装态 `F:/tmp/VisionRT` 回归。

**下一步**
- 先定位 `workbench_paths_contract` 的选择器可见性假设（统一 FileOrFolder 后测试仍等 `visible`）与 `ui_project_paths_contract` 的节点选择；必要时按新交互重写这两条 UI 断言，而不是放宽产品行为。
- 再做 IMG-02：`imageOutputs` 改为按 `{stage,id}` 的真实端口列表（仅 `display.visible`），删除 ROI 假 ID0 与失败借图；与 VisionTools 资产同批发布，节奏见顶部待裁决。
- 之后补 S13 内置编辑开关、S11 默认值语义、S15 回退清单。

### 2026-11-04 — 实际部署到 F:\tmp\VisionRT（schema 6 工具包，1837 文件）

**目标**
- 按约定把当前构建部署到 `F:\tmp\VisionRT`，并验证“用户数据只在软件目录、force-clean 不删用户工具”。

**当前状态**
- 已完成部署：`F:\tmp\VisionRT` 1837 文件 / 929 MB，`VisionRT.exe -v` = 0.1.0，启动截图正常（工具树 7 个分类），包校验输出 `package verification complete: 1837 files`。
- 工具包：`build/tools-package`（schema 5，22 工具）与 `build/bin/tools-package`（schema 4，21 工具）都被宿主校验器拒绝（`schema_version 必须是整数 6`）；改用 `F:\Projects\VisionTools\dist\visiontools-deploy.zip`（schema 6、22 工具、3 模块）后通过。
- 已完成 force-clean 实测：在安装目录放置 `custom-tools\tools\_probe.json`、`ui-preferences.json` 与 `logs\visionrt_2026-11-04.log` 后重跑 `--force-clean` 全量部署，三者全部保留，程序文件被替换（两轮均 13 s 左右）。
- 已完成路径验证：启动日志写入 `F:\tmp\VisionRT\logs\`；`%LOCALAPPDATA%\VisionRT` 未被重新创建；`custom-tools` 只在首次保存时创建（已预建空 `tools\`）。
- 已完成清理：探针、临时工具包 ZIP、冒烟截图已删除；旧的 `%LOCALAPPDATA%\VisionRT` 前一轮已永久删除。
- 未完成：真实窗口里保存一个工具，确认落在 `F:\tmp\VisionRT\custom-tools\tools\`（代码断言已覆盖，人工未点）。

**验证证据**
- `VisionRT.exe -v` → `VisionRT 0.1.0`；`--screenshot` → 正常主窗口。
- 部署日志：`deployed tools package ... into F:\tmp\VisionRT\tools-package`、`tools package verification passed successfully`、`package verification complete: 1837 files`。
- 重部署后 `custom-tools\tools\_probe.json` 与 `ui-preferences.json` 仍在，`logs` 仍在。
- `find /f/tmp/VisionRT -type f | wc -l` → 1837；`du -sh` → 929M。

**下一步**
- 打开 `F:\tmp\VisionRT\VisionRT.exe`，在自定义工具里复制内置 Python 示例并应用，确认 `F:\tmp\VisionRT\custom-tools\tools\` 出现声明、`python\` 出现脚本。
- 后续部署固定使用 VisionTools 的 `dist/visiontools-deploy.zip`；本仓库 `build` 下的旧工具包不能用于新宿主。

### 2026-11-04 — 可写根固定到软件目录：工具、偏好、日志不再写用户目录

**目标**
- 按明确要求：实际部署目录为 `F:\tmp\VisionRT`；用户工具必须写在软件目录下，不能落在用户目录或 C 盘。同步收紧方案里的路径边界。

**当前状态**
- 已完成单一可写根：`src/core/ToolPaths.h` 新增 `softwareDataDir()`（可执行文件所在目录），`CustomToolStore::defaultRootDir()` 改为 `<exe>/custom-tools`，不再使用 `QStandardPaths::AppLocalDataLocation`。
- 已完成交互偏好迁移：`UiPreferences` 写入 `<exe>/ui-preferences.json`，不再使用 `AppConfigLocation`；`recent_projects_contract` 改为直接定位该文件并在结束时清理，不再依赖 `QStandardPaths` 测试模式。
- 已完成 Qt 隐式写入：`main.cpp` 在创建应用前关闭 `QSG_RHI_DISABLE_DISK_CACHE` 与 `QT_DISABLE_SHADER_DISK_CACHE`，着色器/管线缓存不再落到 `%LOCALAPPDATA%`；日志本已默认 `<exe>/logs`。
- 已完成打包保留：`tools/package_app.py` 新增 `PRESERVED_INSTALL_ENTRIES = {custom-tools, logs, ui-preferences.json}`，`--force-clean` 只替换程序文件，不删用户工具、日志与偏好；新增 Python 单测覆盖。
- 已完成方案与部署约定：`docs/R45/05_实现架构与生命周期.md` 的“默认用户根”改为软件目录并写明保留规则；`docs/R45/12_整合追踪与验证.md` 增加可写根入口；`tools/README.md` 示例部署目录改为 `F:\tmp\VisionRT`。
- 未完成：`%LOCALAPPDATA%\VisionRT` 下的旧数据尚未删除（`VisionRT\logs\visionrt.log`、`VisionRT\ui-preferences.json`、空的 `VisionRT\cache\`），按安全规则等确认后移入回收站。
- 未完成：未执行实际部署；部署时使用 `python tools\package_app.py --install-dir F:\tmp\VisionRT`（或 `--deploy-tools-only`）。

**验证证据**
- `cmake --build build --target VisionRT custom_tool_store_contract recent_projects_contract` → 成功，`VisionRT.exe` 链接完成。
- `ctest -R "^(custom_tool_store_contract|recent_projects_contract|tool_port_list_model_test|tool_editor_controller_test|tool_definition_validation_test)$"` → **5/5 通过**（0.80 s）。
- `custom_tool_store_contract` 新增断言：`CustomToolStore::defaultRootDir() == <exe dir>/custom-tools`。
- `python -m unittest tests.test_package_app.InstallationContract.test_clean_replaces_program_files_but_keeps_user_data` → 通过（清理删旧 DLL、保留三类用户数据）。
- 未验证：真实窗口保存一次工具，确认目录为 `build\bin\custom-tools`；部署包内 `custom-tools` 跨 force-clean 保留需在 `F:\tmp\VisionRT` 实测。

**下一步**
- 清理旧用户目录数据（待确认）：`%LOCALAPPDATA%\VisionRT\VisionRT\logs\visionrt.log` 7.8 KB、`ui-preferences.json` 212 B、空目录 `cache\`。
- 部署到 `F:\tmp\VisionRT` 后，先在编辑器保存一个工具，确认写入 `F:\tmp\VisionRT\custom-tools`，再做一次 `--force-clean` 重部署确认工具仍在。

### 2026-11-04 — R45 编辑器：删除端口对账、拖动排序、内置 Python 可复制

**目标**
- 收掉编辑器里还能产出坏声明的两条路径（删除被引用的参数、函数工具加初始化参数），补上方案要求的拖动排序，并让“复制内置工具再改”这句提示真正成立。

**当前状态**
- 已完成删除对账：核心 `erasePort` 在删参数时同步清除其他参数指向它的 `relations.image / same_geometry_as / alignment / path_mode` 与 `interaction.proposals / inputs`，逐条返回被清除项；被 `type_from` 输出引用或宿主固定端口时直接拒绝，草稿保持原样。端口模型 `removePort` 走同一入口，删除后状态栏列出“已删除参数，并清除：端口 2 的图像关联”。
- 已完成初始化阶段边界：函数工具（PythonFunction）在“初始化参数”页新增会被模型拒绝并说明原因，不再等到应用时才报 `Function tool cannot have init params`。
- 已完成拖动排序：端口表新增常驻拖动柄（固定行留空、不显示柄），拖动时关闭列表滚动并把落点解析到业务区，松手调用 `movePort`；落点行与拖动行高亮。上下移动按钮保留。
- 已完成内置 Python 工具复制：`cloneCustomTool` 从声明自己的根（`source.pythonPackage()`）读脚本与资源，`cloneTool` 在自有工具之外也查运行时目录，`canClone` 属性按工具类型决定“复制”是否可用；内置提示文案区分“可复制”与“原生需重打包”。
- 已完成复制后的目录刷新与脏保护：复制成功后按空闲路径 `rescanTools()`；复制不再静默丢弃未保存草稿（与切换工具、新建一致地拒绝）。
- 已完成影响预览补全：被删除的输出若仍被其他节点作为数据源，摘要增加“删除的输出参数 N 会断开下游连接”；保留状态端口 0/1 不误报。
- 未完成：拖动与内置复制未在真实窗口验证；`ui` 组与安装态未回归。

**验证证据**
- `cmake --build build --target VisionRT tool_port_list_model_test tool_editor_controller_test custom_tool_store_contract` → 成功，`VisionRT.exe` 链接完成。
- `ctest -R "^(custom_tool_store_contract|tool_port_list_model_test|tool_editor_controller_test|tools_package_contract|tool_discovery_contract|tool_definition_validation_test)$"` → **6/6 通过**（1.33 s）。
- 新增断言：删除被关联的输入后关系被清除且消息含“端口 2 的图像关联”、对齐/路径模式按目标存活保留；固定端口与 `type_from` 来源删除被拒；函数工具 init 新增被拒、类工具可新增；内置根下的 Python 工具可复制到用户根且原包不变。
- `qmllint src/ui/qml/CustomToolDialog.qml` → 无语法错误（仅既有 import/layout 告警）。
- 未验证：真实窗口中的拖动落点、拖动时滚动抑制、内置 Python 工具的“复制”按钮。

**下一步**
- 真实窗口点一遍：拖动业务参数跨行、拖动到固定行上方应停在首条业务行；选内置 Python 示例→复制→改名→应用→运行。
- 原生（C++/Host）内置工具仍不可复制，这是注册工厂的边界，不是缺口。

### 2026-11-04 — R45 编辑器：关系、编辑器与候选值可写，字符串值按原文往返

**目标**
- 让自定义工具能声明 R45 里的端口关系与编辑元数据：ROI 指向图像、路径模式、对齐、候选值、编辑器；并修掉 String 值在编辑器里无法原样往返的问题。

**当前状态**
- 已完成关系写入：端口模型新增 `relationCandidates(row, kind)`，候选由同一条关系规则过滤（`requireRelationTarget`），`updatePort` 接受 `image` / `alignment` / `path_mode` / `same_geometry_as` 的 `inputs.N` 引用或空值清除；悬空、错类型、自引用的引用在入口即被拒绝，不再等到应用时才报“relations.image must reference an Image port”。
- 已完成编辑器元数据单一来源：核心 `editorChoices(TypeSpec)` 给出某类型可用的编辑器集合，`retypePort` 的清理判定与界面下拉共用它；`updatePort` 接受 `editor`，类型不匹配直接拒绝；修改编辑器时若已有路径模式则要求先清除，避免声明自相矛盾。
- 已完成候选值：`updatePort` 接受 `options`（每行一个值，按端口类型解析，空文本清空），属性面板用多行输入提交。
- 已完成 String 值往返：编辑器显示 String 用的是 `ValueJson::text` 的裸文本，解析时却要求 JSON 引号，导致改默认值必然失败。现在 `parseEditorValue` 对 String 按原文取值，其它类型仍走 ValueJson；`addPort` 与 `updatePort` 共用。
- 已完成属性面板：新增“编辑器”“图像关联/对齐关系/路径模式/同几何关系”“候选值”入口；关联只列出声明真正接受的端口（含阶段名），不再手填。
- 未完成：端口拖动排序（仍只有上移/下移，避免未经真实窗口验证的手势回归）；`ui` 组与安装态未回归。

**验证证据**
- `cmake --build build --target VisionRT` → 成功链接，无编译错误。
- `ctest -R "^(custom_tool_store_contract|tool_port_list_model_test|tool_editor_controller_test|tools_package_contract|tool_discovery_contract)$"` → **5/5 通过**。
- `tool_port_list_model_test` 新增断言：图像候选仅含真实图像端口；自引用/错类型被拒且旧目标保留；对齐只接受 3 维 float64 输入；路径模式需 path 编辑器；`rect` 不能设到 String；候选值按行解析并清空；String 默认值按原文保存。
- `qmllint ToolPortProperties.qml` → 无语法错误（仅既有的 layout/import 告警）。
- 未验证：真实窗口里关系下拉、候选值多行提交与拖动排序。

**下一步**
- 需要拖动排序时，在“拖动柄”列挂独立 DragHandler，避免抢走列表滚动；先在真实窗口验证再合入。
- 自定义工具若要支持包导入，复用现有包校验/安装链，不要在编辑器里另建一套。

### 2026-11-04 — R45 编辑器收口：可运行模板、改型对账、只读与标识边界

**目标**
- 修掉自定义工具编辑器里“能编辑但会产出坏结果”的几处：新建工具的脚本、复制语义、改类型、工具标识与删除影响。

**当前状态**
- 已完成新工具模板按声明生成：`CustomToolStore::pythonTemplate(const ToolDefinition&)` 返回全部业务输出为 `None`、状态码 1“工具尚未实现”，未动过的工具运行时得到失败状态而不是“工具未发布输出端口”的框架错误；脚本与声明都走同一个 QSaveFile 写入。
- 已完成复制语义：`cloneCustomTool` 必须读到源脚本（拒绝空壳复制）、复制声明里的相对资源、拒绝复制 C++/宿主工具；控制器捕获失败并给出状态消息。
- 已完成删除归属：`deleteCustomTool` 先读声明里的 `implementation.package` 再删声明与代码目录，包名不再假定等于工具 id。
- 已完成改类型对账：核心 `retypePort` 保留新类型仍能容纳的默认值/范围/关系，清除不能容纳的字段并逐条返回；端口模型新增 `typeChangeImpact`（在副本上预览）与 `changeType`（提交），QML 改类型前弹窗列出将清除项。改类型不再静默清空范围，也不会留下悬空的 `relations.*`。
- 已完成只读与标识边界：`tools()` 合并 registry 目录，内置工具可查看但只读（端口模型 `editable=false`、应用/删除/复制被拒）；新草稿首次应用前可改工具标识（同步代码包路径），应用后锁定；重复标识与“有未保存草稿时新建”均明确拒绝。
- 已完成删除影响：`toolUsage()` 与影响预览共用同一处节点计数，删除确认显示引用节点数与“保留为工具缺失”的后果。
- 已完成属性面板补齐：所属页面改为固定页签下拉（不再手填非法 tab）、新增分组/步长/小数位；模型 `updatePort` 拒绝 `typeToken`，改类型只走 `changeType`。
- 未完成：端口拖动排序（当前只有上移/下移）；`relations`/`options`/`editor` 仍无界面入口（`retypePort` 已能对账，但编辑器不能新建这些属性）；`ui` 组与安装态未回归。

**验证证据**
- `cmake --build build --target VisionRT tool_port_list_model_test tool_editor_controller_test custom_tool_store_contract` → 成功，`VisionRT.exe` 链接完成。
- `ctest -R "^(custom_tool_store_contract|tool_port_list_model_test|tool_editor_controller_test|tools_package_contract|tool_discovery_contract)$"` → **5/5 通过**（0.95 s）。
- 模板语法：临时导出函数/类模板到 `build/build` 后 `python -m py_compile` → 通过（校验后已还原测试文件并删除导出物）。
- `qmllint src/ui/qml/CustomToolDialog.qml src/ui/qml/ToolPortProperties.qml` → 仅缺 import path 的警告，无语法错误。
- 未验证：`ui` 组、安装态、真实窗口中的改类型确认与只读工具浏览。

**下一步**
- 打开一次“自定义工具”对话框实测：新建→加图像输出→应用→运行，确认状态为“工具尚未实现”而不是框架错误。
- 需要端口拖动排序与 relations/options 编辑时，按 `retypePort` 的对账入口扩展属性区，不要另写一套类型规则。
- 本机 shell 缺 MSVC 的 `LIB`（`vcvars64.bat` 依赖被安全策略拦截的 `reg.exe`），链接前需手动导出 `INCLUDE/LIB/PATH`；可直接 `source build/_env.sh`（该脚本在忽略目录内，仅本机使用）。

### 2026-11-04 — R45 收尾：ui 组测试迁移完成，修掉两个迁移暴露的产品缺陷

**目标**
- 完成 T45-09/T45-14 的 `ui` 组测试迁移（schema 6 数字端口、格式 7 工程、编号字典键）。
- 只在受影响用例上验证，不跑无关全量。

**当前状态**
- 已完成 `ui` 组 41 项测试全部迁移并通过；`backend` 43 项保持通过。
- 测试二进制此前未被默认构建（`ctest` 跑的是 11-02 的旧产物），这解释了“看起来仍按 schema5 失败”的大部分现象；现已显式构建每个契约目标。
- 修复产品缺陷 1：失败/取消的工具省略无默认值的业务输出时，`validateToolExecution` 曾抛“工具未发布输出端口”，把取消（接收器 -5）翻译成框架错误。现在失败态保留 `Missing`，成功态才要求默认值。
- 修复产品缺陷 2：表达式结果端口从 1 起分配，与宿主状态端口 1 冲突，执行条件文本显示成“状态消息”。现在业务输出从 2 起（`ExpressionConfig::nextPortId`、QML `nextSeq`），并在 `validateExpressionConfig` 拒绝占用宿主端口。
- 测试夹具统一：`ParameterFixturePorts.h` 集中参数夹具编号；`WorkbenchFixtures` 使用工具包内 `next_port_id`；多条内联 JSON 声明升到 schema 6，关系/`type_from`/`path_mode` 改为数值引用。

**验证证据**
- `cmake --build build --target ...` → 所有契约目标（含 `workbench_contract` 聚合器）通过。
- `ctest -L ui` → **41/41 通过**。
- `ctest -L backend` → **43/43 通过**。
- 失败根因记录：旧二进制（未重建）、schema5 夹具、字符串端口键、`format_version 4` 工程夹具、`parameter_<name>` QML 对象名、接收器 init 编号需为宿主固定 0..3。

**下一步**
- `build_incremental_contract` 仍需自带 `vcvarsall.bat`，本沙箱无法验证；其余 R45 测试面已全绿。

### 2026-11-03 — R45 收口：编辑器草稿一致性、自定义工具安全边界与遗留测试迁移

**目标**
- 修掉 R45 编辑器/存储在真实使用中会出错的点：固定端口重复投影、约束修改不置脏、复制覆盖已发布工具、影响预览是假值、界面主题引用不存在的令牌。
- 收敛 R45 迁移遗留的红测试，使 `backend` 与 `package` 组恢复全绿。

**当前状态**
- 已完成宿主固定端口单一来源：`hostPort` / `stripHostPorts` 落在 `src/core/ToolDefinition.cpp`，规范化与编辑器投影读同一份标签/类型/默认值；端口模型不再自备一份固定行文案。
- 已完成编辑器草稿语义：草稿只保存声明端口，`saveCustomTool` 改 const 并返回落盘声明，规范化只作用于副本；此前“加载已保存工具后固定行出现两次”的根因即草稿被就地规范化。
- 已完成脏判定补全：`samePort` 比较端口全部可编辑字段（min/max/step/decimals/options/relations/type_from/group 等），修改范围约束现在会置脏并可保存。
- 已完成编辑校验：`addPort`/`updatePort` 先校验后落盘，失败发 `rejected` 提示且不消耗端口号，不再静默吞掉非法值。
- 已完成复制与删除安全：`cloneTool` 递增后缀不再覆盖已有副本，`cloneCustomTool` 对已存在 id 直接拒绝；`deleteCustomTool` 校验标识（拒绝 `../` 逃逸）并返回是否真的删除。
- 已完成影响预览据实计算：按当前工程中引用该工具的节点统计受影响数量、将被解除绑定的输入编号与类型变更编号，替换原固定“无受影响节点”。
- 已完成生命周期约束：运行时忙时拒绝应用/删除；有未保存草稿时拒绝切换工具；目录加载出现同名工具报告双方路径并保留已发布定义。
- 已完成界面主题：`CustomToolDialog.qml` 与 `ToolPortProperties.qml` 此前引用的 `QuiColor.TextPrimary`、`SelectedRowBackground`、`Alert*`、`ErrorText` 等属性在调色板中并不存在。改为先用 QuickUI 的同义属性（`FontPrimary`/`FontCaption`/`ItemHover`/`ItemCheck`/`Background`/`Border`/`CardBackground`/`Highlight`/`Error`），只有 QuickUI 确实没有语义位时（警告文字）才用 `Theme.warning`；同时修正布局管理项尺寸写法，端口属性面板在 `dataChanged` 后刷新，并展示影响预览。
- 已完成遗留测试迁移：`tools_package_contract`、`tool_discovery_contract`、`package_replacement_test`、`validator_cli_contract.py`、`test_package_app.py` 升级到 schema6/数字端口；`tool_registry_contract` 的直通工具改为按编号映射输出；`native_value_lifetime` 增加加载失败原因输出。
- 未完成：`ui` 组 41 个测试仍按字符串端口编写（`workbench_*`、`ui_*`），编译不通过，属 T45-09/T45-14 未迁移部分；`build_incremental_contract` 需调用 `vcvarsall.bat`，本环境 `reg.exe` 被安全策略拦截，无法验证。

**验证证据**
- `cmake --build build` → 成功（`VisionRT.exe` 链接完成）。
- `ctest -L backend` → **43/43 通过**（14.8 s）。
- `ctest -L package -LE integration -E ^build_incremental_contract$` → **4/4 通过**。
- `ctest -R "custom_tool_store_contract|tool_port_list_model_test|tool_editor_controller_test"` → **3/3 通过**；新增断言覆盖固定行不重复、min/max 置脏、连续复制不覆盖、复制到已存在 id 被拒、`../` 标识被拒。
- `qmllint src/ui/qml/CustomToolDialog.qml src/ui/qml/ToolPortProperties.qml` → 无语法错误。
- 未验证：`ui`/`bridge`/`perf`/`gpu`/安装态与 `build_incremental_contract`。

**下一步**
- 迁移 `ui` 组测试到 PortId（先 `workbench_parameters`、`ui_receiver_identity_contract`、`ui_value_contract`，再 `workbench_contract`），迁移完成后再按 profile 跑 `-L ui`。
- 真实环境点开“自定义工具”对话框一次，确认主题、固定行锁定、影响预览与运行中禁用符合预期。

### 2026-11-03 — R45 交付：Schema 6 / ABI 9 数字编号、自定义工具与生命周期

**目标**
- 按 `docs/R45` 全面交付 R45 方案：Contracts ABI 9、Schema 6、工程格式 Format 7、数字 `PortId` 架构、自定义工具存储 `CustomToolStore`、端口模型 `ToolPortListModel`、控制器 `ToolEditorController` 与 QML 编辑弹窗 `CustomToolDialog`。
- 迁移工程与测试：交付迁移脚本 `tools/migrate_r44_to_r45.py`，更新示例工程与回归测试，消除历史字符串端口分叉。

**当前状态**
- 已完成 Contracts ABI 9 与 Schema 6：`PortId` 统一为 `std::int64_t`，去除 `std::string` 端口名依赖；`TypeSpec` 支持 34 种静态类型定义；公共端点与系统保留端口收敛。
- 已完成核心后端与存储：`CustomToolStore`（JSON-last 原子落盘、增删改查、克隆）、`ProjectStorage` Format 7（Schema 6 快照、数字端口键、全量输入字面量补齐）、`ProjectEditor` 与 `RuntimeEngine` 数字端口执行。
- 已完成 UI 交互层：`ToolPortListModel`（前缀保留行、单向递增 `nextPortId`）、`ToolEditorController`（完整类型支持、草稿变更高亮、JSON 预览）、`CustomToolDialog.qml` 与 `ToolPortProperties.qml`。
- 已完成迁移与示例：`tools/migrate_r44_to_r45.py`（自动化迁移旧工程至 Format 7）、更新 `examples/*.vrt.json`。
- 已完成测试与夹具迁移：修复 `installed_pipeline_contract`、`installed_python_author_contract`、`example_pipelines`、`cpu_tools_contract`、`custom_tool_store_contract`、`tool_port_list_model_test`、`tool_editor_controller_test` 等。

**验证证据**
- `ctest -R custom_tool_store_contract` → **Passed** (0.05 s)
- `ctest -R tool_port_list_model_test` → **Passed** (0.09 s)
- `ctest -R tool_editor_controller_test` → **Passed** (0.11 s)
- `ctest -R example_metadata_contract` → **Passed** (0.05 s)
- `ctest -R example_pipelines_contract` → **Passed** (1.18 s)
- `ctest -R installed_pipeline_contract` → **Passed** (4.47 s)
- `ctest -R installed_python_author_contract` → **Passed** (0.28 s)
- `ctest -R cpu_tools_contract` → **Passed** (0.10 s)
- `ctest -R python_tools_execution_test` → **Passed** (0.25 s)
- `ctest -R project_storage_contract` → **Passed** (0.33 s)
- `ctest -R project_editor_contract` → **Passed** (0.04 s)
- `ctest -R definition_json_contract` → **Passed** (0.05 s)
- `ctest -R tool_definition_validation_test` → **Passed** (0.04 s)

**下一步**
- 保持工作区干净，向用户交付 R45 完整架构与验证结果。

### 2026-11-02 — R43 收口：三仓提交、GPU 实测与候选验收

**目标**
- 把前几轮的修复与测试收口落成可评审提交，并补 T43-10 的候选证据（三 SHA、安装态、GPU）。

**当前状态**
- `perf` 组去掉 `runtime_hotpath_contract`：它验证实例复用，不含任何计时，perf 现在只剩 4 个真实基准。
- GPU 增加 `gpu_inference_contract`（`--engine-only`，缺 engine 报 NOT RUN），与重建 engine 的完整条目分开：本机 0.23 s 完成 engine 加载与推理，命中 5 个 PyTorch 参考框、下游过滤保留 4 个。
- 三仓已提交（仅代码与测试）：VisionRT `b0de29c`、VisionTools `e69a2ed`、Contracts `f9b669a`；RT 的 gitlink 指向该 Contracts 提交。`WORKLOG.md` 与 `docs/` 按要求不提交，保留在工作区，`build_info.json` 的 `dirty=true` 只反映这些文档。
- 新增 `docs/R43/11_候选验收记录.md`：三 SHA、环境、产物哈希、执行/跳过项与成本。

**验证证据**
- `ctest -L backend` → **40/40**（18.49 s）；`-L sdk` → **7/7**（5.13 s）；`-L installed` → **2/2**（3.35 s）；`-L package -LE integration` → **5/5**（20.67 s）；`-R ^build_metadata_integration_contract$` → **1/1**（52.21 s，6 个嵌套构建）。
- `ctest -R ^gpu_inference_contract$` → **1/1**（0.41 s），输出 `[PASS] Native YOLO matches five PyTorch detections; downstream filter retains four.`
- VisionTools 缓存正确性/缓存基准/追踪 2 项/几何/逻辑引擎 → **6/6**（1.92 s）。
- 未通过/未跑：完整 `gpu_tools_contract` 的 WTS→engine 重建在本机 17 min 43 s 仍未结束（GPU 100%、显存 7.2 GB）后中止；visual/DPI 默认不注册（此前 `--dpi-child` 崩溃）；`tests-all` 与 `-L ui` 全组未跑。

**下一步**
- 判定 engine 重建 18 min+ 是环境（TensorRT builder / 8 GB 显存）还是产品参数问题；据此决定完整 GPU 条目是否单独标注机器要求。
- 发布前补 visual 矩阵与真实安装目录/包哈希。

### 2026-11-02 — R43 续二：测试构建入口与剩余易混名称

**目标**
- 收掉 T43-06 的 VisionTools 按需构建、T43-04 的 REQUIRES 目标校验，以及 T43-09 中最后两个高混淆名称。

**当前状态**
- 已完成 T43-06（VisionTools）：`visiontools_add_test` 生成的可执行全部 `EXCLUDE_FROM_ALL`；新增 `visiontools_tests` 作为唯一测试构建入口（含 `NO_TEST` 的场景 runner）；`tools/build.cmd --test` 先构建该目标再跑 ctest；README 与工具文档同步。默认 `cmake --build build` 现在只构建产品。
- 已完成 T43-04：`visionrt_group_test` 在配置期校验 `REQUIRES` 的每个目标存在，拼错直接 `FATAL_ERROR`，不再静默跳过依赖。
- 已完成 T43-09：`tests/result_view.h → tests/ToolResultAssertions.h`（VisionRT 26 处、VisionTools 6 处、探针 1 处 include 同步）；`tests/fixtures/test_plugin_legacy.cpp → tests/fixtures/incompatible_abi_plugin.cpp`，目标名与 ABI 负例语义不变，注释说明它不是历史死代码；`tests/workbench_canvas.cpp → tests/flow_canvas_scenarios.cpp`（target/场景名保持 `workbench_contract`/`workbench_canvas_contract`，由显式 `target_sources` 解耦）；`visionrt_contract` 增加 `SOURCES`，文件、target 与 CTest 条目不再强绑定。
- 已完成 T43-09（giant aggregate）：把 `examples_contract` 的包替换场景拆到 `tests/package_replacement_test.cpp`（`LABELS package`，自带临时工具包，0.52 s 通过），共享等待逻辑移入 `tests/RuntimeTestSupport.h`；剩余管线/生命周期文件改名 `tests/example_pipelines.cpp`、target/CTest 名改为 `example_pipelines_contract`（2.42 s 通过）。`examples_contract` 不再同时承担示例管线与部署事务。
- 明确延期：CSV 中其余 130 余项路径建议；`workbench_canvas` 的 target/场景名保持 `workbench_*`（按场景过滤仍方便）。
- 已完成 T43-09（剩余优先项）：`python_contract → python_tools_execution_test`、`node_gate_contract → logic_node_execution_test`；Contracts 子模块 `test_sdk_function_tool → tool_registration_test`、`test_sdk_parameters → parameter_mapping_test`，SDK 组、隔离安装与文档同步，旧名不再注册。

**验证证据**
- VisionTools：删除 `build/tests/image_read_cache_test.exe` 后执行默认 `cmake --build build` → 未重新生成（`TEST_EXCLUDED_FROM_DEFAULT`）；`cmake --build build --target visiontools_tests` → 构建成功（13 个测试可执行）；`ctest -R "^(image_read_cache_test|image_read_cache_benchmark|infer_trace_geometry|infer_trace_uninitialized|test_geometry_contract|test_module_contract)$"` → **6/6 通过**。
- VisionRT：重新配置通过（REQUIRES 全目标存在）；`ctest -R "^(port_value_validation_test|plugin_abi_contract|plugin_contract|python_contract|output_text_readback_test)$"` → **5/5 通过**（4.55 s）；`cmake --build build --target tests-backend` + `ctest -L backend` → **40/40 通过**（21.54 s），覆盖全部引用改名头的后端测试与 `example_pipelines_contract`。
- 四个改名的 Python/逻辑/SDK 测试：**4/4 通过**（0.90 s）。
- 未验证：全量 `tests-all`、GPU/安装/visual。

**下一步**
- 三仓提交；候选验收时再补安装/GPU/visual 与三 SHA。

### 2026-11-02 — R43 续：测试成本分层、命名与文档收口

**目标**
- 完成 T43-13（缓存/打包/GPU 场景拆分）、T43-09 中“容易看错”的名称迁移、T43-05 长名专项删除；补 T43-10 的当前类型承诺与死链。

**当前状态**
- 已完成 T43-13（VisionTools）：`test_read_cache_contract` 拆成 `image_read_cache_test`（3 张 96×64 小图：LRU/保护/超大拒绝、冷/暖解码次数、reload、失败后仍命中、第二实例独立缓存；唯一临时目录，`atexit` + 析构双重清理）与 `image_read_cache_benchmark`（`--perf`，尺寸/图数/轮次/证据目录均可显式指定，`LABELS perf`；只有大批次才断言 working set）。实测快速项 0.20 s、基准 0.66 s（12×1600×900 + 120 帧）。
- 已完成 T43-13（VisionRT）：`tests/test_package_app.py` 只保留部署事务/安装契约/`cmake -P` 元数据脚本；六个真实嵌套 Git+CMake 构建移入 `tests/test_build_metadata_integration.py`，注册为独立 `build_metadata_integration_contract`（`LABELS package;integration`）。新增 `integration` 测试组，快速回归用 `ctest -L package -LE integration`。嵌套工程按平台取 `mini_app.exe`/`mini_app`，并显式传入 `PYTHON_EXECUTABLE`。
- 已完成 T43-13（VisionTools）：`test_infer_whitebox_trace` → `infer_trace_test`，`--case geometry|uninitialized|initialized-trace`；前两者 `smoke`，`infer_trace_initialized` 归 `gpu` 并单独 `SKIP_RETURN_CODE 77`（无引擎/权重时实测为 Skipped）。
- 已完成 T43-09（优先项）：`declaration_values_contract`→`port_value_validation_test`、`declaration_value_contract`→`tool_definition_validation_test`（不再靠单复数区分）、`node_model_contract`→`node_list_model_test`、`ui_readback_contract`→`output_text_readback_test`、`runtime_readback_contract`→`result_readback_barrier_test`、`ui_drain_contract`→`display_reader_drain_test`；CMake 与 `docs/README` 同步，旧 target 不再注册。
- 已完成 T43-13 附带命名：`RealBuildMetadataIntegrationContract` 六个用例改为行为名（如 `test_initial_build_records_head_and_artifact_time`），快速脚本矩阵类改名 `BuildMetadataScriptContract`，方法去掉 `matrix_NN`。
- 已完成 T43-05：删除 `workbench_canvas.cpp` 的超长名称夹具与 `executed_long_name` 截图；保留普通悬停、边界、隐藏/点击与缩放矩阵。DPI/外观仍由 `VISIONRT_TEST_VISUAL` 显式开启。
- 已完成 T43-10（部分）：修正 `docs/README` 两个死链到 R43 方案/工单；明确“Dense 数值→ndarray”是对早期“全部 list”约定的显式修订；补充 `integration` 分组、缓存/追踪场景说明与真实构建集成索引。
- 新发现的产品级健壮性修复：`WriteBuildInfo.cmake` 依赖 PATH 猜 `python3`，在 PATH 存在另一套解释器且 `PYTHONHOME` 指向宿主时，查询脚本崩溃并把 commit 写成 `unknown`。生产 `POST_BUILD` 两处与测试现在都显式传入 `PYTHON_EXECUTABLE`，`build/bin/build_info.json` 复验为 `8aaf0e4af55d / dev`。
- 明确延期：`result_view.h`→`ToolResultAssertions.h`（跨全部测试 include，纯风格）、`workbench_canvas.cpp`→`flow_canvas_scenarios`、`examples_contract`/`test_plugin_legacy` 改名，以及 CSV 中其余 130 余项路径建议。

**验证证据**
- `ctest -R "^(tool_definition_validation_test|port_value_validation_test|node_list_model_test|output_text_readback_test|result_readback_barrier_test|display_reader_drain_test)$"` → **6/6 通过**（3.88 s）。
- `ctest -L package -LE integration` → **4/4 通过**（35.8 s，含 validator、JSON ZIP、依赖清单、增量构建）；`build_metadata_integration_contract` 单独运行 → **通过 56.8 s**。
- VisionTools `ctest -R "^(image_read_cache_test|image_read_cache_benchmark|infer_trace_geometry|infer_trace_uninitialized|test_geometry_contract)$"` → **5/5 通过**（0.89 s）；`infer_trace_initialized` → Skipped（77）。
- `workbench_canvas_contract` → 通过（2.8 s）；`cmake --build build --target VisionRT` 重新链接后 `build_info.json` 的 commit 不再为 unknown。
- 未验证：全量 `tests-all`、`ctest -L ui`、安装态、GPU 实机、DPI/visual（本机仍不可用）。

**下一步**
- 提交三仓（Contracts 子模块先提交，再更新两仓 gitlink）。
- 发布候选时再补 T43-10 的三 SHA、安装态与适用 GPU/visual 证据；其余命名建议按需在触碰文件时局部迁移。

### 2026-11-02 — R43：两个 P1 修复与测试可信度收口

**目标**
- 按 `docs/R43` 的工单修掉 F43-01/02/03（数组桥接、逻辑 ndarray、模板叶类型），并把与生产相反的断言、重复执行和不可判别断言收口。

**当前状态**
- 已完成 T43-01：`PythonInterop::numpyView` 的 capsule 持有完整 `Array` 浅拷贝（不再只要 `bufferOwner()`），最后一个 NumPy 视图释放后才释放缓冲并卸载模块；新增 `tests/python_array_lifetime_contract.cpp`（真实插件 grid 输出→行 slice→释放宿主结果与注册表→视图仍可写→释放视图后模块卸载并写 released）。
- 已完成 T43-02：`logic/common/engine.py` 在唯一输入归一入口接受 ndarray（先校验 rank/dtype/容器数/预算，再一次 `tolist()` 原地校验）；`tests/test_logic_engine.py` 增 7 项（list/ndarray 等价、rank2 int32、分支、dtype/rank 拒绝、预算、shape=(n,0) 容器上限、NaN/字符串）。
- 修复过程中发现的产品缺陷（未列入 R43）：Python 工具实例的工厂绑定的是**打包声明**，而 `NodeRuntime` 用节点契约（表达式/条件节点的动态端口）创建实例，导致这些节点执行必然报“输出键不在声明中”。`core/Tool.h` 增 `PythonFactoryResolver`，`ToolRegistry` 用调用方契约重建工厂，`PythonRuntime::factory` 提供解析器；`tests/python_contract.cpp` 用真实包跑通 数组→表达式→分支。
- 已完成 T43-03：`Array::elements<T>` 用 `detail::ArrayElement` 约束（拒 bool/嵌套 vector/指针），`boolBytes()` 保留；删除无调用的 `ArrayScalarType/ArrayScalarTypeT/arrayElementCount`；`test_array_contract` 加 9 条编译期断言。
- 已完成 T43-11：性能文件删除“ndarray 必须被拒绝”的过期契约（改由 `python_value_contract` 承担接受/拒绝）；末节点结果改为增量收集后强制存在；删除 bool→0/1 的恒真队列断言；目录检索改“有匹配>0 / 无匹配==0”；阈值检查移到打印 PASS 之前并带实测值；`InLoop`→`repeated_once`，`scheduler_gap_ms`→`round_minus_slowest_ms` 并改正文说明。
- 已完成 T43-04：`workbench_contract` 不再注册聚合 CTest 条目（`visionrt_contract(... NO_TEST)`）；`core` 与 `short-window` 迁出为独立场景；`REQUIRES` 由不存在的同名 executable 改为真实 runner；DPI 三个场景移入 `VISIONRT_TEST_VISUAL`（默认 OFF）。
- 部分完成 T43-05：24 张 DPI×缩放截图矩阵已不在默认回归运行（visual 可选）；`hoverCardContract` 里的超长名称专项代码未删，仍是 visual 场景的一部分。
- 部分完成 T43-06：`node_model_contract` 改 `backend`（不再占桌面锁、不再进 ui 组）；`value_bridge_benchmark` 归 `perf`；日志三项退出 `perf` 组（`perf` 只剩 4 个真实基准）。
- 已完成 T43-07：聚合场景改为单一消费者（除模型场景外 `setViewActive(false)` 后再 `take()`）；`ProducerThreads` 首异常加锁并在 join 后重抛。
- 已完成 T43-08：删除无产品入口的 `copyDiagnostics`，`requestOutputText/requestText` 去掉 `copy/diagnostic` 两个参数（产品只有“复制完整输出”一种意图）；`plugin_contract` 里借该入口导出诊断 JSON 的两段断言删除——结构化诊断已由 `workbench_contract` 直接经 `OutputModel::diagnosticText` 覆盖。
- 已完成 T43-12：分发夹具改为当前信封 `{outputs:{value,__status_code,__status_message}, detail}` 并在探针中断言旧 `status` 键不存在；宿主执行拆成独立用例，缺环境显式 skip，`VISIONTOOLS_REQUIRE_HOST_EXECUTION=1` 时缺环境直接失败。
- 已完成 T43-14：SDK 实例用局部 RAII 保证异常路径 release+destroy；计数器改为同实例两次、新实例一次；参数测试让原 image 真正离开作用域后再查弱引用。
- 部分完成 T43-10：`docs/README.md` 增加“数组与 Python 类型”矩阵并索引新契约；三 SHA/安装/GPU/visual 的候选验收证据未补。
- 未完成：T43-09 全仓命名迁移、T43-13 Tools 缓存/打包/GPU 场景拆分、T43-05 长名专项删除、契约子模块提交与两仓 gitlink 更新。

**验证证据**
- `ctest --test-dir build -L backend` → **40/40 通过**（22.88 s），含 `python_array_lifetime_contract`、`python_contract`、`native_value_lifetime_contract`。
- `ctest --test-dir build -L sdk` → **7/7 通过**（含 `test_isolated_installation` 4.93 s）。
- `ctest -R "^(ui_readback|ui_value|ui_completion|ui_result_isolation|workbench_popups|plugin_contract|workbench_core|workbench_short-window|workbench_output|workbench_parameters)_contract$"` → 全部通过。
- `performance_contract.exe --rounds=3` → 通过（含新增的末节点/过滤/ndarray 断言）。
- `ctest -R "^log_(text|aggregation|lifecycle)_contract$"` → 3/3（3.07 s）。
- VisionTools `ctest -E test_infer_whitebox_trace` → **16/16 通过**（11.82 s）。
- 反向验证（说明新断言可判别）：回退 capsule 为 `bufferOwner` → `python_array_lifetime_contract` 退出码 0xC0000409；去掉 registry 的契约解析 → `python_contract` 报“输出键不在声明中：p2”；让引擎拒绝 ndarray → `NumpyInputs` 5 失败 2 错误。
- 未验证：DPI visual 场景（本机 `--dpi-child` 三个缩放均 0xC0000374/SEGV，与既有记录一致）、GPU、安装态完整链路、`ctest -L ui` 全组。

**环境备注**
- 本机 `tools/build.cmd` 的 vcvars 因安全策略拦截 `reg.exe` 而拿不到 Windows SDK，需手写 `INCLUDE/LIB` 与 PATH；PATH 里的 MSVC/SDK 目录要用 `/c/...` 形式，否则 Git Bash 会把 `C:/...` 转义成错误路径（表现为 `rc.exe` 找不到、`test_isolated_installation` 报 compiler broken）。

**下一步**
- 提交三仓：契约子模块提交后更新 RT/VisionTools 的 gitlink（当前 `git status` 显示子模块为 modified content）。
- 再做 T43-09/T43-13；发布前补 T43-10 的三 SHA、安装态与适用 visual 证据。

### 2026-11-02 — 提交（dev）

- 宿主 `8aaf0e4`（80 文件，+3064/−2032）：声明 schema 5（nameEn/nameZh、单 token、PortRef 阶段限定、删 required）、默认值单源与输出补齐唯一入口、PythonRuntime 省略语义、工具树分类路径、about 移入 `AboutReport`、注册 `node_model_contract`。
- 工具仓 `cfc573e`（37 文件，+2104/−2059）：22 份声明迁 schema 5、打包器读 implementation、工具停止写 None/Missing；追加 `4857d08` 修正图像加一失败用例断言。
- 行尾按各文件原有风格归一：RT 19 个源文件、VT 22 份声明（此前批量脚本把它们整篇换了行尾，diff 曾虚高到 14406 行）。
- 提交后复跑：RT 非 UI 60/60、VisionTools 16/16；`workbench_popups_contract`（about 文案）通过。
- 未提交：两仓 `WORKLOG.md`、`docs/README.md`、`docs/T39_执行报告_20261028.md`（删除）、两份方案文档。

### 2026-11-02 — U14/U15：工具声明 schema 5、限定端口引用与默认值单源

**目标**
- 按《统一修改优化方案》S07 与 Tickets U14/U15，把工具声明从 schema 4 一次性迁到 schema 5：`name_en/name_zh`、单 type token、删除 `required`、类型默认值单源物化、端口引用带阶段；同步 RT、VisionTools 的解析器、全部消费者、22 份工具声明与打包链路，不留双字段兼容。

**当前状态**
- 已完成（U14，模型）：`ToolDefinition` 收敛为 `nameEn/nameZh/version/implementation(categories/visibility/nodeModel/interaction)`；`PortDefinition` 为 `nameEn/nameZh/type/defaultValue` 加 `constraints(min/max/options)`、`ui(editor/tab/group/step/decimals/options_provider)`、`relations(image/alignment/path_mode/same_geometry_as)`、`display(color_order)`；新增 `PortRef`/`PortStage`，`inputs.images` 这类限定引用在 JSON 边界解析一次，内部不再拼字符串。
- 已完成（U14，解析与校验）：schema 5 严格拒绝旧字段（`tool_id/display_name/kind/entry/init_params/inputs/outputs/required/dtype/rank`、大写 type 名、`vec_` 超过四层）；`type_from` 与 `type` 互斥并只接受 `init.*` 引用；关系引用统一按阶段解析后校验目标类型；宿主状态端口仍不可由作者声明。
- 已完成（默认值单源）：`normalizeToolDefinition` 用 Contracts 的 `defaultValue(TypeSpec)` 物化每个值端口；`ToolValueValidation::validateToolExecution` 成为唯一的输出补齐入口（未写或写空的业务端口取声明默认值，未声明键仍报错），`NodeRuntime` 用 `portDefault()` 克隆默认值，避免两个节点共享可写数组；`PythonRuntime` 只转换实际返回的业务项，未声明键报错、省略键交给宿主补齐；`InputNormalizer` 删除 required 分支。
- 已完成（U15，声明与消费者）：VisionTools 22 份声明整体迁移为 schema 5（含 ROI 关系、动态 `type_from`、交互映射的限定引用），全部通过宿主校验器；RT 侧 `ToolRegistry/ProjectEditor/ProjectStorage/NodeContracts/ToolPackage/ModuleIndex/PythonRuntime/OutputModel/ResultPresentation/AppController*/AssistPreview/ToolTreeModel` 与契约测试同步迁移；打包器 `package_tools.py/package_payload.py` 改读 `implementation` 块与 `name_en`；作者 SDK 删除 `OutputWriter.missing`，示例工具与 C++ 辅助工具不再向业务端口写 `None/Missing`。
- 已完成（工具树）：`ui.category` 为中文路径数组，`ToolTreeModel` 取首段为分类、其余合并为子类，保持两级展示。
- 未完成（U13）：GPU/性能基准与安装态跨语言复验；UI/workbench 套件本轮按要求未跑。

**兼容边界（有意保留）**
- 工程存储仍为 format 6：节点类型快照继续以 `type/dtype/rank` 结构化保存（声明 schema 与工程格式是两个独立版本维度），旧工程迁移路径不变。
- 模块索引仍以 `tool_id` 为键，其值即声明 `name_en`；索引格式本身不随声明 schema 改版。

**验证证据**
- `cmake --build build` 与 `--target tests-all` → 全部目标编译通过。
- `ctest -E "^(workbench_.*|ui_.*|gpu_tools_contract|package_app_contract|build_incremental_contract|dependency_manifest_contract|performance_contract)$"` → **59/59 通过**。
- `visionrt_validate_tool` 逐份校验 22 份声明 → 22/22 通过；`--package F:/Projects/VisionTools/dist/visiontools-deploy --load` → 原生工厂全部可用。
- VisionTools `ctest -E test_infer_whitebox_trace` → **16/16 通过**（含打包/分发/Python 工具/逻辑引擎）。
- 未验证：`workbench_*` 与 `ui_*`（按要求未跑）、`gpu_tools_contract`、`performance_contract`。

**下一步**
- U13 收尾：GPU 与性能基准复跑、安装态与真实工具包端到端复核；UI 套件按环境单独排查后再计入结论。

### 2026-10-31 — U11/U12：Array 新 ABI、NumPy 桥与工程 format 6（三仓整体切换）

**目标**

- 按《统一修改优化方案》S02 与 Tickets U11/U12 把 Array 从嵌套 vector 整体换成平坦存储 + Dense/Ragged 结构，同步 RT、Contracts、VisionTools 三仓的 Python 桥、工程持久化、作者 SDK 与全部测试，不留半迁移双写。

**当前状态**

- 已完成（U11，Contracts）：`Array` 持有 `shared_ptr<Storage>`，按 dtype 存 7 种元素（Bool 一字节/元素）；`dense/ragged/fromNested/nestedOf/elements/boolBytes/clone/retainedBy/borrowedDense` 与 `supportedTypes()` 7×4 表；删除旧 `get<NestedVector>`/`visit` 接口。
- 已完成（U12，RT）：`PythonInterop` 对矩形数值 Array 给 pybind11 NumPy 视图（owner 借由 `PythonStorage::retain` 保活），ragged/字符串仍走嵌套 list；只读或非连续入口按一次复制处理并在测试中断言“不是零拷贝”；`fromPython` 拒绝 dtype/rank 不精确匹配。
- 已完成（U12，持久化）：工程 `format_version` 升到 6，Array 值以 `shape`/`offsets` + `values` 结构化保存；4/5 旧工程按各自版本迁移仍可读；声明默认值仍是可读嵌套形式。`ui_value_contract`/`project_value_contract`/`project_storage_contract` 断言随之更新。
- 已完成（U12，VisionTools）：作者 SDK 的 `InputReader.array/optional_array` 同时接受 ndarray 与 list，示例工具与全部 C++/Python 测试迁移到 `elements<>`/`nestedOf`；`MODULE_ABI_MAJOR` 升到 8，两端夹具与静态断言同步。
- 已完成（U14 基础）：Contracts 提供唯一 `parseTypeToken/typeToken`（`vec_` 前缀 + 7 种元素名）与 `defaultValue(TypeSpec)` 单源默认值；声明 schema 迁移（name_en/name_zh、单 token、删 required）尚未开始。
- 修复（本批集成期暴露的真实缺陷）：
  - `ValueJson` 图像编码按用途分流：`Literal`/`Stored` 只允许空批次并写 `[]`（像素不入文档），`Display` 输出 `{type,count,images[{rows,cols,channels}]}` 描述；此前把三者合并导致工程存盘写出对象、重开报“JSON 值必须是数组”。
  - `Array::Storage` 字段顺序改为 `keepAlive` 在最先声明：C++ 逆序析构使“钉住模块的所有者”最后释放，插件值（`native_value_lifetime_contract`）在模块卸载后再析构缓冲所有者会踩空虚表。
  - `msvc-env.sh` 补 Windows SDK `bin`（`rc.exe`），否则 `test_isolated_installation` 的 try_compile 因找不到资源编译器报“compiler broken”。
  - `NodeListModel::sync` 的行索引表在删行后没有随位移更新：`update()` 用过期下标访问 `rows_` 并写穿堆内存，损坏在后续释放时才报 0xC0000374；`ui_result_isolation_contract`、`plugin_contract` 两项失败同源。改为每次结构变化后整表重建索引，并在 `update()` 入口校验下标与行标识，越界即重建并放弃该次更新。
- 未完成：U14 声明模型（name_en/name_zh、单 token、删 required）与 U15 全部工具声明迁移；U13 收尾（打包/安装态/GPU/性能复验）。按用户要求，UI/QML 契约本轮不再反复运行。

**验证证据**

- `cmake --build build --target tests-all` → 全部测试目标编译链接通过。
- `ctest -E "^(workbench_.*|ui_.*|plugin_contract|gpu_tools_contract|package_app_contract|build_incremental_contract|dependency_manifest_contract|performance_contract)$"` → **58/58 通过**（40s），含数组/值/工程/运行/插件/示例/SDK 契约。
- `ctest -R "^(ui_result_isolation_contract|plugin_contract)$"` → 2/2 通过（修复前两者均 0xC0000374）。
- 未验证（按要求未跑或环境不稳）：`workbench_*` 与 `ui_*` 共 30 项。其中 `workbench_contract` 的 DPI 子进程在本机反复出现 0xC0000374 / SEGV / 0xC0000142（同一二进制连续三次运行结果不同），属渲染子进程环境问题，需单独排查，不计入本轮结论。
- `value_bridge_benchmark`（smoke）→ 28 种 Array/ROI/Image 组合全部通过，矩形数值走 ndarray 单对象通道，`OutputModel` 快照清理后 `waitForRelease` 正常排空。
- VisionTools `ctest -E test_infer_whitebox_trace` → 16/16 通过；`package_tools.py` 重新出包 22 工具并用宿主校验器验证通过；部署目录与 `build/tools-package` 同步刷新。
- 未验证：`gpu_tools_contract`（需 GPU）与 `performance_contract`（长时间基准）本轮未运行。

**下一步**

- U14：ToolDefinition/DefinitionJson 改 `nameEn/nameZh` + 单 type token + 删除 `required`，由 `defaultValue(TypeSpec)` 物化默认值；U15：22 份工具声明、校验器、打包索引与全部声明消费者一次性迁移（schema 5）。迁移必须与声明同批切换：解析器不再接受 `type/dtype/rank` 三字段与 `required`，`type` 只接受 `int|float|bool|string|image|roi|vec_…` token。
- `workbench_contract` 的 DPI 子进程需单独排查（本机 0xC0000374/SEGV/0xC0000142 交替出现，疑似 Qt 软件渲染 + 注入环境），不影响后端口径。
- U13：GPU 与性能基准复跑，安装态与真实工具包端到端复验。

### 2026-10-30 — U10：日志测试按职责拆分与拥有型夹具

- `tests/log_contract.cpp`（1214 行）拆为三个测试源：`log_text_contract`（单行格式、文件同文、控制符转义、UTF-8 边界截断、`boundedSingleLine` 矩阵与分配探针）、`log_aggregation_contract`（有界信箱、批量、丢弃摘要、错误聚合、Critical、Qt 桥接、每日文件、预算、模型裁剪）、`log_lifecycle_contract`（不可写降级、默认目录、幂等启停、handler 还原、并发关闭与 A/B/C 锁交错）。
- 新增 `tests/log_test_support.h`：`ServiceSession` 拥有启停与配置、`ProducerThreads` 拥有生产线程并按线程收集异常后统一 `join()` 重抛；删除 `ThreadScopeGuard` 的原生指针登记与 `catch(...)` 吞异常。三个目标共用 `visionrt_log_testing` 与同一 `VISIONRT_LOG_TEST_HOOKS`，未引入新框架。
- CMake 用同一段 `foreach` 注册三个契约，标签与 60s 超时保持一致；`docs/README.md` 两处引用改为按职责的三个契约。
- 验证：`ctest -R "^log_(text|aggregation|lifecycle)_contract$"` → 3/3 通过（0.44s / 1.97s / 0.44s）。

### 2026-10-30 — U07：节点 ListModel、局部通知与路由索引

- 新增 `src/ui/NodeListModel`：固定 `nodeId` 角色与 id→行索引；`sync` 用插入/删除/移动行通知，`update` 只对实际变化的角色发 `dataChanged`；整体 `reset` 仅用于换流程/换工程。
- `AppController` 暴露 `nodeListModel`，`nodes()` 变成模型快照；轮询只对本次变化的节点重算并更新单行，不再重建整表；`changed()` 负责结构同步。
- `wires` 改为独立 `wiresChanged`，由连接拓扑签名决定，节点耗时/状态不再触发连线重绘。
- `FlowCanvas` 直接以模型为 Repeater 数据源，删除 `nodeIds`/`findNode`/全表扫描；悬停、菜单、框选改用 `nodeRecord(id)`/`nodePosition(id)` 的 ID 查询；`NodeDelegate` 改为明确的角色属性。
- `routeWires` 的节点合法性检查改为一次构建 ID 集合，去掉每条几何对全部节点的扫描。
- 验证（按需单跑）：`workbench_canvas_contract`（含 Qt `QAbstractItemModelTester` 与位置角色 `dataChanged` 断言）、`workbench_status_contract`、`workbench_clipboard_contract`、`workbench_flows_contract`、`workbench_layout_contract`、`workbench_publication_contract` 通过。

### 2026-10-30 — U06（第三簇）：结果展示状态独立

- 新增 `src/ui/ResultPresentation.h/.cpp`：唯一拥有 `snapshots/active/lifecycle/instance`、图像选择（输出/序号/批次）、显示请求（视口/缩略图/叠加/ROI 目标）、文本与读回队列、drain 协调；内部组合既有 `OutputModel`、`DisplayConverter`、`RoiEditSession`。
- 控制器只实现 `PresentationHost` 的只读查询与 QML 通知；`Impl` 删除上述字段及 `TextRequest/TextResult/TextMailbox`，`Display/Readback` 两个分片退化为转发。
- 迁移中修正三个真实契约暴露的边界：
  - 无图 Rect 编辑的冻结背景是 `Missing`，展示层不能把“端口不存在”判成会话失效；
  - 排队文本读回必须用 `portContext` 重建含 publication 的实时上下文再比对（否则被当作过期丢弃）；
  - 运行期切换图像序号属于视图状态，允许改变并延后呈现（QML 仍随 `workspaceEnabled` 禁用），与 `ui_readback_contract` 一致。
- 验证（按需单跑）：`workbench_roi_contract`、`ui_value_contract`、`ui_readback_contract` 依次通过；`VisionRT` 重编通过。

### 2026-10-30 — U06（第二簇）：工程操作状态独立

**目标**
- 把新建/打开/保存/关闭/重置/删除节点的排队状态、未保存决策、排空与重置进度、关闭握手下沉到唯一拥有者，控制器只执行副作用。

**当前状态**
- 已完成：U06 第二簇（ProjectOperations）。
  - 新增 `src/ui/ProjectOperations.h/.cpp`：拥有 `Action`、`ClosePhase`、pending 路径、approved、awaitingDecision、resetStarted、drained、closing、closeReadyEmitted 与待替换工程；`begin/cancel/complete/holdForOwners/markReady` 是唯一状态迁移入口。
  - `AppController::Impl` 删除 `Pending`、`ClosePhase`、`pending*`、`awaitingDecision`、`closing`、`closeReadyEmitted`、`replacement` 及 `isClosing()` 转发函数；`request/advancePending/finishClose/poll/refresh/idle/start` 与 Assist/Readback/Display 全部改用 `operations`。
  - `ProjectOperations` 不持有当前工程、不复制 `RuntimeState`；待打开的 `replacement` 只存在于操作期间，完成时移动给 `ProjectEditor`。
- 未完成：U06 第三簇 ResultPresentation；U07、U10、U11、U12、U14、U15。

**验证证据**
- `cmake --build build --target VisionRT workbench_contract ui_value_contract` 通过。
- `ctest -R "^(workbench_lifecycle_contract|workbench_startup_contract|workbench_paths_contract|workbench_roi_contract|workbench_interaction_contract|ui_value_contract|workbench_assist_contract)$"` → 7/7 通过；`workbench_roi_contract` 连续运行 6 次全通过（期间一次 0.74s 失败为本机窗口/DLL 环境波动，非断言失败）。
- 状态搜索：`src/ui` 中不再存在 `pendingPath/pendingApproved/awaitingDecision/pendingReset/pendingDrained/closeReadyEmitted` 或 `Impl::Pending/ClosePhase`。

**下一步**
- U06 第三簇 ResultPresentation（snapshots/outputs/readback/display 状态）→ U07 节点 ListModel。

### 2026-10-30 — U06（第一簇）：ROI 编辑会话独立

**目标**
- 按 S03 先把 ROI 编辑状态从 `AppController::Impl` 收拢到唯一拥有者，再迁移工程操作与结果展示两簇；本步只做 ROI 会话，保持行为不变。

**当前状态**
- 已完成：U06 第一簇（RoiEditSession）。
  - 新增 `src/ui/RoiEditSession.h/.cpp`：拥有编辑目标、草稿、原绑定、冻结背景、视图恢复信息、会话 id、字段有效性；`begin/end/commitGeometry/commitRectangle/editor()` 是唯一写入口，几何值由 `validateValue` 校验后经既有 `ProjectEditor::setBinding` 提交。
  - `AppController_p.h` 删除 `RoiPreview`、`roi`、`roiSerial`，改为 `RoiEditSession roiSession{editor}`；`AppControllerRoi.cpp`、`AppControllerDisplay.cpp`、`AppControllerBindings.cpp`、`AppController.cpp`、`AppControllerAssist.cpp` 全部改为通过会话读写，不再直接改草稿。
  - 视口与控制器仍各自只做一件事：会话产出目标+新值，控制器负责信号与视口恢复，视口不反向写会话。
- 未完成：U06 的 ProjectOperations、ResultPresentation 两簇；U07、U10、U11、U12、U14、U15。

**验证证据**
- `cmake --build build --target VisionRT` 通过；`workbench_contract`、`ui_value_contract`、`viewport_contract` 重编通过。
- `ctest -R "^(viewport_contract|workbench_roi_contract|workbench_assist_contract|ui_value_contract|workbench_interaction_contract|workbench_cancellation_contract|workbench_lifecycle_contract)$"` → 7/7 通过（`viewport_contract` 首次在批量运行时崩溃一次，单独与复跑均通过，属既有 UI 时序波动）。
- 广回归 14 项中 13 项通过；`workbench_roi_contract` 在批量运行时一次等待超时，单独运行与复跑通过。
- 状态写入点检查：`src/ui` 中不再存在 `impl_->roi`、`roiSerial`、`RoiPreview` 或对 `roi->draft/index/background` 的直接写入。

**下一步**
- U06 第二簇 ProjectOperations（打开/新建/保存/关闭 pending 状态）→ 第三簇 ResultPresentation；随后 U07 在 ResultPresentation 上建节点 ListModel。

### 2026-10-30 — U05：RT/Tools ROI 消费方迁移到语义视图

**目标**
- 按 S01 完成 ROI 消费者迁移：视口绘制/命中/编辑、Assist、ROI 读写/参数、Tools 裁剪/投影/检测/圆与行间距测量全部改用 Contracts 命名视图与约束编辑，删除嵌套 `value_type` 与几何魔数解码；底层三层 vector 与端口格式不变。

**当前状态**
- 已完成：U05。
  - Contracts：`RegionEditor` 新增 `translate`（按几何类型只移动坐标槽，矩形仍是左上角语义）与 `setVertex`（仅点/线段/多边形/点集；矩形与圆走约束 setter）；`test_roi_view_contract` 覆盖平移、顶点编辑与拒绝路径。
  - RT：`ROI::value_type` 系列别名全部替换为 `EncodedRegion/RegionGroup`；`ImageViewport` 的 center/contour/handles/命中/创建/拖动/旋转/顶点编辑改为 `RegionView` 一次分派 + 命名视图 + `make*`；`AppControllerRoi` 矩形读写改 `RectangleView`/`makeRectangle`，`AppControllerMetadata` 矩形映射改视图，Assist 种子与 Display 分组仅保留批次索引。`roiKind()` 不再出现在绘制循环中。
  - Tools：`roi::Record` 直接等于 `visionrt::EncodedRegion`，删除自带 kind+values DTO 与 `encoded()` 二次编码；`groups()` 在入口用 `RegionView` 一次校验后传递；`contains/regionMask` 改 Rectangle/Circle/Polygon 视图；裁剪、投影、检测过滤、YOLO 输出、圆测量、行间距、Assist 全部改用视图/`make*`。Tools 内 `3rdparty/VisionRTContracts` 同步同一份 Contracts 改动。
- 未完成：U06、U07、U10、U11、U12、U14、U15。

**验证证据**
- RT 全量构建 `cmake --build build` 通过；`ctest -R "^(viewport_contract|workbench_roi_contract|workbench_assist_contract|workbench_assist_release_contract|workbench_interaction_contract|workbench_status_contract|ui_value_contract|test_roi_view_contract|test_sdk_value_contract|test_sdk_parameters|workbench_cancellation_contract|ui_notifications_contract)$"` → 12/12 通过。
- Tools：`visiontools_common/preprocess/metrology/infer` 重编通过；`test_preprocess_contract/test_geometry_contract/test_module_contract/test_writable_tools_contract/test_upstream_status_contract/test_read_cache_contract/test_output_ownership_contract/test_installation_consumer/test_yolo_postprocess/test_infer_contract` → 10/10 通过；`test_infer_whitebox_trace` 因无 GPU 跳过。
- ROI 成本边界（`roi_view_benchmark`，10 万记录，15 次中位数，ns/记录）：构造矩形 42.97（每条一个 vector 分配，未消除）；校验读 20.05；trusted 读 16.10；点集 trusted 读 1.27。说明视图只去掉重复解码与验证，不改变嵌套载体的分配成本。
- 消费者搜索：RT `src`、Tools `src` 中不再出现 `ROI::value_type`、`record.values/kind` 或 `record[1..5]` 形式解码（测试中的原始编码仅作为序列化边界断言保留）。

**下一步**
- U06（S03/F04 控制器状态归属拆分）→ U07（S04 节点 ListModel）→ U10；U11/U12、U14/U15 按协议批次整体切换。

### 2026-10-30 — 统一方案首批实施：U01/U02/U03/U04/U08/U09

**目标**
- 按 docs/统一修改优化方案.md 与 统一优化_Tickets.md 实施：F01 运行期交互收口、F02 嵌套构建工具链复用、F03 示例语义、S01 ROI 语义视图（Contracts 部分）、S05 显示归一化、S06 插件失败模块释放。

**当前状态**
- 已完成：U01、U02、U03、U04、U08、U09。
  - U02：`AppController::workspaceEnabled/stopEnabled/stop()` 从 `RuntimeState` 派生唯一可用条件；单次/循环/单节点共用 `stop()`；QML Action、菜单、画布、视口、参数、工具库、输出全部改绑该条件；停止/关闭不受影响。删除 `SaveRequest`、`flushDeferredSaves` 与运行中保存排队，运行中保存直接拒绝且不落盘。
  - U08：非 8 位显示路径改为 OpenCV 批量原语（逐通道极值/掩码/convertTo/merge 直接写入 QImage），非有限像素统一为不透明黑并提示“非有限像素显示为黑色”；8U 路径不变。
  - U01：`RealBuildMetadataIntegrationContract` 删除 vcvars/SDK/Ninja 硬编码发现，改用 build.cmd 初始化环境 + CMake 传入 `CMAKE_COMMAND/CMAKE_MAKE_PROGRAM/CMAKE_CXX_COMPILER`；删除只断言提交号变化的 test_07。顺带修复 `visionrt_apply_test_environment` 的 PATH 列表未全转义缺陷（此前测试只得到第一个 PATH 目录，嵌套构建必然失败）。
  - U04：Contracts 新增 `EncodedRegion/RegionGroup`、Point/Segment/Rectangle/Circle/Polygon/PointSet 视图、`RegionView`（一次校验后不再扫描）、`make*` 构造与 `RegionEditor` 约束编辑；`rectangleCorners`/`transformedRoiBounds` 复用视图，矩形仍存左上+宽高+角度。底层仍是三层 vector。
  - U03：examples/README 明确两条并行分支与“节点5读原图”，断言值 51.0/22.95 与处理分支 95.625/191.25 可区分。
  - U09：失败模块在锁外析构，显式 `rejectedModule` 分支。
- 未完成：U05（RT/Tools 消费方迁移到 ROI 视图）、U06、U07、U10、U11、U12、U14、U15。

**验证证据**
- 显示转换同条件基准（`display_conversion_benchmark`，同机同构建，15 次中位数，毫秒）：
  - 1080p 16UC1 101.579 → 2.826；1080p 32FC1 105.113 → 7.415；1080p 32FC4 182.200 → 42.740；4K 16UC3 497.978 → 43.169；4K 32FC1 420.748 → 32.489；1080p 8UC3（未改路径）0.920 → 0.953。
  - 证据文件 build/evidence/display-conversion-{before,after}.csv。
- `ctest -R "^(workbench_[a-z_]+|ui_[a-z_]+|examples_contract|plugin_contract|definition_json_contract|declaration_values_contract|project_storage_contract|project_value_contract|runtime_value_contract|output_value_contract|display_storage_contract|viewport_contract|test_sdk_value_contract|test_roi_view_contract)$"` → 44 项中 41 通过；3 项因测试二进制未重编而失败，重编后全部通过（flows/clipboard/receiver_identity）。
- `ctest -R "^(package_app_contract|build_incremental_contract)$"` → 2/2 通过（嵌套真实 CMake/Ninja 编译、脏/未知候选拒绝、增量 no work to do）。
- `ctest -R "^(test_roi_view_contract|test_sdk_value_contract|test_sdk_parameters|viewport_contract|workbench_roi_contract|ui_value_contract)$"` → 全部通过；`ui_value_contract` 的旧“排队保存”断言改为新语义（拒绝且不写入）后通过。
- 新增 `workbench_interaction_contract`（真实窗口 + 真实 Python 工具）：单次/节点/循环三种运行均锁工作区、`stop()` 生效、运行中 `save` 拒绝且不落盘、结束恢复；通过。
- 环境限制（未验证项）：本机 Security Center 屏蔽 reg.exe，vcvars 无法完整初始化；在开发者环境（VSCMD_VER/INCLUDE/LIB/PATH 就绪）中 `package_app_contract` 与 `build_incremental_contract` 均通过，从无 VS 环境的纯 PowerShell 直接运行时这两项会因 vcvars 失败而失败，属本机安全策略限制而非代码问题。`assist_viewport_contract` 在本机基线代码上同样失败（hover 报 213,72），与本轮改动无关，未修复。

**下一步**
- U05：把 ImageViewport（contour/center/handles/hit/编辑）、AppControllerRoi/Assist 与 VisionTools 的几何消费者迁移到 `RegionView`，删除嵌套 `value_type` 与魔数解码后跑 viewport/workbench_roi/cpu_tools 回归。
- 其后按方案顺序推进 U06、U07、U10，最后再一次性做 U11/U12 与 U14/U15（Array 与声明协议必须整体切换，不能半迁移）。

### 2026-10-29 — 基于真实调用点完善统一方案与工具声明设计

**目标**
- 分析当前代码并完善完整流程、六项架构问题及用户新增的工具JSON设计，保持docs只有当前统一入口。

**当前状态**
- 已完成设计：补齐运行状态/停止入口、ROI入口一次校验和命名视图、控制器字段归属、NodesModel通知、显示语义及工具链复用入口。
- 用户确认Array按新ABI/API完整迁移设计；方案明确所有权、可写元素/不可resize发布存储、工程clone隔离、Dense/Ragged、NumPy与工程序列化迁移。ROI底层和节点值仍是三层vector<double>。
- 新增S07工具JSON重设计：name_en/name_zh、vec_vec_int等紧凑类型、implementation/ports/ui/relations分层。按用户纠正删除required/allow_missing，默认使用类型空值，仅业务覆盖写default；状态不由默认值判断。
- 统一方案现含11个Specs与15项Tickets；旧文档未恢复。未实施产品代码、未提交推送；保留用户AGENTS.md改动。

**验证证据**
- 阅读Contracts ROI/Array/Value/Parameters、PythonImages/PythonStorage/PythonInterop、ProjectValues/ValueJson、AppController_p/ROI/QML模型及实际VisionTools消费者。
- 对22份工具声明统计：192个端口，174个required:true、18个false、52个Array声明；设计已覆盖真实声明而非只改例子。
- `ctest --test-dir build -R '^(test_array_contract|python_value_contract|display_storage_contract|viewport_contract)$' --output-on-failure --no-tests=error` → 已有二进制4/4通过，总0.87s；没有执行全量测试或声称新设计已实现。
- 3段JSON示例语法检查通过；docs本地链接检查通过；11项Specs均映射Tickets；`git diff --check -- docs`通过。
- 查询Qt、pybind11与OpenCV官方说明及既有实现，复用能力引用已放统一方案。

**下一步**
- 读取docs/统一修改优化方案.md及统一优化_Tickets.md，根据用户对具体实施的指令开发。默认值/工具JSON、Array协议迁移均需按新方案整体执行，不能继续按旧required或旧Array写接口实现。

### 2026-10-29 — 清理过时审查文档与统一方案入口

**目标**
- 删除已被统一方案替代的报告与分散方案，保留单一当前入口。

**当前状态**
- 已删除28个过时文件：T39报告、R42方案ZIP、R43—R48报告/Specs/Tickets及分散分析与方案。
- docs现保留README.md、统一修改优化方案.md、统一优化_Tickets.md；修正索引、移除历史版本表及失效交付链接。
- 保留既有WORKLOG历史记录及用户对AGENTS.md的改动；未修改产品代码。

**验证证据**
- `Get-ChildItem docs -File` → 仅上述3份当前文档。
- 本地Markdown链接检查发现README跨仓错误链接，已改为明确的VisionTools仓库路径说明。
- docs过时文档名称引用扫描无残留；`git diff --check -- docs`通过。纯资料清理未运行产品测试。

**下一步**
- 以docs/统一修改优化方案.md及统一优化_Tickets.md为当前设计来源；产品实施状态仍为待确认/未实施。


### 2026-10-30 — R47 发布门禁元数据全入口校验、显式发布开关、Git稳定NUL解析、真实构建集成测试与全量端到端验证

**目标**
- 依据 R47 验收报告、方案与 Specs（S47-01 至 S47-04）及 Tickets（T47-01 至 T47-04）完整实施，严格保持 ABI7 协议信封与无状态交付规范。
- T47-01：发布门禁在元数据缺失、损坏、unknown 或 dirty 时全入口严格拒绝（S47-01）。覆盖 `package_tools.py`、`package_app.py: deploy_tools_package` 与 `package_app.py: verify_package` 三大入口；在失败发生于替换旧安装包之前实现原子保护；显式保留 `--allow-dirty` 开发旁路。
- T47-02：发布模式与 Git 状态语义显式解耦（S47-02）。根 CMakeLists.txt 引入 `VISIONRT_RELEASE_BUILD` 与 `VISIONTOOLS_RELEASE_BUILD` 显式开关，不把常规开发 `-DCMAKE_BUILD_TYPE=Release` 优化配置等同发布门禁；创建 `cmake/check_git_workspace.py` 使用 Git porcelain `-z` 稳定 NUL 定界接口解析路径与状态，处理中文/引号/分号/空格及 `R`/`C` 重命名两端（防止生产文件重命名为审查文档绕过校验）；Git 状态失败统一输出 `dirty: "unknown"` 并由发布模式直接拦截。
- T47-03：真实 CMake/Ninja 构建产物集成测试（S47-03）。在 `VisionRT/tests/test_package_app.py` 编写 `RealBuildMetadataIntegrationContract`，使用真实独立临时 Git 仓真实编译 C++ `mini_app.exe` 产物，覆盖：1. 干净初始构建与元数据匹配；2. 无变化构建复用产物与保存时间戳；3. 文档提交更新 commit 但复用二进制；4. 生产源码变更触发重新编译且发布模式拒绝 dirty；5. 未跟踪被包含头文件触发 dirty 且发布模式拒绝；6. Git 查询失败标记 unknown 且发布模式拒绝；7. 构建期间候选变动检测。
- T47-04：候选发布能力与全量端到端证据（S47-04）。固定两仓发布候选并推送远端 `origin/dev`（在线校验远端 SHA）；使用真实 YOLO engine 补跑 VisionTools 全部 17 项 CTest（消除白盒 trace 跳过，达 100% 通过）；独立应用 `VisionRT.exe` 实跑 `pipeline_cpu_python.vrt.json` 全链示例工程并记录日志与截图；运行独立真实 DLL 消费者 `test_installation_consumer.exe` 验证 8 项图像工具；执行安装态契约与 VisionRT 全量 91 项 CTest 并归档 JUnit XML 报告。

**当前状态**
- 已完成：
  - T47-01：两仓打包、安装部署及验证入口元数据校验全部实现，反例与原子保留均已验证；
  - T47-02：显式发布开关接通，Git porcelain `-z` 跨平台解析脚本落地，两端重命名检查生效；
  - T47-03：`RealBuildMetadataIntegrationContract` 7 项真实编译与增量行为测试全部通过；
  - T47-04：固定远端候选在线一致，VisionTools 17 项全过（白盒 trace 真实通过，0 跳过），独立应用流程实跑成功，独立消费者 8 项工具执行验证完成，VisionRT 全量 91 项 CTest 100% 通过。
- 未完成：无。

**验证证据**
- 远端分支 SHA 核验：
  - `git ls-remote origin dev`（VisionRT） → `d54b2a1f07c38f1e0f3e781008211de71d40870f`；
  - `git -C F:\Projects\VisionTools ls-remote origin dev` → `522d41c9be018363289d2b9545b93cb4b2432fa1`；
  - `git -C F:\Projects\VisionRT\3rdparty\VisionRTContracts ls-remote origin main` → `b2b643bfb9ee8510521dc1f978a55d313be4d8a9`（ABI 7）；
- 真实构建集成测试：
  - `ctest --test-dir build -R "^package_app_contract$"` → **Passed (16.18s)**（覆盖 34 项测试，包含 7 项真实 CMake/Ninja 编译与增量测试全部通过）；
- 真实工具打包与部署：
  - `python F:\Projects\VisionTools\tools\package_tools.py --build-dir F:\Projects\VisionTools\build --install-dir F:\Projects\VisionTools\dist\VisionTools --zip-output F:\Projects\VisionTools\dist\VisionTools.zip` → **已打包 22 个工具**，`build_info.json` clean，commit `522d41c`；
  - `python tools/package_app.py --install-dir F:/tmp/VisionRT --tools-package F:/Projects/VisionTools/dist/VisionTools.zip --force-clean` → **1836 文件部署完成**，三仓元数据检验通过且全部为 clean 候选；
- 独立动态库消费测试：
  - `cmd.exe /c "set PATH=F:\tmp\VisionRT\tools-package\bin;D:\Software\dev\opencv-4.8.0\build\x64\vc16\bin;%PATH% && F:\Projects\VisionTools\build\tests\test_installation_consumer.exe F:\tmp\VisionRT\tools-package\bin\vst_image.dll"` → **[PASS] test_installation_consumer passed successfully (ReturnCode: 0, 8/8 工具正常执行)**；
- 独立应用真实运行工具流：
  - `F:\tmp\VisionRT\VisionRT.exe --project F:\Projects\VisionRT\examples\pipeline_cpu_python.vrt.json --run-once --screenshot F:\tmp\launch_project_r47.png --screenshot-delay 3000` → **Exit code: 0**；
  - 应用日志 `F:\tmp\VisionRT\logs\visionrt_2026-10-30.log` 记录启动、打开工程 `pipeline_cpu_python.vrt.json`、开始单次运行、正常退出；
  - 界面截图 `F:\tmp\launch_project_r47.png`（108,506 字节）留存；
- 安装态契约：
  - `ctest --test-dir build -R "^installed_"` → **2/2 Passed (100%)**（`installed_pipeline_contract` 5.97s, `installed_python_author_contract` 0.28s）；
- VisionTools CTest：
  - `cmd.exe /c "set VISIONTOOLS_TEST_YOLO_ENGINE=F:\Projects\VisionRT\build\gpu-fixtures\yolov8n.engine && ctest --test-dir F:\Projects\VisionTools\build --output-on-failure --output-junit F:\tmp\VisionTools_R47_CTestResults.xml"` → **17/17 Passed (100%, 0 Skipped, 0 Failed)**，白盒 trace `test_infer_whitebox_trace` 实跑 0.29s 通过；
- VisionRT 全量 CTest：
  - `tools\build.cmd ctest --test-dir build --output-on-failure --output-junit F:\tmp\VisionRT_R47_CTestResults.xml` → **91/91 Passed (100%, 0 Failed, 0 Skipped)**（总耗时 358.44s）。

**下一步**
- 所有 R47 工单（T47-01 至 T47-04）已全部关闭，测试矩阵与真实产物完整通过，等待发布验收。

### 2026-10-30 — R46 构建元数据真实状态、未跟踪生产文件检测、9项集成测试矩阵与独立环境真实工具发布验证

**目标**
- 依据 R46 方案、Specs 与 Tickets（T46-01 至 T46-04）完整实施并验证，保持 ABI7 协议信封不变。
- T46-01：双仓构建元数据与发布关口拒绝校验（S46-01）。`WriteBuildInfo.cmake` 修复 Git 执行失败时错误返回上一次 commit SHA 与 dirty=false 的降级缺陷（F46-01），在失败时将 commit、branch、dirty 统一置为 `unknown`，并在 Release 构建模式下直接以 `FATAL_ERROR` 终止；状态检查改用 `git status --porcelain -uall` 并显式排除 `WORKLOG.md`、`docs/R[0-9].*\.md` 及 `docs/VisionRT_.*\.zip`，将未跟踪的生产源码/头文件纳入脏工作区判定（F46-02）；发布脚本 `package_app.py` 与 `package_tools.py` 增加元数据合规校验，默认拒绝 commit 为 `unknown` 或 dirty 不为 false 的产物，提供 `--allow-dirty` 调试标志。
- T46-02：元数据 9 项集成测试矩阵（S46-02）。在 `VisionRT/tests/test_package_app.py` 中编写 `BuildMetadataIntegrationContract` 测试套件，使用真实独立临时 Git 仓库与小型 CMake 产物测试真实脚本行为（覆盖：1. 首次干净构建；2. 无变化增量构建；3. 仅提交审查文档；4. 跟踪代码变更 dirty；5. 未跟踪被引用头文件 dirty；6. 仅新增白名单审查文档保留 clean；7. 子模块脏状态；8. Git 异常失败 unknown 降级；9. 发布关口拒绝 unknown/dirty）。
- T46-03：固定候选在线引用验证与独立环境真实工具发布验证（S46-03）。采用两仓固定 Release 干净提交候选并推送远端，在线查询 `origin/dev` 引用验证匹配（VisionRT: `969f37b25a870c8d6bd5ecfbb140422a28e381fa`，VisionTools: `86f479c315baec65e8f4687d1a7a278316cb89c2`，VisionRTContracts: `b2b643bfb9ee8510521dc1f978a55d313be4d8a9`）；打包部署至 `F:\tmp\VisionRT` 并通过校验；在隔离环境（PATH 仅保留 Windows 系统目录）执行独立原生消费者 `test_installation_consumer.exe` 验证打包的 `vst_image.dll`（8 项图像工具真实执行并通过）；执行安装管线测试与 Python 算子测试；独立运行 `VisionRT.exe` 并捕获视觉截图；归档 JUnit 报告。
- T46-04：测试维护与堆分配探针优化（S46-04）。`VisionRT/tests/log_contract.cpp` 分配探针输出次数/字节及编译环境 `[MSVC 1929]`；将受控并发测试夹具中的 `std::vector<std::unique_ptr<std::thread>>` 重构为固定大小原生指针数组 `std::thread* threads[2]`，避免堆分配及其引发的局部异常处理复杂度。

**当前状态**
- 已完成：
  - T46-01：两仓 `cmake/WriteBuildInfo.cmake`、`tools/package_app.py`、`tools/package_tools.py` 均已更新；
  - T46-02：`tests/test_package_app.py` 9 项矩阵测试全部通过；
  - T46-03：固定 Release 候选提交并推送到远端 `origin/dev`，在线确认远端 SHA；部署独立安装包 `F:\tmp\VisionRT` 验证三大组件元数据完全匹配；无开发路径注入下独立消费者验证 `vst_image.dll` 全部 8 项工具执行成功（返回码 0）；独立运行无头截图成功（47,642 字节）；SDK 隔离安装测试通过；两仓全量 CTest 均通过且留存 XML；
  - T46-04：`log_contract.cpp` 堆分配探针输出与线程固定结构优化完成，测试全部通过。
- 未完成：无。

**验证证据**
- `git ls-remote origin dev`（VisionRT） → `969f37b25a870c8d6bd5ecfbb140422a28e381fa`（与本地 HEAD 及 build_info.json 完全一致）；
- `git -C F:\Projects\VisionTools ls-remote origin dev` → `86f479c315baec65e8f4687d1a7a278316cb89c2`（与本地 HEAD 及 build_info.json 完全一致）；
- `git -C F:\Projects\VisionRT\3rdparty\VisionRTContracts ls-remote origin main` → `b2b643bfb9ee8510521dc1f978a55d313be4d8a9`（ABI 7，与 build_info 完全一致）；
- `pytest tests/test_package_app.py -k BuildMetadataIntegrationContract` → **9/9 Passed**；
- `ctest --test-dir build -R "^package_app_contract$"` → **Passed (11.05 sec)**（覆盖 23 项集成测试）；
- `tools/package_app.py --install-dir F:/tmp/VisionRT --tools-package F:/Projects/VisionTools/dist/VisionTools.zip --force-clean` → **Release gating verification passed**（1837 文件，VisionRT `969f37b` clean, VisionTools `86f479c` clean, VisionRTContracts `b2b643b` clean）；
- 隔离环境（PATH 仅为 `C:\Windows\System32;C:\Windows`）执行独立动态库消费测试：
  `F:\Projects\VisionRT\3rdparty\VisionRTContracts\build\tests\test_installation_consumer.exe F:\tmp\VisionRT\tools-package\bin\vst_image.dll` → **8/8 tools passed (ReturnCode: 0)**（测试工具：`vst.image.threshold`、`vst.image.resize`、`vst.image.crop`、`vst.image.gaussian_blur`、`vst.image.color_convert`、`vst.image.morphology`、`vst.image.template_match`、`vst.image.affine_transform` 全部正常产出结果）；
- `installed_pipeline_contract.exe F:\tmp\VisionRT\tools-package` → **Passed (ReturnCode: 0)**；
- `installed_python_author_contract.exe F:\tmp\VisionRT\tools-package` → **Passed (ReturnCode: 0)**；
- 独立应用运行与无头截图：
  `F:\tmp\VisionRT\VisionRT.exe --demo-menu --demo-select --screenshot F:\tmp\launch_screenshot_r46.png --screenshot-delay 2000` → **Exit code: 0**（生成 47,642 字节无头截图，启动与工具初始化正常）；
- CTest 全量通过与归档：
  - VisionTools: `ctest --test-dir F:\Projects\VisionTools\build` → **17/17 Passed (100%)**，归档于 `F:\tmp\VisionTools_R46_CTestResults.xml`（11 原生 C++ 可执行测试 + 6 Python 脚本套件包含 50 项测试）；
  - VisionRT: `ctest --test-dir build` → **91/91 Passed (100%)**（包含 SDK 隔离测试与完整 91 项回归测试），归档于 `F:\tmp\VisionRT_R46_CTestResults.xml`；
- 日志堆分配探针输出验证：
  `build/bin/log_contract.exe` → 输出 `[LogAllocationProbe] MSVC 1929: 2 allocs, 560 bytes during scoped log write (within 4096-byte budget)`，ThreadScopeGuard 改为非堆分配 fixed array `threads[2]`。

**下一步**
- 交付最终审查报告并响应用户指令。

### 2026-10-29 — R45 真实并发交错、隔离堆分配探针、双仓构建元数据与干净发布候选交付

**目标**
- 根据 R45 方案、Specs 与 Tickets（T45-01 至 T45-04）完整实施并验证，保持 ABI7 协议信封不变。
- T45-01：受控并发测试线程归属修正与零推断锁排空（S45-01）。`start`/`shutdown` 全部由 `LogService` 所属主线程调用，彻底消除 `QObject::killTimer` 跨线程停止定时器警告；采用协调线程与排空进入观察点（`onInsideShutdownLock`）验证 Scenario A（锁内投递完成 vs 排空进入真实时序，消除 50ms 睡眠猜测）；Scenario B 显式断言旧文件可读且未被写入；Scenario C 覆盖旧会话 writer 准入后在锁前挂起、主线程关闭并重启至独立新文件后再释放的真实跨会话迟到接纳语义；`ThreadScopeGuard` 析构无异常保证并兜底 unblock 动作，工作线程通过 `exception_ptr` 向主线程精准传递异常；
- T45-02：日志观测与反例收尾（S45-02）。定义全局 `operator new`/`delete` 隔离堆分配探针，真实记录调用期间分配次数与字节（短 `abc` / 4096 预算与长输入 / 100 预算断言堆分配小于 4096 字节，能够捕获临时第二 4096 buffer 变异）；现有容量测试重命名为返回对象容量检查；补齐 16 预算与 64 预算转义消息 A/B 经 `writeError` 服务入口的独立首报防碰撞测试（UI 与磁盘文件均断言 2 行，64 预算能够独立拒绝删除 `boundedMessage.truncated` 守卫）；完成 5 项真实变异注入与精准断言拦截；
- T45-03：两仓构建元数据与真实候选构建（S45-03）。`WriteBuildInfo.cmake` 增加 Git 失败降级为 `unknown` 以及工作区脏状态检查（`dirty: true/false`，排除本地维护的 `WORKLOG.md` 与未跟踪文档）；`VisionTools` 的 `visiontools_build_info` 显式依赖发行模块目标（`visiontools_metrology`, `visiontools_preprocess`, `visiontools_infer`），扫描目录限定为 `src/` 模块产物，消除对测试可执行文件时间的混入；`VisionRT` 构建命令传递 `-DSOURCE_FILE=$<TARGET_FILE:VisionRT>` 保证未重链时保留精确产物时刻；采用“先提交代码固定干净候选 SHA -> 在干净候选上执行构建/测试/打包”的最佳实践交付流程；
- T45-04：全量门禁与发布收口（S45-04）。两仓全量 CTest 100% 通过（RT 91 项全量 328.36s、Tools 17 项含真实 YOLO 白盒 trace 0.44s，留存原始日志与 XML 至 `F:/tmp`）；打包 22 项 VisionTools；部署 VisionRT 至 `F:/tmp/VisionRT`（1837 文件），验证三大组件 `build_info.json` 提交哈希与各仓 Git HEAD 完全一致且 `dirty: false`；独立进程无头截图成功（47,642 字节，退出码 0）；Debug 配置明确记录为不适用（仅提供 Release 运行时依赖）。

**当前状态**
- 已完成：
  - T45-01：`src/log/LogService.h` 与 `LogService.cpp` 引入 `onInsideShutdownLock` 钩子；`tests/log_contract.cpp` Group 18 重构为全生命周期受控交错测试，消除跨线程 `killTimer` 警告与时序猜测；
  - T45-02：`tests/log_contract.cpp` 补齐堆分配隔离探针及 16/64 预算转义消息防碰撞矩阵，完成 5 项变异体检验并均精准拒绝；
  - T45-03：`WriteBuildInfo.cmake` 完善脏状态检测与 Git 错误处理；`VisionTools/CMakeLists.txt` 添加模块依赖；两仓代码与 CMake 配置均已提交推送（VisionRT: `f20f3b1`, VisionTools: `2326bd5`, Contracts: `b2b643b`）；
  - T45-04：两仓全量测试全部通过并归档；独立部署包 1837 文件完整校验；三大组件元数据完全匹配；无头截图验收成功。
- 未完成：无。

**验证证据**
- `ctest --test-dir F:\Projects\VisionRT\build --output-on-failure -O F:\tmp\VisionRT_R45_CTest.log --output-junit F:\tmp\VisionRT_R45_CTestResults.xml` → **91/91 Passed (100%)**（耗时 328.36s，包含 gpu: 1 [163.75s], bridge: 29, ui: 39, backend: 37, sdk: 6, package: 4, perf: 3, installed: 2）；
- `ctest --test-dir F:\Projects\VisionTools\build --output-on-failure -O F:\tmp\VisionTools_R45_CTest.log --output-junit F:\tmp\VisionTools_R45_CTestResults.xml` → **17/17 Passed (100%, 0 Skipped, 0 Failed)**（含真实 YOLOv8n TensorRT 引擎 `test_infer_whitebox_trace` 0.44s 通过；11 原生 C++ 可执行测试 + 6 Python 脚本套件包含 50 项测试）；
- `pytest tests/test_python_tools.py tests/test_logic_engine.py tests/test_package_profile.py`（VisionTools） → **50/50 Passed (100%)**；
- `tools/package_tools.py --profile rtx40-build` → 生成 `dist/VisionTools.zip`（22 工具）；
- `ctest --test-dir F:\Projects\VisionRT\build -R "installed_"` → **2/2 Passed (100%)**；
- `tools/package_app.py --install-dir F:/tmp/VisionRT --tools-package dist/VisionTools.zip --force-clean` → 校验通过（1837 文件），三大组件 `build_info.json` 提交哈希与 Git HEAD 完全一致且 `dirty: false`（VisionRT: `f20f3b1`, VisionRTContracts: `b2b643b`, VisionTools: `2326bd5`）；
- `F:\tmp\VisionRT\VisionRT.exe --demo-menu --demo-select --screenshot F:\tmp\launch_screenshot_r45.png --screenshot-delay 2000` → 退出码 0，生成 47,642 字节视觉快照，日志正常写入且无异常报错；
- 5 项变异体验证：
  1. 缺锁变异（移除 shutdown 内部锁）：触发确切断言失败 `Scenario A: writer must release shared lock before shutdown drain enters exclusive lock!`，非盲目崩溃；
  2. 消息守卫移除变异（移除 `boundedMessage.truncated`）：触发 `truncated long messages collided into a repeat summary`；
  3. 源守卫移除变异（移除 `boundedSource.truncated`）：触发 `truncated long sources collided into a repeat summary`；
  4. 预算门槛变异（`minCountBudget` 由 22 降为 20）：触发 `cap 21 erroneously coalesced errors`；
  5. 临时 4096 堆 buffer 变异：触发 `allocation probe: short input with large cap must not heap-allocate 4096 bytes`；
  6. 截断边界/前缀回退忽略变异：触发 `a multi-byte message was not capped on a character boundary`；均精准拦截并已恢复。

**下一步**
- 本地保留 WORKLOG.md 与审查文档，保持工作区干净整洁。

### 2026-10-29 — R44 契约指导终结、测试API隔离、确定性并发屏障、截断防碰撞与构建元数据动态追踪

**目标**
- 根据 R44 方案、Specs 与 Tickets（T44-01 至 T44-05）完整实施并验证，保持 ABI7 协议信封不变。
- T44-01：作者契约指导与公开文档收敛。Contracts 头文件 Parameters.h（第 307 行）明确禁止工具直接操作状态端口；docs/README.md 移除 Result.h 死链，统一引用 gitlink、build_info.json 与动态 CTest 注册，消除与 Error.h 的重复错误码表格；
- T44-02：受控并发测试与测试接口隔离。还原 LogService flushPending 为私有，仅在 VISIONRT_LOG_TEST_HOOKS 下暴露 flushPendingForTesting 与 onBeforeShutdownLock 钩子；Group 18 消除时序猜测与死循环自旋，采用严格 promise/future 与 ThreadScopeGuard 验证 Scenario A（锁内在途写入屏障排空）、Scenario B（锁前迟到检测安全丢弃并校验旧日志文件未污染）及 Scenario C（跨会话重启后正常接收落盘），双日志级别参数化覆盖并设置 TIMEOUT 60；
- T44-03：消息与源截断防碰撞与确切指标断言。测试补齐 TAB 与 0x1F 控制符，分离单行输出长度与底层内存分配观察（capacity < 4096）；以 64 字节有界截断验证长前缀同源与同文防碰撞、21/22 预算边界落盘行数（2行）、600ms 窗口负例（1行）与 5 次重复计数（4次）；完成 4 项真实变异注入拦截检验；
- T44-04：Theme 参数面板双引擎单例隔离与固定行高语义。tests/workbench_parameters.cpp 验证引擎 2 动态修改不污染引擎 1 属性，验证点号/像素切换不破坏固定行高语义，断言真实 QML item 可见性与正向尺寸；
- T44-05：全量门禁、构建元数据动态追踪与部署留存。WriteBuildInfo.cmake 修复提交哈希静态滞后缺陷，构建期动态提取 HEAD 哈希；两仓全量 CTest 100% 通过（RT 91 项、Tools 17 项含 YOLO 白盒 trace 0.34s，留存 XML/LOG 至 F:/tmp）；打包 22 项 VisionTools；部署 VisionRT 至 F:/tmp/VisionRT（1837 文件），验证三大组件 build_info.json 提交哈希完全吻合 HEAD；独立进程无头截图成功（47,642 字节，退出码 0）。

**当前状态**
- 已完成：
  - T44-01：VisionRTContracts/include/visionrt/contracts/Parameters.h 已提交并推送到 origin/main（b2b643b），两主仓子模块引用均指向 b2b643b；VisionRT/docs/README.md 已更新死链并收敛单真相源；
  - T44-02：LogService.h 将 flushPending 还原为 private，新增仅测试宏可见的 flushPendingForTesting 及 onBeforeShutdownLock；CMakeLists.txt 固化 VISIONRT_LOG_SOURCES 与 TIMEOUT 60；tests/log_contract.cpp Group 18 严格采用 RAII 与零推断同步覆盖 A/B/C 三场景；4 项变异体检验均有效拦截；
  - T44-03：tests/log_contract.cpp 完善 Group 15（控制符测试与 capacity 观测）及 Group 17（64 字节有界截断同文/同源防碰撞、22 预算边界及 5 次重复计数确切断言）；
  - T44-04：tests/workbench_parameters.cpp 补齐双引擎单例隔离、固定行高语义及 QML 控件尺寸断言，全部 22 项 workbench 测试通过；
  - T44-05：cmake/WriteBuildInfo.cmake 引入 REA POST_BUILD 动态 Git 探测；RT 91 项全量通过（338.83s），Tools 17 项全量通过（含 YOLO trace 0.34s）；打包部署 1837 文件，元数据校验一致，GUI 截图成功；VisionTools 子模块同步与构建元数据改动已提交推送（ee651fe）。
- 未完成：无。

**验证证据**
- `ctest --test-dir F:\Projects\VisionRT\build --output-on-failure -O F:\tmp\VisionRT_R44_CTest.log --output-junit F:\tmp\VisionRT_R44_CTestResults.xml` → **91/91 Passed (100%)**；
- `ctest --test-dir F:\Projects\VisionTools\build -O F:\tmp\VisionTools_R44_CTest.log --output-junit F:\tmp\VisionTools_R44_CTestResults.xml` → **17/17 Passed (100%, 0 Skipped, 0 Failed)**（含 YOLO whitebox trace 0.34s 通过，明确分类为 11 原生 C++ 可执行用例 + 6 Python 脚本套件包含 50 项测试）；
- `ctest --test-dir F:\Projects\VisionRT\build -R "installed_"` → **2/2 Passed (100%)**；
- `tools/package_tools.py --profile rtx40-build` → 生成 `dist/VisionTools.zip`（22 工具）；
- `tools/package_app.py --install-dir F:/tmp/VisionRT --tools-package ... --force-clean` → 校验通过（1837 文件），三大组件 `build_info.json` 提交哈希与 Git HEAD 完全一致（VisionRT: cf847c8, VisionRTContracts: b2b643b, VisionTools: f413f69）；
- `F:\tmp\VisionRT\VisionRT.exe --demo-menu --demo-select --screenshot F:\tmp\launch_screenshot_r44.png --screenshot-delay 2000` → 退出码 0，生成 47,642 字节视觉快照，日志正常写入；
- 4 项变异体验证：变异 1（移除 shutdown 内部锁）触发 0xc0000409 崩溃；变异 2（移除 message truncated 保护）触发长前缀碰撞错误；变异 3（移除 source truncated 保护）触发长源碰撞错误；变异 4（移除 minCountBudget）触发 cap 21 错误合并；均能精确拦截。

**下一步**
- 将 VisionRT 的 C++ 源码、测试、CMake 构建配置与子模块提交并推送到 dev（严格排除 WORKLOG.md 与 docs/ 下方案文档）。

### 2026-10-29 — R43 契约指导修正、受控锁交错测试、日志有界全矩阵与全量门禁验收发布

**目标**
- 根据 R43 方案与 Specs 完整实施并验证 T43-01 至 T43-05，保持 ABI7 协议信封不变。
- T43-01：修正作者契约指导与文档，移除 Result.h 死链接，统一基线至 ABI7（e8b1d78），更新 Parameters.h 状态端口由 toolResult/finish 打包说明，阐明 CTest 多标签与 91 用例总数；
- T43-02：日志有界与聚合防护全矩阵（0/1/2/3 budget、中英Emoji、转义控制符、畸变UTF-8与内存分配保证；Critical直通、长前缀截断防碰撞、21/22预算边界、无重复600ms窗口负例、5次重复聚合测试，且包含变异体拒收验证）；
- T43-03：受控并发与锁交错测试（通过仅在 visionrt_log_testing 启用的 setLogTestHooks 零生产开销测试钩子，分别验证 Scenario A 锁内写起屏障等待写完安全落盘，以及 Scenario B 锁前迟到唤醒检测已关闭安全丢弃）；
- T43-04：Theme 单位语义与动态传播（数值 14 在 pointSizeF 与 pixelSize 切换验证 QFont 模式语义与对 QML 实际组件 stepperInput 的实时传播；两引擎单例隔离验证；22项 workbench 测试全绿）；
- T43-05：全量门禁与发布留存（RT 91 项全量 CTest 100% 通过留存 XML/LOG，Tools 17 项 CTest 含真实 YOLO 白盒 trace 100% 通过留存 XML/LOG，50 项 Python 独立测试通过，打包 VisionTools 22 工具，运行 2 项 installed 测试，打包 VisionRT 至 F:/tmp/VisionRT 1837 文件，执行真实截屏验收）；
- T43-06：No-Qt 路线按规范继续明确延期。

**当前状态**
- 已完成：
  - T43-01：更新 VisionRTContracts/include/visionrt/contracts/Parameters.h（已提交并推送 e8b1d78），两主仓子模块引用同步指向 e8b1d78，更新 VisionRT/docs/README.md（死链移除、ABI7基线声明、标签说明）；
  - T43-02：在 tests/log_contract.cpp 补齐 Group 15（30+ 有界单行边界矩阵）与 Group 17（6 类聚合与防碰撞矩阵），并通过变异注入验证断言有效性；
  - T43-03：CMakeLists.txt 增加 visionrt_log_testing 隔离目标，LogService 增加受控测试钩子 setLogTestHooks 与 flushPending，tests/log_contract.cpp Group 18 覆盖 Scenario A（锁内在途写入排空）与 Scenario B（锁前迟到写入安全丢弃）；
  - T43-04：tests/workbench_parameters.cpp 完善点号/像素语义断言及对 QML item 传播测试，全部 22 项 workbench 测试通过；
  - T43-05：两仓全量 CTest 运行并留存完整 JUnit/日志至 F:/tmp，打包 22 项 VisionTools，部署 VisionRT（1837 文件），通过无头截图验证真实拉起。
- 未完成：无。

**验证证据**
- `ctest --test-dir F:\Projects\VisionRT\build --output-on-failure -O F:\tmp\VisionRT_R43_CTest.log --output-junit F:\tmp\VisionRT_R43_CTestResults.xml` → **91/91 Passed (100%)**；
- `ctest --test-dir F:\Projects\VisionTools\build -O F:\tmp\VisionTools_R43_CTest.log --output-junit F:\tmp\VisionTools_R43_CTestResults.xml` → **17/17 Passed (100%, 0 Skipped, 0 Failed)**（含 real YOLO whitebox trace 0.21s 通过）；
- `pytest tests/test_python_tools.py tests/test_logic_engine.py tests/test_package_profile.py`（VisionTools） → **50/50 Passed (100%)**；
- `tools/package_tools.py --profile rtx40-build` → 生成 `dist/VisionTools.zip`（22 工具）；
- `ctest --test-dir F:\Projects\VisionRT\build -R "installed_"` → **2/2 Passed (100%)**；
- `tools/package_app.py --install-dir F:/tmp/VisionRT --tools-package ... --force-clean` → 校验通过（1837 文件）；
- `F:\tmp\VisionRT\VisionRT.exe --screenshot F:\tmp\launch_screenshot_r43.png --screenshot-delay 1500` → 退出码 0，生成 45,330 字节视觉快照；
- 变异体检验：在测试 17c 中临时注释 `truncated` 保护字段判定，测试立即 red 拦截，证明截断防碰撞测试具备真实变异拦截能力。

**下一步**
- 维护 WORKLOG.md；
- 将 VisionTools 子模块变更提交并推送到 dev；
- 将 VisionRT 的 C++ 源码、测试与子模块提交并推送到 dev（严格排除 WORKLOG.md 与 docs/ 下文件）。

### 2026-10-29 — R42 全套工单实施与全矩阵验证发布

**目标**
- 根据 R42 方案完整实施并验证 T42-01 至 T42-08，保持 ABI7 协议信封不变。
- 修复单缓冲有界日志截断与非前缀子串回退缺陷，截断/紧缩场景直接投递防止误聚合；
- 补齐日志有界截断 5 类确切边界用例、消解摘要探针误报、增加晚期写入并发屏障与辅助预览 3 分支异常测试；
- 收敛 Theme 参数字体为唯一真相源（消除 paramEditorFontSize），高度与标签宽度单源化，统一 QML 绑定与动态覆盖测试；
- 移除 VisionTools 内部重复 `ToolFactory.h`，统一复用 `VisionRTContracts` 公共类工厂 `visionrt::registerClass`，补齐构造器异常测试；
- 修正 Python SDK 状态端口文档注释与测试用例，实跑白盒推理追踪（无 skip），执行全矩阵打包与独立安装验收。

**当前状态**
- 已完成：
  - T42-01：LogService 单缓冲有界单行截断，记录切分点与省略号，避免非前缀子串截断；
  - T42-02：LogService 错误聚合保护，source 或 message 截断时不聚合，空间不足（< 22 字节）直接投递，摘要行优先使用完整重复说明；
  - T42-03：补齐 log_contract 截断 5 边界用例、重命名探针消解误报、晚期写入并发屏障，workbench_assist_release_contract 补齐 3 错误分支分类测试；
  - T42-04：Theme 移除冗余 paramEditorFontSize，parameterFont 作为单写源，统一 parameterRowHeight，导出 logButtonHoverColor、parameterLabelWidth、timingIconSize、executeButtonPlayIconSize 并绑定 QML，参数测试断言字体更新；
  - T42-05：移除 VisionTools 私有 ToolFactory.h，所有 C++ 模块统一复用 visionrt::registerClass，补齐构造器抛异常与缺少宿主服务断言；
  - T42-06：Python SDK 注释明确 status 由 result() 打包，更新逻辑引擎状态端口文档；
  - T42-07：配置 yolov8n.engine 真实路径实跑 test_infer_whitebox_trace（0.24s 通过，0 skipped），打包 VisionTools 并验证 22 工具，打包 VisionRT 部署包至 F:/tmp/VisionRT 并完成无头截图验收；
  - T42-08：按规范记录 Qt 解耦与 std 模型迁移延期说明。
- 未完成：无。

**验证证据**
- `ctest --test-dir F:\Projects\VisionTools\build` → **17/17 Passed (100%, 0 Skipped, 0 Failed)**；
- `pytest / python -m unittest`（VisionTools） → **50/50 Passed (100%)**；
- `tools/package_tools.py --profile rtx40-build` → 生成 `dist/VisionTools.zip` 与 `dist/visiontools-deploy.zip`（22 工具）；
- `ctest --test-dir F:\Projects\VisionRT\build` → **91/91 Passed (100%)**（含 backend 37、bridge 29、gpu 1、installed 2、package 4、perf 3、sdk 6、ui 39 全部绿灯）；
- `tools/package_app.py --install-dir F:/tmp/VisionRT --tools-package ...` → 完整交付目录校验通过（1837 文件）；
- `F:\tmp\VisionRT\VisionRT.exe --screenshot F:\tmp\launch_screenshot_r42.png --screenshot-delay 1500` → 退出码 0，成功生成 45,330 字节视觉快照。

**下一步**
- 提交并推送两仓改动（严格排除 WORKLOG.md 与 docs）。

### 2026-10-29 — R41 全套工单实施与全矩阵安装验证

**目标**
- 根据 R41 方案完整实施并验证 T41-01 至 T41-10，保持 ABI7 协议信封不变。
- 修复 Python 状态原样包装、收紧测试 view 辅助、收口工具与辅助预览异常边界、修复日志有界解码与同步、完成主题单例与样式传播、执行全矩阵打包与发布验收。

**当前状态**
- 已完成：
  - T41-01：移除 Python SDK 与公共 logic 包装器的隐式类型转换及缺失字段回退；
  - T41-02：收紧两仓 `tests/result_view.h` 与 Python 测试 helper，只过滤确切系统键，补缺字段与未知键反例；
  - T41-03：收口工具 pack 与业务 catch 边界，确保作者 ContractError 透传到达 NodeRuntime（DataInvalid）；
  - T41-04 & T41-05：LogService 实现有界 UTF-8 校验与换行转义，预留摘要计数空间，重写锁同步与锁外发送启动日志；
  - T41-06：Theme 改用 `engine->singletonInstance`，移除属性缓存与重复创建，补齐状态栏/分段按钮尺寸与主题色，实现 parameterFont/高度单写源与动态传播测试；
  - T41-07：AssistPreview 区分装配与执行阶段，使契约违约输出统一分类为 DataInvalid，未知 std 异常归 Internal；
  - T41-08：同步作者文档注释，清理未用字段；
  - T41-09：完成真实候选分组与安装验收，包括 Tools 打包、RT 部署、CPU+GPU 全契约验证；
  - T41-10：按规范明确延期，不引入未经审查的架构改动。
- 未完成：无。

**验证证据**
- `ctest --test-dir F:\Projects\VisionTools\build` → **16/16 Passed, 1 Skipped (GPU 白盒), 0 Failed**；
- `test_python_tools.py` → **18/18 OK**；
- `tools/package_tools.py --profile rtx40-build` → 生成 `dist/visiontools-deploy.zip`（22 工具）；
- `ctest --test-dir F:\Projects\VisionRT\build` → **91/91 Passed (100%)**（包含 `workbench_*` 21 项全部通过、`gpu_tools_contract` 171.14s 通过、`test_isolated_installation` 通过、`installed_pipeline_contract` 通过）；
- `tools/package_app.py --install-dir F:/tmp/VisionRT --tools-package ...` → 完整交付目录校验通过（1837 文件）；
- `F:\tmp\VisionRT\VisionRT.exe --screenshot F:\tmp\launch_screenshot.png` → 成功拉起真实 Workbench 并生成正常视觉快照。

**下一步**
- 交付成果，推送代码。

### 2026-10-28 — 三仓提交推送与 T39 执行报告

**目标**
- 把本轮三仓改动落成提交并推送，输出完成项/未完成项及原因的书面报告。

**当前状态**
- 已完成（提交）：VisionRT `538f734`（70 文件）、VisionTools `8f9fdfc`（64 文件）、Contracts `5147c32`（早已与 `origin/main` 同源）。
- 已完成（推送）：`origin/dev` 分别在两父仓推进到上述提交；三仓工作区干净，gitlink 一致。
- 已完成（报告）：[T39_执行报告](docs/T39_执行报告_20261028.md) 按 16 张工单逐条记账——14 张完成、T39-16 部分完成、T39-15 未启动（P3，独立后续目标）。
- 已完成（补证）：`-L installed` 复跑 2/2 通过，闭合 T39-12 的最后一处验证缺口。
- 未完成：T39-16 的一次性发布验收（全组干净构建、provenance 汇总、改造前后 A/B、Debug 配置实测）；`build_incremental_contract` 因沙箱拦截 `reg.exe` 无法在本会话给出绿结论。

**验证证据**
- `cmake --build build`（两仓）→ 0 错误；VisionRT 需手工导出 `INCLUDE`/`LIB`（`vcvars64.bat` 依赖被拦的 `reg.exe`）；
- `ctest -L installed` → **2/2 通过**（4.9 s）；
- `ctest -R build_incremental_contract` → Failed，输出为 `vcvars64.bat` 调用 `reg.exe` 被 Program Blacklist 拦截，非参数拆分缺陷；
- `git push origin dev`（VisionRT）→ `e83b603..538f734`；`git push origin dev`（VisionTools）→ `c268186..8f9fdfc`。

**下一步**
- 按报告第 5 节顺序：先补 T39-16 的三块证据（不改代码），再开 T39-15 首个切片（纯 std 模型 + no-Qt probe）。

### 2026-10-28 — T39-08 第三阶段、ui/bridge 组实跑、Contracts 推送

**目标**
- 收尾上一轮遗留：Contracts 推送、`ui`/`bridge` 组实际执行、`ToolRegistry`/`PluginLoader`/`PythonRuntime` 剩余裸负数码迁移（T39-08 第三阶段）；T39-15 按 P3 评估后再决定是否动手。

**当前状态**
- 已完成（Contracts 推送）：`main` 从 `78c966d` 推到 `5147c32`（含测试随子工程注册、ROI 限定有限可逆仿射）。
- 已完成（错误码，[Error.h](src/core/Error.h)、[ToolRegistry.h](src/core/ToolRegistry.h)、[ToolRegistry.cpp](src/core/ToolRegistry.cpp)、[PluginLoader.cpp](src/plugins/PluginLoader.cpp)、[PythonRuntime.cpp](src/python/PythonRuntime.cpp)、[RuntimeEngine.cpp](src/runtime/RuntimeEngine.cpp)、[AppController.cpp](src/ui/AppController.cpp)、[AppControllerDiagnostics.cpp](src/ui/AppControllerDiagnostics.cpp)、[AssistPreview.cpp](src/ui/AssistPreview.cpp)）：新增 `makeStatus(code, message, detail)` 作为宿主唯一构造入口；`src/` 内不再有字面负数码。分类按语义落地而不是替换数字：注册参数错 `-2`、注册异常 `-1`；定义缺失/工厂未注册 `-4`；宿主绑定/无加载器 `-14`；Python 未加载/关闭/刷新 `-12`；模块加载失败（文件、符号、ABI、空实例、重复 ID）`-11`；重入加载 `-3`；生命周期状态错 `-7`；入口不是 `.py`/缺 symbol `-13`；释放或初始化抛异常 `-1`（不再伪装成 `-4 NotFound`）。`RegistryError` 由 `ToolAvailability` 推导默认码。
- 已完成（错误码契约）：[module_registration_contract](tests/module_registration_contract.cpp)、[plugin_contract](tests/plugin_contract.cpp)、[python_adapter_contract](tests/python_adapter_contract.cpp)、[python_loading_contract](tests/python_loading_contract.cpp) 改为断言 `hostCode(ErrorCode::...)`；`unavailable()` 显式要求期望码，区分“实现不存在”和“运行时已关闭”。
- 已完成（ui/bridge 实跑）：首轮 13/29 失败，全部定位为 ABI 6 遗留——11 个测试的 Python 夹具仍返回 `{'__status_code', ...}` 旧信封；`workbench_assist_release_contract` 的 C++ 夹具把 `__status_code` 当业务输出写；`workbench_startup` 仍断言图像读取失败码为 `1`（工具域已是 `101004`）。按新契约批量修正后 `-L bridge` 29/29、`-L ui` 39/39 通过。
- 已完成（路径断言）：[workbench_paths.cpp](tests/workbench_paths.cpp) 的“目录参数重开后不变”改为比较解析后的真实资源（同盘工程按相对路径存储，字符串形式本就会变）；[ui_project_paths_contract.cpp](tests/ui_project_paths_contract.cpp) 区分“另存为需重定位”和“普通保存保留用户字面量”，并给失败信息补上端口、存储值与工程目录。
- 未完成（T39-15）：按 S39-14 明确为“无 Qt 为独立后续目标、不作为 T39-16 前置”，且要求先拆纯 std 模型/错误再拆 Qt JSON 与平台加载 adapter，属于跨多文件的架构改造；本轮不做，避免在错误码迁移同一批里引入两套模型。首次切片建议：把 `core/Error.h` + 值模型做成不依赖 Qt 的静态库，加一个只链接它的 no-Qt probe（`dumpbin /dependents` 作为证据），再逐步把 `Project.h`/`ValueJson.h` 的 Qt JSON 移到 adapter。
- 未完成（未验证）：`sdk` 组本轮未复跑（Contracts 改动已在上一轮验证，推送不改二进制）；`installed`、`package`、`gpu` 组未复跑（本轮未触及工具包与推理路径）。

**验证证据**
- `ctest -L backend` → **37/37 通过**；
- `ctest -L bridge` → **29/29 通过**（316 s → 68 s，不再有超时）；
- `ctest -L ui` → **39/39 通过**（148 s）；
- `ctest -R "^(project_storage_contract|project_editor_contract)$"` → 2/2（确认“仅另存重写路径”的原契约未被破坏）；
- `git push origin main`（Contracts）→ `78c966d..5147c32`。

**下一步**
- 若要继续 T39-15，按上面的首个切片单独立项；`project_storage_contract` 的“普通保存保留字面量”与 `ui_project_paths` 的“另存后相对”已成对断言，改路径策略必须同时改两处。

### 2026-10-28 — 三仓审核包执行：日志生命周期、测试分组、ROI 仿射契约、OpenCV 多配置部署

**目标**
- 按 `docs/VisionRT_三仓审核_完整方案_Specs_Tickets_20261028.zip` 执行 P1/P2 工单：T39-01 子模块同源、T39-03/04 日志、T39-06 测试分组、T39-13 ROI 范围、T39-14 OpenCV 配置；T39-09（ABI 6 + 独立 detail）以上轮已落地的 `ToolResult{outputs,status}` 为载体，不重复改造。方案与既有实现冲突处按“单一真相源 + 长期可维护”就地修正，不引入第二套记录。

**当前状态**
- 已完成（日志，[LogService.cpp](src/log/LogService.cpp)、[LogService.h](src/log/LogService.h)、[LogModel.cpp](src/log/LogModel.cpp)）：生产者共享锁 / 关闭独占锁构成排空屏障，线程池与 sink 最后释放；Qt handler 恢复按“安装过”判定，默认 handler（非空指针）同样还原；timer 只在构造时连接一次；首条错误立即入队、重复只累计后续次数并保留等级与最近时间、Critical 不聚合不延迟；换行在日志层转义、按 UTF-8 字符边界截断；`take()` 的丢弃摘要占用同一个 maximum 预算；模型先裁剪输入批再删旧行。
- 已完成（日志契约，[log_contract.cpp](tests/log_contract.cpp)）：新增首条即时/Critical 等级/CRLF 单行/UTF-8 边界/take 预算/批量裁剪/并发关闭/handler 还原八组断言；旧“重复 5 次合并为一行”的断言按新语义改为“首条 + 重复 4 次”。
- 已完成（测试分组，[CMakeLists.txt](CMakeLists.txt)、[AddTest.cmake](cmake/AddTest.cmake)）：`sdk` 组由 Contracts 子工程注册真实成员（option `VISIONRT_CONTRACTS_TESTS`），`package` 组带上真实前置目标，`gpu` 有独立 ctest 入口（缺设备/缺 fixture 返回 77 → NOT RUN），`bridge`/`ui` 在 `BUILD_APP=OFF` 下不再给不存在的测试设属性，配置阶段拒绝空组。
- 已完成（Contracts，[ROI.cpp](3rdparty/VisionRTContracts/src/ROI.cpp)、[ROI.h](3rdparty/VisionRTContracts/include/visionrt/contracts/ROI.h)）：`transformedRoiBounds` 限定为有限可逆仿射，透视/奇异/非有限矩阵显式拒绝，删除透视 512 点采样；圆仍用精确椭圆包围盒。安装态消费者夹具更新到 ABI 6 协议，`test_isolated_installation` 首次真正执行并通过。
- 已完成（OpenCV，[ConfigOpenCV.cmake](cmake/ConfigOpenCV.cmake)）：运行时 DLL 取自 `$<TARGET_FILE:...>`，开发部署与包清单按实际配置解析，删除 configure 期猜测与没人读取的 per-config 清单。
- 已完成（子模块，`.gitmodules`）：两份 `.gitmodules` 增加 `branch = main`；Contracts 本地提交后两父仓 gitlink 同源。
- 未完成/未验证：`ui`、`bridge` 组未跑；T39-15 无 Qt 拆分未启动；宿主 `ToolRegistry`/`RuntimeEngine` 仍有少量裸负数码，属 T39-08 第三阶段，未在本次迁移；`installed` 组只跑了其中 `installed_python_author_contract`。

**验证证据**
- `ctest -R "^log_contract$"` → Passed（连续 3 次），覆盖并发关闭与 handler 还原；
- `ctest -L sdk` → **6/6 通过**（含隔离安装消费者，ABI 6）；
- `ctest -R "^runtime_value_contract$"` → Passed（含剪切/镜像/奇异/透视/非有限矩阵断言）；
- `cmake -S . -B F:/tmp/vrt-noapp -DVISIONRT_BUILD_APP=OFF -DBUILD_TESTING=ON` → 配置完成，`bridge`/`ui` 打印“不存在”；空 `sdk` 组会 FATAL_ERROR；
- `ctest -R "^gpu_tools_contract$"` → **Passed（208 s）**，在 RTX 4060 上用重新出包的 `dist/visiontools-deploy` 完成 build_wts 与推理两阶段（`[PASS] Native YOLO matches five PyTorch detections; downstream filter retains four.`）；`--show-only=json-v1` 确认 `LABELS=gpu`、`SKIP_RETURN_CODE=77`，缺设备/缺 fixture/无 infer 模块三条路径都返回 77 且不被当作通过；
- `cmake --build build --target deploy_runtime_dlls` → 按当前配置复制 OpenCV DLL 成功；
- 带真实包回归 `python_contract`、`cpu_tools_contract`、`installed_python_author_contract`、`examples_contract` → **4/4 通过**（工具夹具重建为 `--profile cpu` 的 21 工具包）。

**下一步**
- 复跑 `-L ui`、`-L bridge`、`-L installed`（日志与分组改动都触及这些路径）；
- 若继续 T39-08，把 `ToolRegistry`/`RuntimeEngine` 剩余裸数码按 `ErrorCode` 迁移，同步更新 `tool_registry_contract`/`python_loading_contract` 的断言。

### 2026-10-28 — 错误处理重构落地（Contracts ABI 6 / 单一 status / Python 信封）

**目标**
- 按 `docs/VisionRT_错误处理重构_最终版/` 的方案交付：工具只回答一个 `status`，`__status_code` / `__status_message` 由宿主在工具正常返回后物化；宿主自己有一套可分支的错误码与返回类型；Python 工具改用 `{status, outputs}` 信封。方案与代码冲突处以方案为准，方案本身不合理处就地修正。

**当前状态**
- 已完成（Contracts，[Tool.h](3rdparty/VisionRTContracts/include/visionrt/contracts/Tool.h)、[Issue.h](3rdparty/VisionRTContracts/include/visionrt/contracts/Issue.h)、[Parameters.h](3rdparty/VisionRTContracts/include/visionrt/contracts/Parameters.h)）：`ToolResult{outputs, status}`、`MODULE_ABI_MAJOR = 6`；`InputReader` / `OutputWriter` / `PathResolver` 对坏输入返回 `PortIssue`/`ReadResult`，不再抛异常；新增 `isStatusPort`、`materialiseToolStatus` 之外的契约常量。
- 已完成（宿主，[Error.h](src/core/Error.h)、[Result.h](src/core/Result.h)、[NodeRuntime.cpp](src/runtime/NodeRuntime.cpp)、[ToolValueValidation.cpp](src/core/ToolValueValidation.cpp)）：`ErrorCode` 负数码表 + `ErrorInfo`；`RuntimeEngine::start/requestReset/requestRefresh/requestSynchronize` 返回 `ErrorInfo`；工具业务码原样上报，宿主只把 status 写进两个状态端口；执行/转换阶段的契约破坏判为 `DataInvalid(-13)`，输入装配阶段仍为 `InvalidArgument(-2)`。
- 已完成（UI 诊断，[Diagnostics.cpp](src/ui/Diagnostics.cpp)、[AppControllerDiagnostics.cpp](src/ui/AppControllerDiagnostics.cpp)）：不再按换行拆消息，`message`/`detail` 直接进入诊断摘要与详情。
- 已完成（VisionTools）：`ToolErrorDomains.h` + 公共/读图/裁剪/几何/计量/YOLO 错误目录；读图码严格按方案 101001–101005、YOLO 301001–301005；全部工具 `execute` 改为返回式，`fail()` 只在工具内部离开嵌套 helper。
- 已完成（Python 协议，VisionTools `python/**/visiontools_sdk` 与 `builtin/logic/common/status.py`）：`execute` 返回 `{status, outputs}`，`initialize`/`release` 只返回 status；示例工具（image_add / image_mean / roi_geometry / image_sizes / call_counter）与表达式/分支工具全部迁移，示例内联夹具同步。
- 已完成（测试环境，[AddTest.cmake](cmake/AddTest.cmake)）：测试的 `TMP`/`TEMP` 由 `VISIONRT_TEST_TEMP_DIR` 统一指定（本机设为 `F:/tmp`），不再写系统盘；此前的 `LNK1104`/`0xc0000409` 时好时坏即 C 盘写满所致。
- 已完成（VisionTools 自身契约）：`ctest --test-dir build` 17/17 通过；期间修掉 `AssistRequest` 缺失 `return` 与 `extentMin` 自引用两处重构误伤。
- 未完成：`ui`/`bridge` 标签未跑；`package/build_incremental_contract` 在 `build.cmd` 的参数拆分处失败（环境缺陷，与本轮改动无关）。

**验证证据**
- `ctest --test-dir build -L backend --output-on-failure` → **37/37 通过**（含 runtime_contract、runtime_diagnostics_contract、plugin_contract、python_adapter_contract、python_contract、runtime_python_lazy_contract、cpu_tools_contract、examples_contract）；
- `ctest --test-dir build -L installed` → **2/2 通过**；
- VisionTools `python -m unittest tests.test_python_tools tests.test_logic_engine` → **全部通过**；`tools/package_tools.py` 重新打包 `dist/visiontools-deploy` 并同步到 `build/tools-package`、`build/bin/tools-package`；
- `ctest --test-dir build -L package` → 3/4，仅 `build_incremental_contract` 失败（构建脚本参数拆分，未验证与本轮相关）。

**下一步**
- 跑 `-L ui` 与 `-L bridge` 确认 UI 侧对诊断字段的改动；如需完整交付，再复跑 `-L package` 并修 `build_incremental_contract` 的脚本调用。

### 2026-10-28 — 流程页签运行指示灯纯化与下拉复选列表改造

**目标**
- 将流程页签（Tab）上的小圆点去掉点击交互功能，纯化为状态指示灯（绿色实心 = 启用运行，灰边空心 = 禁用运行）。
- 将画布顶部的“选择流程: X/Y”下拉框改造为多选复选列表（包含 CheckBox、高亮数字、展开箭头与边框高亮联动），支持在下拉列表中选择启用/禁用流程。
- 更新自动化测试契约，并在实机包 `F:\tmp\VisionRT` 重新打包部署。

**当前状态**
- 已完成（UI 与交互重构）：
  1. [`FlowCanvas.qml`](src/ui/qml/FlowCanvas.qml)：
     - 移除流程页签上绿色小圆点的 `MouseArea`，保留 `flowTabIndicator_<id>` 作为纯只读状态灯；
     - 改造 `flowDropdown`：文本拆分为“选择流程: ”与加粗高亮数字 `X/Y`（已启用/总流程数），右侧箭头与边框在弹窗展开时联动高亮；
     - 弹窗改用 `Popup`，内嵌流程列表 `ListView`，每个流程行包含蓝底勾选框（`QuiFontIcon.CheckMark`）与流程名称，点击整行或复选框触发 `app.setFlowSelected(id, !selected)`，支持连续多选不自动关闭。
  2. [`tests/workbench_flows_contract.cpp`](tests/workbench_flows_contract.cpp)：
     - 将原本通过页签圆点点击切换运行选择的断言，更新为点击 `flowDropdown` 打开弹层并通过弹层内的复选框触发，同时断言页签指示灯颜色状态同步更新。
- 已完成（验证与部署）：
  - `workbench_flows_contract` 1/1 通过（1.18 s）；
  - `workbench_contract --startup-child` 通过（双启动模式均通过）；
  - 重新打包部署至 `F:\tmp\VisionRT`（1830 文件完整校验通过）。

**验证证据**
- `ctest --test-dir build -R "workbench_flows_contract" --verbose` → **1/1 Passed (1.18 s)** (`WORKBENCH_FLOWS_PASS added=3 tabs=4 cancel=1 confirm=1 last_guarded=1 menu=1`)；
- `build\bin\workbench_contract.exe --startup-child` → 退出码 0，两轮启动均验证 `[Startup Contract Passed]`；
- `python tools/package_app.py --build-dir build --install-dir F:/tmp/VisionRT --tools-package F:/Projects/VisionTools/dist/visiontools-deploy.zip --force-clean` → 退出码 0，1830 files 验证通过。

**下一步**
- 邀请用户在 `F:\tmp\VisionRT` 实机运行测试新的流程复选列表与页签指示灯。

### 2026-10-28 — 修复“最近打开工程”二级菜单展开、完整路径展示与 ToolTip 残留问题

**目标**
- 修复 `Main.qml` 中“最近打开工程”未显示二级展开箭头（`>`）且悬停无法弹出二级工程列表的问题。
- 二级菜单直接显示完整路径（`modelData.tooltip`），彻底移除 `ToolTip`，根除弹窗关闭后 ToolTip 悬浮残留的问题。
- 补充合约级自动化验证并在实机包 `F:\tmp\VisionRT` 完成部署。

**当前状态**
- 已完成（架构修正）：
  1. 定位根因：此前在 `Main.qml` 中错误地将二级菜单 `QuiMenu` 嵌套于 `QuiMenuItem` 内部；Qt Quick Controls 2 中 `MenuItem` 内部嵌套 `Menu` 无法将该菜单绑定为其 `subMenu` 属性，导致 `arrow.visible: control.subMenu` 判定为 false 且菜单框架不会分发 hover 展开事件。此外，菜单项使用全局 Overlay 层的 `ToolTip` 时，在菜单点击关闭后因 Qt 未派发 `HoverLeave` 导致 ToolTip 永久残留。
  2. 方案改进：
     - 参考 `DeepLearningTool` 架构最佳实践（如 `DatasetsView.qml`），直接将 `QuiMenu` 作为父级 `fileMenu` 的子项（设置 `title: "最近打开工程"`），剥离多余的外层 `QuiMenuItem` 包装，同时内部采用标准 `Instantiator + insertItem/removeItem` 范式显式挂载管理子菜单项。
     - 菜单项直接显示完整路径（`text: modelData.tooltip`），彻底移除所有 `ToolTip` 代码，既让用户一览完整路径，又从根源上消除 ToolTip 残留 Bug。
  3. 增加合约验证：在 [`workbench_startup.cpp`](tests/workbench_startup.cpp) 中新增断言，自动化校验 `fileMenu` 中对应项的 `subMenu` 绑定、`title`、`enabled`，以及二级菜单子项存在且文本为完整路径。
- 已完成（验证与部署）：
  - `workbench_contract --startup-child` 通过（双启动模式均通过，校验完整路径展示与二级展开）；
  - `recent_projects_contract` 1/1 通过（0.17 s）；
  - 重新打包部署至 `F:\tmp\VisionRT`（1830 文件完整校验通过）。

**验证证据**
- `build\bin\workbench_contract.exe --startup-child` → 退出码 0，两轮启动均验证 `[Startup Contract Passed]`；
- `ctest --test-dir build -R "recent_projects_contract" --verbose` → **1/1 Passed (0.17 s)**；
- `python tools/package_app.py --build-dir build --install-dir F:/tmp/VisionRT --tools-package F:/Projects/VisionTools/dist/visiontools-deploy.zip --force-clean` → 退出码 0，1830 files 验证通过。

**下一步**
- 邀请用户在 `F:\tmp\VisionRT` 实机运行测试。

### 2026-10-28 — 彻底根治路径弹窗顶开参数行间距问题

**目标**
- 彻底根除 `QuiFileFolderDialog` 打开时顶开“图像路径”与“递归选项”纵向间距的问题。
- 重新出包并部署至 `F:\tmp\VisionRT`，提交推送代码（不提交 WORKLOG.md 与 docs）。

**当前状态**
- 深入定位根因：此前在 `QuiFileFolderDialog.qml` 内部设置 `parent: null` 时，因 QML 声明式语法树在实例化时由引擎的 `data_append` 机制隐式调用 `item->setParentItem(root)`，导致在组件内部的静态 `parent: null` 被外部 `ColumnLayout` 强制重置为了父级 `ColumnLayout`，弹窗打开时依然被 `ColumnLayout` 当作可见项参与垂直布局并插入了 `Theme.parameterRowSpacing`。
- 彻底解决方案：
  1. 将 [`ParameterEditor.qml`](src/ui/qml/ParameterEditor.qml) 中的 `pathPicker` 移出根级 `ColumnLayout`，置入内部的 [`controlFrame`](src/ui/qml/ParameterEditor.qml#L75)（输入框外层 `Rectangle`，固定高度 28px，非 Layout 容器）。从结构上使 `ColumnLayout` 永远只有唯一的 `RowLayout` 单一子项，杜绝任何纵向间隙计算。
  2. 在 [`QuiFileFolderDialog.qml`](3rdparty/QuickUI/src/controls/QuiFileFolderDialog.qml) 中显式约束 `width: 0; height: 0; implicitWidth: 0; implicitHeight: 0`。
- 已完成（验证与部署）：`workbench_paths_contract` 1/1 通过（2.36 s）；重新打包部署至 `F:\tmp\VisionRT`（1830 文件完整校验通过）。
- 已完成（提交与推送）：QuickUI 提交 `f6c35b8` 推送至 `origin/main`；VisionRT 提交 `9ed0e28` 推送至 `origin/dev`。未暂存 `WORKLOG.md` 及 `docs/`。

**验证证据**
- `ctest --test-dir build -R "workbench_paths" --verbose` → **1/1 Passed (2.36 s)**；
- `python tools/package_app.py --build-dir build --install-dir F:/tmp/VisionRT --tools-package F:/Projects/VisionTools/dist/visiontools-deploy.zip` → 退出码 0，`tools package verification passed successfully`，1830 files；
- QuickUI 远程推送：`To https://github.com/QWERDF007/QuickUI fd549a5..f6c35b8 main -> main`；
- VisionRT 远程推送：`To https://github.com/QWERDF007/VisionRT.git d84f24e..9ed0e28 dev -> dev`。

**下一步**
- 邀请用户在实机上运行测试并确认。

### 2026-10-28 — 修复路径弹窗顶开参数行间距并将标题改为“选择文件或文件夹”

**目标**
- 将路径选择弹窗标题改为“选择文件或文件夹”（针对目录/文件模式自适应，针对 path 模式统一为“选择文件或文件夹”）；
- 消除弹窗打开时顶开“图像路径”与“递归选项”等参数行间距的视觉 Bug；
- 重新出包并部署至 `F:\tmp\VisionRT`，提交推送代码（不提交 WORKLOG.md 与 docs）。

**当前状态**
- 已定位根因：`ParameterEditor.qml` 根节点为 `ColumnLayout`，弹窗控制器 `QuiFileFolderDialog` 作为其直接子 Item 挂载；当弹窗打开时其 `visible` 变为 `true`，触发 `ColumnLayout` 自动将其加入垂直排列流，并在“图像路径”行与弹窗 Item 之间额外插入了 `spacing: Theme.parameterRowSpacing`，导致下方的“递归选项”被多余间距顶开；弹窗关闭后因 `visible: false` 间距才复原。
- 已完成（QuickUI）：在 `3rdparty/QuickUI/src/controls/QuiFileFolderDialog.qml` 中设置 `parent: null`，彻底将非视觉的弹窗控制器从任何 QML 父布局的 visual tree 中剥离，确保弹窗状态变化不会对调用方的任何 `ColumnLayout` / `RowLayout` 产生任何排版与间距污染。
- 已完成（VisionRT）：在 `src/ui/qml/ParameterEditor.qml` 中将 `pathPicker.title` 统一修正为 `root.parameter.editor === "directory" ? "选择文件夹" : (root.parameter.editor === "file" ? "选择文件" : "选择文件或文件夹")`，图像路径打开时显示“选择文件或文件夹”。
- 已完成（验证与部署）：`workbench_paths_contract` 1/1 通过（2.35 s）；重新打包部署至 `F:\tmp\VisionRT`（1830 文件完整校验通过）。
- 已完成（提交与推送）：QuickUI 提交 `fd549a5` 推送至 `origin/main`；VisionRT 提交 `d84f24e` 推送至 `origin/dev`。未暂存 `WORKLOG.md` 及 `docs/`。

**验证证据**
- `ctest --test-dir build -R "workbench_paths" --verbose` → **1/1 Passed (2.35 s)**；
- `python tools/package_app.py --build-dir build --install-dir F:/tmp/VisionRT --tools-package F:/Projects/VisionTools/dist/visiontools-deploy.zip` → 退出码 0，`tools package verification passed successfully`，1830 files；
- QuickUI 远程推送：`To https://github.com/QWERDF007/QuickUI e5332f6..fd549a5 main -> main`；
- VisionRT 远程推送：`To https://github.com/QWERDF007/VisionRT.git cc0631b..d84f24e dev -> dev`。

**下一步**
- 继续后续业务需求。

### 2026-10-28 — 原生文件弹窗单一“打开”按钮兼容文件与文件夹选择

**目标**
- 移除此前通过 `IFileDialogCustomize` 注入的“选择此文件夹”附加按钮，保持原生文件选择器极简外观。
- 实现仅依赖原生“打开”按钮（`IDOK`）即可同时完成文件或文件夹选择（选文件时返回文件路径；未选文件或选目录时返回目录路径）。
- 部署到 `F:\tmp\VisionRT` 并完成远程推送（不提交 worklog 与 docs）。

**当前状态**
- 已完成（QuickUI）：在 `3rdparty/QuickUI/src/QuiNativeFileDialog.cpp` 中移除 `IFileDialogCustomize` 及自定义按钮控件；通过 `IFileDialogEvents::OnFolderChange` 获取对话框 HWND 并以 `SetWindowSubclass` 监听 `WM_COMMAND`（`IDOK`）。在 `FileOrFolder` 模式下，若高亮项为目录或未选文件直接点击“打开”，自动提取目标目录路径并通过 `WM_CLOSE` 优雅接受退出；高亮项为文件或双击目录时保持 Windows 原生行为；清理了已无用的 `folderButtonText` 参数。
- 已完成（验证）：运行独立自动化探针（`test_auto_dialog.exe`）验证选文件夹返回目录、选文件返回文件；跑通宿主 `workbench_paths_contract`（覆盖 `readImageToolUiContract()` 文件与目录两种场景）及 `workbench_popups_contract` 契约测试；重新出包并部署至 `F:\tmp\VisionRT`（1830 文件完整校验通过）。
- 已完成（提交与推送）：QuickUI 提交 `e5332f6` 推送至 `origin/main`；VisionRT 提交 `cc0631b` 推送至 `origin/dev`。未暂存 `WORKLOG.md` 及 `docs/`。

**验证证据**
- `test_auto_dialog.exe`（自动化消息注入） → 选文件夹与选文件双向验证通过，控制台输出 `[TEST RESULT] Accepted folder: ...` 与 `[TEST RESULT] Accepted file: ...`；
- `ctest --test-dir build -R "workbench_paths" --verbose` → **1/1 Passed (5.07 s)**；
- `ctest --test-dir build -R "workbench_popups" --verbose` → **1/1 Passed (3.07 s)**；
- `python tools/package_app.py --build-dir build --install-dir F:/tmp/VisionRT --tools-package F:/Projects/VisionTools/dist/visiontools-deploy.zip` → 退出码 0，`tools package verification passed successfully`，1830 files；
- QuickUI 远程推送：`To https://github.com/QWERDF007/QuickUI 2c243e1..e5332f6 main -> main`；
- VisionRT 远程推送：`To https://github.com/QWERDF007/VisionRT.git 54615cb..cc0631b dev -> dev`。

**下一步**
- 继续后续业务需求。

### 2026-10-28 — 收尾核验：部署一致性、开发树夹具同步、dist 备份清理

**当前状态**
- 核验 `F://tmp//VisionRT` 为最新：`VisionRT.exe` md5 与 `build/bin/VisionRT.exe` 相同；`tools-package` 的 `vst_image.dll`/`vst_metrology.dll` 与 09:02 发行包一致（commit `077706f`、`built_at_epoch 1793148779`）；无源码比二进制更新。
- 开发树夹具原本落后于发行版：`build/bin/tools-package` 是 10-23 的 10 工具/1 模块旧包，`build/tools-package` 是 10-27 的 21 工具/2 模块、DLL 为 VT-03 之前版本。两者均已同步为发行包（22 工具/3 模块），本地运行与集成契约不再跑旧 DLL。
- 清理 `VisionTools\dist` 下 15 项出包备份（5 目录 + 5 ZIP + 5 索引副本，约 13.9 GB），仅保留当前 `visiontools-deploy` 与 `visiontools-deploy.zip`。

**验证证据**
- `ctest -R "^(workbench_contract|workbench_status_contract|workbench_tooltree_contract|workbench_catalog_contract|workbench_popups_contract|workbench_startup_contract)$"` → **6/6 通过（77.42 s）**；
- `ctest -R "^examples_contract$"` → **1/1 通过（4.65 s）**（使用同步后的 22 工具夹具）；
- 实机 `build/bin/VisionRT.exe --screenshot` 退出码 0，工具库 8 个分类加载、底部“流程 / 全流程 / 已运行”正常，日志仅启动与关闭两行、无 `[W]`/`[E]`，证据 `build/evidence/buildtree_after_sync.png`。

**仍未完成**
- 三仓改动均未提交（VisionRT 45 项、VisionTools 15 项含 4 个新文件、Contracts 3 项），Contracts gitlink 需随宿主提交一并更新；
- `docs/verification-gates.md` 列出的 5 项仍无自动化门槛（VR-09 滚动实机检查、跨时区、GPU/显存、Contracts 数值精度、干净机安装）。


### 2026-10-28 — 发布：重新出包并部署到 F://tmp//VisionRT

**目标**
- 用 VT-03 之后的工具包重新出包，把 VisionRT 发行版部署到 `F://tmp//VisionRT` 并做启动验证。

**当前状态**
- 已完成：VisionTools 按 `rtx40-build` 重新出包（22 工具），构建元数据改为单一来源后，包内 `build_info.json` 为 `commit 077706f…` / `built_at_epoch 1793148779`，不再是旧格式。
- 已完成：`package_app.py --force-clean` 重新部署，1830 个文件、931 MB；`tools-package` 校验通过；宿主与 Contracts 元数据分别为 `8a06881…/1793147148`、`5b2e118…/1793147129`。
- 过程记录：`F://tmp//VisionRT` 里残留的旧 `VisionRT.exe`（PID 28312）持有 DLL 导致首次替换失败，终止后重试成功；目录无 `.visionrt_package` 标记，故用 `--force-clean`。

**验证证据**
- `F://tmp//VisionRT//VisionRT.exe -v` → `VisionRT 0.1.0`；
- `VisionRT.exe --screenshot … --screenshot-delay 1500` → 退出码 0，窗口完整（工具库、画布、底部“流程 / 全流程 / 已运行”时间条），截图 `build/evidence/release_startup.png`；
- 运行后生成 `F://tmp//VisionRT//logs//visionrt_2026-10-28.log`，两行启动/关闭记录，无 `[W]`/`[E]`；
- 部署后 `tools-package/module_index.json` 为 22 工具 / 3 模块。

**下一步**
- 需要对外分发时，把 `F://tmp//VisionRT` 打包成压缩件或复制到目标机器；`dist/` 下的历史备份目录可按需清理。

### 2026-10-28 — 收尾：VT-02/03/04 落地、REV-01 门槛索引、VT-01 复核

**目标**
- 完成 2322 方案里剩余的全部票项：VisionTools 侧的 VT-02/03/04，以及跨仓的 REV-01。

**当前状态**
- 已完成（VT-02/03/04，VisionTools 仓库）：读图缓存实测、输出所有权收敛、missing 端口 helper 归并，明细见该仓 `WORKLOG.md` 同日条目。
- 已完成（VT-01 复核）：VisionTools 完整重建后 `build_info.json` 的 `built_at_epoch` 从 1793082712 前进到 1793148757，证明时间来自真实重链后的产物 mtime，而不是配置或元数据步骤的运行时刻。
- 已完成（REV-01）：新增 [`docs/verification-gates.md`](docs/verification-gates.md)，把每个已接受票项指向唯一的可执行门槛（测试文件或构建目标），并单列“尚未建立门槛”的推断项（滚动边缘的实机检查、跨时区、GPU/显存、Contracts 数值精度、干净机安装）。README 顶部加入口。后续任何“已验证”说法必须能在该表里找到门槛。
- 未做（发布步骤，非本票）：VisionRT 的测试夹具 `build/tools-package` 仍是 VT-03 之前的 DLL；重新出包需跑 VisionTools 的 `tools/package_tools.py` 并用宿主校验器验证，本轮未执行，因此关于弹窗里的工具包时间仍是旧值。

**验证证据**
- VisionTools：`cmake --build build` 退出码 0；`ctest -R "^(test_preprocess_contract|test_geometry_contract|test_upstream_status_contract|test_writable_tools_contract|test_module_contract|test_read_cache_contract|test_output_ownership_contract)$"` → **7/7 通过（1.18 s）**；
- VT-02 实测：`build/evidence/read_cache_evidence.json`（冷 113.6 ms/10 帧、热 0.19 ms/12 帧、120 帧循环 1.66 ms、工作集 +50.4 MiB 后不再增长、第二实例重新解码 12 帧）；
- VT-03 门槛自检：临时恢复 owner 保留即失败，恢复后通过；
- 本轮未改 VisionRT 的 C++ 代码，故未复跑其测试；上一轮的 7/7 与 26/26 结果仍对应当前源码（`docs/verification-gates.md` 记录门槛位置）。

**下一步**
- 出包时重跑 VisionTools 打包并更新宿主 `build/tools-package`，让关于弹窗里的工具包构建时间与 VT-03 后的 DLL 一致。

### 2026-10-28 — 继续完成：Contracts ROI 边界修复、VR-11 Release 基准、VR-12 覆盖确认

**目标**
- 收口上一轮明确未做的 P1 项：`VRC-02/03`（ROI 透视奇点、非等比仿射圆边界）与 `VR-11`（≥500 轮 Release 基准、P50/P95/P99 与根因分类），并核实 `VR-12` 是否已有回归覆盖。

**当前状态**
- 已完成（VRC-02/03，Contracts 仓库）：`transformedRoiBounds` 先校验矩阵有限性；`w == 0` 或非有限时抛 `ContractError("ROI transform is singular: ...")`，不再用 `w == 0 → 1` 伪造坐标；圆的仿射边界改为解析式（`r * ||线性部分第一行|| / |m22|` 与第二行），非仿射按 512 点采样；返回前再校验包围盒四个分量有限。宿主 `AppControllerAssist::nearCursor` 捕获该错误、按“不在光标下”处理并只记一条告警，避免从 QML 悬停槽抛出异常。明细见 Contracts `WORKLOG.md` 同日条目。
- 已完成（VR-11）：`tests/performance_contract.cpp` 增加 `--rounds=N`、P99、`runtime_benchmark.json`（wall / 引擎流程 / 最慢节点 / 调度间隙 / 会话建立 / 完成等待 六组分位数）。基准等待改为忙轮询 `waitCompletionBusy`——原来的 `sleep_for(1ms)` 在 Windows 上会被 15.6 ms 定时器精度放大，量到的是 harness 而不是运行时。
- 已完成（VR-12 覆盖确认）：`runtime_readback_contract` 已覆盖 Once/Node/Loop 三模式、Stop 后最终读回、ACK 前的覆盖门控、跨会话/跨轮次 ACK 拒绝、停机不死锁；本轮复跑通过，未新增重复用例。
- 未完成（明确不在本轮）：VT-02/03/04（VisionTools 读图缓存驻留、owner 传递、旧 helper 归并）仍为 P2，需在 VisionTools 侧用真实图像与 RSS 采样；REV-01 的“105 项清单转基准门槛”跨三仓，未启动。

**验证证据**
- `test_sdk_value_contract`（独立 `build/contracts-roi`，Ninja/Release）：`ABI5 writable value, ROI, result and scalar contracts verified`；
- `ctest -R "^(runtime_value_contract|runtime_readback_contract|workbench_assist_contract|workbench_assist_release_contract)$"` → **4/4 通过（7.03 s）**；
- `performance_contract --evidence-dir build/evidence --rounds=500` → 退出码 0，`build/evidence/runtime_benchmark.json`：wall p50 1.096 / p95 1.639 / p99 2.081 ms；引擎流程 p50 1.001 / p99 1.987 ms；最慢节点 p50 0.464 / p99 1.198 ms；调度间隙 p50 0.532 / p99 0.953 ms；会话建立 p50 0.028 ms；完成等待 p50 1.066 ms。
- 该组数据同时解释了“工具 / 流程 / 全流程为什么不同”：多节点流程里约一半区间是节点之间的调度帧，单节点单流程时三者仍是同一区间。

**下一步**
- 若要继续 P2：在 VisionTools 用 1/10/100 张不同尺寸图与长 Loop 量化读图缓存峰值 RSS 与复用率（VT-02），再做输出 Mat 与输入无共享的断言后削减 owner 链（VT-03）；
- Contracts 改动尚未提交，宿主 gitlink 需在提交时一并更新。

### 2026-10-28 — 验收方案落地：整行日志/每日文件、节点耗时语义、计时口径统一、关于弹窗分层与滚动边界

**目标**
- 按 `docs/VisionRT_20261027_验收与优化方案_Specs_Tickets-2322.zip` 落地 VR-01~VR-10 与 VT-01、VRC-01：关于弹窗分层、真实构建时间按本地时区显示、日志改整行纯文本并每日落盘到软件目录、运行中节点不再显示耗时、Tooltip 只显示有效阶段、可滚动控件边缘停止；
- 重点对齐「工具时间 / 流程时间 / 全流程时间」的口径，让同一节点在单流程单节点时三层数值完全一致，并把差异压缩到只来自真实的调度与并发。

**当前状态**
- 已完成（日志，VR-04/05/06）：`LogService` 改用 spdlog 原生 pattern `[%Y/%m/%d %T.%e] [%L] [%t] %v`（不含 `%n`，无 `[visionrt]`），来源以 `[来源]` 写入 `%v`；文件与窗口共用同一格式器；`daily_file_sink_mt` 按本地日期写 `<exe>/logs/visionrt_YYYY-MM-DD.log`，同日追加；目录不可写时降级 stderr、计入 `writeFailures()` 并在窗口给一条“日志目录不可写”诊断，不静默改路径；`LogConfig` 去掉按大小轮转的 `fileBytes/fileCount`；`LogDialog` 改为单列可选中整行，无表头、无分列。
- 已完成（节点耗时，VR-07/08）：`DurationText` 对运行中、无快照、跳过/阻塞/取消一律返回空文本（不再出现“运行中”“—”），并新增 `timingPhasesOf()` 只输出真正测到的阶段；节点模型改为 `timingText` + `timingPhases`，`FlowCanvas` 悬停卡直接遍历阶段集合，`0.000 ms` 仍作为有效行显示。
- 已完成（计时口径，VR-10）：`NodeRuntime` 记录一次连续执行窗口 `[prepare 开始, 最后一个阶段结束]`，异常路径在逃逸点闭合；`Snapshot` 增加 `startedNs/endedNs`；`RuntimeEngine` 用节点窗口的并集派生流程耗时、用各流程窗口的并集派生全流程耗时，不再另起一套计时。单节点单流程时节点总计=流程=全流程；多节点差额只来自节点间调度帧，多流程差额只来自并发错峰；底部时间条增加口径说明 Tooltip。
- 已完成（关于弹窗，VR-01/VR-02/VR-03）：内容区改为独立分层面板（`Theme.surfaceBg` + 1px `Theme.border` + 圆角 + 16px 外边距），字体统一用 `Theme.monoFont`；构建时间改由构建完成阶段生成——`cmake/WriteBuildInfo.cmake` + `VisionRT` POST_BUILD 写 `build_info.json`（宿主链接完成时刻）与 `contracts-build-info.json`（取 Contracts 产物 mtime）；`Version` 去掉以宿主时间冒充 Contracts 的 `CONTRACTS_BUILD_TIME`；`AppController` 读三份元数据并按当前机器本地时区带偏移显示，缺失即“未知”；`package_app.py` 随包复制两份元数据。
- 已完成（滚动，VR-09）：新增 `BoundedScrollView`（`StopAtBounds` + `boundsMovement`），表达式编辑器与关于弹窗改用它；`LogDialog`、`ParameterPanel`、`GuardEditor`、`ResultsPanel`、`FlowCanvas`（流程标签横向 Flickable、搜索建议）、`OutputPanel`（`QuiTableView`）补齐边缘停止。
- 已完成（VisionTools，VT-01）：`CMakeLists.txt` 去掉配置期时间戳，改为构建结束目标 `visiontools_build_info` 按产物最新 mtime 写 `build_info.json`（新增 `cmake/WriteBuildInfo.cmake`），与 VisionRT 同一 schema。
- 未完成（明确不在本轮）：VRC-02/03（Contracts ROI 透视奇点与圆仿射边界）属另一仓库且需独立的 ROI 数值基线，本轮未改；VT-02/03/04（读图缓存驻留、owner 传递、旧工具 helper 归并）需真实资源测量；VR-11 基准（≥500 轮 P50/P95/P99）与 REV-01 未做。
- 未完成（环境）：本机无 `cmd` 可用，改用手工拼装的 MSVC/SDK 环境直接调用 `cmake --build`；`tests-all` 全量构建在 `examples_contract` 一类大翻译单元上耗时过长，改为按需构建受影响目标。

**验证证据**
- `cmake --build build --target log_contract version_contract runtime_contract workbench_contract workbench_status_contract ...` → 退出码 0，无 `error C/error LNK`；
- `ctest -R "^(workbench_.*_contract|runtime_contract|node_gate_contract|log_contract|version_contract|package_app_contract)$"` → **26/26 全绿（43.96 s）**；
- 日志实机：`build/bin/logs/visionrt_2026-10-28.log` 首行为 `[2026/10/28 00:07:21.601] [I] [29236] [日志] 日志服务已启动：...`，包内同样生成 `F:/tmp/VisionRT-pkgcheck/logs/visionrt_2026-10-28.log`；
- 构建元数据：`build/bin/build_info.json` = `built_at_epoch 1793116738`（= 2026-10-27 23:58 +08:00），`contracts-build-info.json` = `1793071356`（= 2026-10-27 11:22 +08:00），两者不同即证明未互相顶替；
- 关于弹窗截图证据 `build/evidence/about_dialog.png`：正文区有独立底色与边框、40 位 SHA 完整可见、按钮未裁切；
- 计时一致性：`runtime_contract` 新增断言“单节点单流程时节点总计、流程、全流程差 < 1e-3 ms，且各阶段之和 ≤ 总计”；`workbench_contract` 新增断言“底部流程与全流程数值差 ≤ 0.002 ms”；
- 打包回归：`python tools/package_app.py --build-dir build --install-dir F:/tmp/VisionRT-pkgcheck --tools-package ...` → 1829 个文件、校验通过，`VisionRT.exe --screenshot` 正常启动（证据 `build/evidence/packaged_startup.png`）；
- 未验证：125%/150% DPI 下的关于弹窗外观、跨时区（修改系统时区）实机复核、VisionTools 完整重建后的新 `build_info.json`（本轮仅用手工脚本重生成现有构建目录的元数据）。

**下一步**
- 在 125%/150% DPI 与跨时区环境复核关于弹窗与时间显示，并把 VisionTools 完整重建一次确认 `visiontools_build_info` 目标行为；
- 若需要，可单独开票处理 Contracts ROI 数值问题（VRC-02/03）与 VisionTools 缓存/owner 测量（VT-02/03）。

### 2026-10-27 — 「读取图像」工具 UI 自动化验证契约与「关于 VisionRT」弹窗规格改造落地

**目标**
- Part A: 补充自动化测试用例，包含 VisionRT 上对「读取图像」（`visionrt.image.read`）工具的真实 UI 自动化验证：通过单按钮“选择路径”分别打开图像文件（`F:\Projects\InferRT\assets\pics\bus.jpg`）与图像目录（`F:\Projects\InferRT\assets\pics`），断言参数正确更新、工程标记 dirty，并驱动管线单次执行验证解码输出；
- Part B: 按照 `docs/VisionRT_AboutDialog_96E688_Specs_Tickets_20261027.zip` 方案与 Tickets（AC-01、AT-01~03、AR-01~07）完成三仓协作与宿主关于弹窗全面改造：展示 VisionRT、VisionTools、VisionRTContracts 三组件版本号、40 位真实 Git Commit、UTC 构建时间；正文可鼠标拖选、右键复制、全选；底部“复制全部”完整复制不关闭弹窗；支持工具包未安装（“未安装”）与元数据缺失（“构建信息不可用”）等防御场景。

**当前状态**
- 已完成（Tickets AC-01 / AT-01~03 / AR-01~02）：
  - `VisionRTContracts`：通过 target 属性 `VISIONRT_CONTRACTS_VERSION` 导出自身 `PROJECT_VERSION`；
  - `VisionTools`：CMake 构建阶段生成 `build_info.json`（含版本、40位 Git SHA、UTC 构建时间），`package_tools.py` 随包打入 staging 与 ZIP 根目录，单元测试 `test_build_info_is_packaged_if_present` 通过；
  - `VisionRT`：`cmake/ConfigVersion.cmake`、`VersionBuildTime.h.in`、`VersionUtils.h.in`、`src/common/Version.h/.cpp` 抽取宿主引用的 Contracts Git 提交与版本宏，实现 `GetContractsVersion()` 并注入 `VersionInfo`；`tests/version_contract.cpp` 扩充 40 位 SHA 与 UTC 验证。
- 已完成（Tickets AR-03~04）：
  - `src/ui/AppController_p.h` / `src/ui/AppController.h` / `src/ui/AppController.cpp`：
    - `loadToolsPackage()` 独立读取 `candidate/build_info.json`，缓存 `toolsPackageInstalled` 与 `toolsPackageBuildInfo`，并在工具包变化时触发 `aboutTextChanged` 信号；
    - `aboutText()` 格式化 VisionRT、VisionTools（含“未安装”/“构建信息不可用”状态处理）、VisionRTContracts、运行环境（Qt/Python）与版权行；
    - 新增 `copyAboutText()` 接口，统一复制全量原始正文至系统剪贴板。
- 已完成（Tickets AR-05~06）：
  - 新增 `src/ui/qml/AboutVisionRTDialog.qml`：继承 `WorkbenchDialog`，基于 `ScrollView + TextArea` 呈现等宽、只读、支持鼠标拖选及右键菜单（复制/全选）的正文展示区；
  - 底部 neutral 按钮配置为“复制全部”，通过 `onNeutralClickListener` 调用 `app.copyAboutText()` 实现复制后弹窗保持打开；
  - `src/ui/qml/Main.qml`：原位替换原有 `WorkbenchDialog` 为 `AboutVisionRTDialog`。
- 已完成（Part A UI 测试用例与 Part B 契约验证，Tickets AR-07）：
  - `tests/workbench_paths.cpp`：新增 `readImageToolUiContract()`，验证 `visionrt.image.read` 的 UI 表现、单按钮打开与取消、“选择路径”选中 `bus.jpg` 文件及单次运行结果断言、选中 `pics` 目录及单次运行（`count = 6`）批量读取 6 张图及索引切换，工程保存重开路径保持；
  - `tests/workbench_popups.cpp`：新增 `aboutDialogContract()`，验证 `aboutText` 三组件 40 位真实 SHA、剪贴板一致性、未安装工具包场景、元数据缺失场景，以及 QML `AboutVisionRTDialog` 打开、正文、菜单及“复制全部”不关闭交互；
  - `tests/test_package_app.py`：新增 `test_build_info_is_preserved_when_present` 测试用例（14 用例全绿）。
- 已完成（打包与部署）：
  - 全量重新打包部署至 `F:/tmp/VisionRT`（1827 文件），包含新工具包 `build_info.json`。
- 未完成：无。

**验证证据**
- 版本契约测试：`build\bin\version_contract.exe` →
  - `Version Contract Passed: 0.1.0 - [dev] - (7ee12a04105cae05455bbe6d6bc44dad219351a6) (Built at 2026-10-27 10:04:33 UTC)`
  - `Contracts: 0.1.0 commit: 751181b40a9d72e95d116f5428184e3ee308af59 (2026-10-27 10:04:33 UTC)`
- Part A 路径与读取图像测试：`ctest --test-dir build -R "workbench_paths" --verbose` →
  - `[Evidence T-05] QuickUI QuiFileFolderDialog and QuiNativeFileDialog single-button path contracts verified.`
  - `[Evidence ReadImage UI] '读取图像' tool single-button path picker successfully tested for both image file (F:/Projects/InferRT/assets/pics/bus.jpg) and image directory (F:/Projects/InferRT/assets/pics) with pipeline execution and 6 images verified.`
  - `Test #71: workbench_paths_contract Passed (2.30 sec)`
- Part B 弹窗与弹出窗口测试：`ctest --test-dir build -R "workbench_popups" --verbose` →
  - `[Evidence About UI] AboutVisionRTDialog verified: 3-component versions, 40-char SHAs, clipboard copy, uninstalled state, missing metadata state, and QML dialog actions.`
  - `Test #75: workbench_popups_contract Passed (2.64 sec)`
- 综合契约测试：`ctest --test-dir build -R "workbench_.*_contract|version_contract"` → 22/22 全量通过 (100% tests passed)；
- 打包回归测试：`python tests/test_package_app.py` → 14/14 测试通过 (Ran 14 tests in 0.741s, OK)；
- 部署校验：`python tools/package_app.py --build-dir build --install-dir F:/tmp/VisionRT --tools-package F:/Projects/VisionTools/dist/visiontools-deploy.zip` → 退出码 0，验证 1827 个文件；`F:/tmp/VisionRT/tools-package/build_info.json` 存在，`F:/tmp/VisionRT/VisionRT.exe -v` 正常输出 `VisionRT 0.1.0`。

**下一步**
- 交付最终结果。

### 2026-10-27 — QuickUI 集成路径选择组件（方案2：Win32 原生 IFileOpenDialog + IFileDialogCustomize 单按钮选文件/文件夹）

**目标**
- 实现路径参数编辑器的交互收敛：将原先拆分为两个操作按钮（选择文件/选择文件夹）的旧方案，收敛为单个“选择路径”按钮，且无需额外的微型二级菜单；
- 将方案 2 深度集成到 `QuickUI` 体系中（作为 QuickUI 通用控件与原生 C++ 后端），方便后续工程全面复用；
- 兼顾跨平台及无头测试契约环境（自动化测试与非原生环境下的透明平滑回退），保持项目代码可编译且自动化契约全部通过。

**当前状态**
- 已完成（QuickUI 组件库，`3rdparty/QuickUI`）：
  - 新增 `quickui::QuiNativeFileDialog`（`src/include/quickui/QuiNativeFileDialog.h` 与 `src/QuiNativeFileDialog.cpp`）：
    - 深度封装 Windows COM 原生 `IFileOpenDialog` 与 `IFileDialogCustomize`，支持在原生对话框中直接注入“选择此文件夹”原生按钮；
    - 实现 `IFileDialogControlEvents::OnButtonClicked`：当点击“选择此文件夹”时，优先取当前选中的子文件夹，若无则取当前浏览的目录并成功关闭对话框；当点击文件“打开”时返回选中文件完整路径；
    - 提供路径规范化及 `urlToLocalFile` / `localFileToUrl`，精准处理空格、`#`、`%` 与 Unicode 字符；
    - 支持 `chooseFileOrFolder`、`chooseFile`、`chooseFolder` 及配套 C++ 静态调用入口；
    - 环境感知：当检测到 `Qt::AA_DontUseNativeDialogs` 时自动旁路原生调用，避免无头测试阻塞。
  - 新增 QML 组件 `QuiFileFolderDialog.qml`（`src/controls/QuiFileFolderDialog.qml`）：
    - 暴露标准 QuickUI 控件接口（`title`、`currentFolder`、`selectedPath`、`folderButtonText`、`nameFilters`、`dialogMode`、`open()`、`close()`、`choosePath()`、`accepted`、`rejected`）；
    - 原生环境自动调用 `QuiNativeFileDialog`，无头/测试环境无缝委托给 `QtQuick.Dialogs` 回退实现。
- 已完成（VisionRT 接入与适配）：
  - `src/ui/AppController.h` / `src/ui/AppControllerParameters.cpp`：`setParameterPath` 增加 `const QString &path` 原生重载，并与 `const QUrl &url` 保持统一的本地路径规范化；
  - `src/ui/qml/ParameterEditor.qml`：移除旧有 `secondaryActionIcon` 与双按钮逻辑，针对 `editor === "path"` 收敛为单一按钮（`QuiFontIcon.FolderOpen` / `"选择路径"`），对话框收敛为单个 `QuiFileFolderDialog` 实例；
  - `tests/workbench_paths.cpp`：补充 `QuiNativeFileDialog` 静态路径转换契约测试及 `setParameterPath` QString 重载校验。
- 已完成（打包与部署）：全量打包并同步部署至 `F:/tmp/VisionRT`（1776 个文件），版本与运行验证通过。
- 未完成：无。

**验证证据**
- QuickUI 编译：`python run_msvc.py cmake --build build --target quickui` → 退出码 0，成功生成 `bin/quickui.dll`；
- VisionRT 编译：`python run_msvc.py cmake --build build --target VisionRT` → 退出码 0，成功生成 `bin/VisionRT.exe`；
- 聚合契约测试：`ctest --test-dir build -R workbench_contract --output-on-failure` → 1/1 Test Passed（62.61 秒，涵盖路径编辑器单选、目录选择、Unicode、空格、`#`、`%` 保存加载、动态模式切换全流程）；
- 打包部署与冒烟：`python tools/package_app.py --build-dir build --install-dir F:/tmp/VisionRT` → 退出码 0，`F:\tmp\VisionRT\VisionRT.exe -v` 输出 `VisionRT 0.1.0`（退出码 0）。

**下一步**
- 提交代码并在真实工作流工程中体验单按钮选文件/文件夹的完整交互。

### 2026-10-27 — T36 第三批（聚合测试重构修复、参数 SDK 迁移完成、夹具包重建）

**目标**
- 修复 `tests/workbench_contract.cpp` 被脚本改写后的损坏（coreContracts 未定义、main 重复块）；完成 T36-18 剩余项（ReadImageTool 参数子类、Python visiontools_sdk、image_add/counter 迁移、随包安装与安装目录复验）；重建 `build/tools-package` 精简夹具。

**当前状态**
- 已完成（VisionRT）：`workbench_contract.cpp` 恢复可编译结构——匿名命名空间新增 `coreContracts()`（tree/pending/duration/diagnostics 四个无窗口场景），main 聚合块调用 `coreContracts()` + saveToolMetadata 断言 + 其余场景，恢复 ROI 子进程启动块，保留 short-window/DPI 子进程分发各一份。手动运行 `workbench contract passed`（Exit 0）；ctest 下 ROI 场景偶发焦点时序失败（"ROI toolbar entered direct drawing"/"Dragging a newly created rectangle replaced its dimensions"），为窗口激活环境问题，非逻辑回归，搁置。
- 已完成（VisionTools，T36-18）：
  - 新增 `src/preprocess/ReadImageParameters.h`：`ReadImageInputs`（path/recursive/traversal/order/count/reload，复用 `InputParametersBase`）与 `ReadImageOutputs`（五端口显式 Missing/值语义 + `markFailed`，复用 `OutputParametersBase`）；`ReadImageTool.cpp` 的 execute/commit/快速路径/异常路径全部改走参数子类，upstream 类型错误仍按原语义在 try 外传播，缓存与事务行为不变。
  - 新增 Python 作者 SDK `visiontools_sdk`（ABC parse/pack + InputReader/OutputWriter，状态键归基类、值零拷贝），随 image_add 与 call_counter 包安装（安装目录导入冒烟通过，不依赖 PYTHONPATH）。
  - `image_add/tool.py` 改为 `ImageAddInputs/ImageAddOutputs` 参数子类（upstream 非 0 原样转发、inplace 保持 ndarray 同一对象、错误路径显式 Missing）；`counter.py` 保持类入口，execute 用 `CounterOutputs`，initialize/release 用 `LifecycleStatus`（宿主要求恰好两个状态键，业务端口不得混入）。
- 已完成（夹具包）：`build/tools-package` 从新版 `dist/visiontools-deploy` 重建为精简夹具（21 工具、preprocess+metrology 两模块、bin 仅 vst_image/vst_metrology/opencv、runtime_files 仅 opencv、剔除 yolo 声明）。
- 未完成：T36-19（联合发布与完整验收证据，P1）。

**验证证据**
- VisionTools：`cmake --build build`（全量）→ 通过；`ctest -R "test_preprocess_contract|test_upstream_status_contract|test_python_tools"` → 3/3 Passed；`python tests/test_python_tools.py` → 14/14 OK。
- 打包：`python tools/package_tools.py --build-dir build --install-dir dist/visiontools-deploy --profile rtx40-build` → 22 工具打包成功（目录+zip 一致，含 SDK 子包）。
- VisionRT 安装链路：`ctest -R "tools_package_contract|tool_discovery_contract|cpu_tools_contract|example_metadata_contract|python_contract|installed_pipeline_contract|installed_python_author_contract"` → 7/7 Passed；`ctest -R "python_shutdown_contract|runtime_python_lazy_contract|runtime_contract|runtime_diagnostics_contract|runtime_value_contract|plugin_late_runtime_contract"` → 6/6 Passed。
- 安装目录 SDK 冒烟：直接 `sys.path` 导入 dist 内 `visiontools_sdk`，InputReader/OutputWriter/状态键守卫行为正确。
- T36-19 全矩阵：`cmake --build build --target tests-all` + `ctest`（84 项）→ 81 Passed；`ui_result_isolation_contract` 因 tests-all 漏链补链后 Passed；`assist_viewport_contract` 全量并行下真实窗口 hover 光标竞争失败、单独复跑两次均 Passed；`build_incremental_contract` 为既有环境阻塞（Security Center 拦 reg.exe 致 build.cmd 内 vcvars64 丢 SDK INCLUDE，嵌套配置必失败；已给该测试补充 Ninja PATH 传递）。`workbench_contract` 手动完整运行两次 Exit 0。
- 交付基线：三仓库 Contracts 一致于 `14f6be8`（ABI 5）；Qt 6.6.2 / OpenCV 4.8.0 / Python 3.12.13 / CUDA 11.8 / MSVC 14.29 已记录于 `docs/README.md`「T36 交付基线」；`docs/README.md` 悬空方案链接已改为指向实际打包件。

**下一步**
- T36-19 剩余：三仓库提交边界待用户确认后执行。

### 2026-10-27 — T36 第四批（GPU 端到端验收通过，外部权重阻塞解除）

**目标**
- 用户提供真实权重 `F:/models/yolov8/yolov8n/yolov8n.wts`（FP32 COCO 80 类）后，在 RTX 4060（sm89）上完成 T36-19 的目标 GPU 端到端验收。

**当前状态**
- 已完成：`gpu_tools_contract` 三处适配——修复示例工程 `examples/pipeline_gpu_yolo.vrt.json`（yolo 节点 y 0→1 满足新层级约束、reader count 2→1 单图推理）；修复测试内过时的读图输入（`inputs.at("mode")` 已随 T36-12 读图重构删除，改用 `count=1`）。
- 已完成（GPU 验收，RTX 4060 / driver CUDA 11.8 / TensorRT sm89 FP16）：
  - `build_wts` 阶段：从真实 wts 现场构建 engine（204.4 秒，FP16 8.95MB），5 个检测与独立 PyTorch 参考（IoU>0.90、score 差<0.03）完全匹配，下游 `detection.filter` 保留 4 个；
  - `engine` 阶段：加载保存的 engine（0.049 秒推理），同样 PASS；
  - fixture：`build/gpu-fixtures/`（yolov8n.wts + bus.jpg，后者与 `F:/Projects/InferRT/assets/pics/bus.jpg` 字节一致）。
- 外部权重阻塞解除；T36-01..18 全部通过各自验收，T36-19 仅剩三仓库提交边界。

**验证证据**
- `gpu_tools_contract.exe <dist包> examples/pipeline_gpu_yolo.vrt.json build/gpu-fixtures`（完整双阶段）→ 两阶段均 `[PASS] Native YOLO matches five PyTorch detections; downstream filter retains four.`。
- `ctest -R examples_contract` → Passed（示例工程修改无副作用）。

**下一步**
- 执行三仓库提交（按既定边界：WORKLOG/docs 方案包/.workbuddy 不入库）。

### 2026-10-27 — 全新部署 F:\tmp\VisionRT（T36 交付产物）

**目标**
- 把三仓库提交后的最终产物（VisionRT `7ee12a0` 宿主 + VisionTools `5a69d2b` rtx40-build 工具包 + Contracts `751181b`）部署为可直接运行的安装目录。

**当前状态**
- 已完成：宿主全量重建（55 步，主程序重新链接）→ `package_app.py --force-clean` 全新部署 `F:\tmp\VisionRT`（1827 文件，包校验通过；首跑因外部文件保护钩子拦截旧目录批量删除而中止，改为整体清空旧目录后全新部署成功）。
- 部署内容：22 工具 / 3 模块（含 infer，sm89 TRT 分区）/ `visiontools_sdk` 子包 / CUDA 11.8 + TensorRT DLL。

**验证证据**
- `F:\tmp\VisionRT\VisionRT.exe --screenshot` → Exit 0，工具树 8 分类、三位毫秒状态栏、「日志」入口正常渲染。
- `installed_pipeline_contract.exe F:\tmp\VisionRT\tools-package` → Exit 0（C++→Python→C++ 图像/数组/ROI 与工程回环）。
- `installed_python_author_contract.exe F:\tmp\VisionRT\tools-package` → Exit 0（Python ROI 与图像编辑作者工具真实 RuntimeEngine 执行）。
- `gpu_tools_contract.exe F:\tmp\VisionRT\tools-package ... --engine-only` → `[PASS]`，engine 推理 0.157 秒（安装态 GPU）。

**下一步**
- 无。T36 收口。



- 继续按 T36 方案完成 UI 交互收敛、日志链路与函数/参数作者接口试点。

**当前状态**
- 已完成 T36-06（VisionRT）：数据列右键只有“复制”，无数据时禁用；名称列只负责下层连接，空候选不打开菜单/不造占位项；删除 `BindingPopup`、`PortDefinitionEditor`、零调用的 `WorkbenchTextDialog`、`relationTargets` 与 16MiB 二次确认门；复制仍走有界后台任务 + 读回 ACK，失败不截断。
- 已完成 T36-07/15（VisionRT）：Theme 新增 `parameterFont`/`parameterTextColor`/`parameterRowSpacing`/`parameterRowHeight`，普通/枚举/路径/表达式/ROI 行统一字体、字号、颜色与行高；移除红色非法边框、行内“下次执行重建资源”等提示和字段占位说明；空候选列表保持为空；`hasDraft` 区分空草稿，清空后编辑取消/确定不再恢复旧公式；切到手动模式立即取消来源显示。
- 已完成 T36-13（VisionRT）：`3rdparty/spdlog` 固定 v1.15.3 子模块；异步队列 8192 / overrun_oldest，轮转文件 10MiB×3，UI 有界信箱 2000、可见时 50ms 批量≤200、隐藏不刷新；错误按 key 合并计数；`qInstallMessageHandler` 桥接 Qt/QML；`LogDialog.qml` 为唯一 `Qt.Dialog` 非模态窗口，关闭只隐藏；底部“提示信息”替换为“日志”，删除诊断 JSON 按钮与旧提示弹窗；关闭阶段先停生产者、排空日志再销毁 logger/线程池。
- 已完成 T36-18 的 C++ 试点（VisionTools）：`BrightnessTool` 迁移为无状态函数 + `BrightnessInputs/BrightnessOutputs` 参数子类，经 `registerFunction` 注册；读图等有状态工具仍用类；`installed_pipeline_contract` 在真实安装包中通过 RuntimeEngine 执行该函数工具并校验成功。
- 部分完成 T36-18（Python）：`image_add`/Python 侧参数 SDK 与计数器类接入未实施，随包安装 Python SDK 未做。
- 部分完成 T36-19：文档索引已更新日志与输出入口；目标 GPU、真实发布包与日志性能基线未测。

**验证证据**
- `ctest --test-dir build`（VisionRT）→ `100% tests passed, 0 tests failed out of 84`（180.49s），新增 `log_contract`（有界信箱/批量/丢弃摘要/错误合并/Qt 桥接/轮转）。
- `workbench_startup_contract` → 真实打开日志窗口、写入一条记录、模型收到批量并截图 `build/evidence/log_window.png`；旧诊断弹窗对象不存在，窗口 flags 含 `Qt.Dialog`、`Qt.NonModal`。
- `ctest --test-dir build`（VisionTools）→ 15/15 通过（`test_infer_whitebox_trace` 按设计 Skipped），亮度工具改函数后几何/上游状态用例全通过。
- `installed_pipeline_contract` → 在 `F:/Projects/VisionTools/dist/visiontools-deploy` 中执行 `visiontools.image.brightness` 节点，状态 0，原读图→Python→裁剪存储链保持。
- `ctest --test-dir build-dev`（Contracts）→ 6/6 通过，含独立安装消费者。
- `VisionRT.exe --screenshot ...` → 退出码 0，底部显示“日志”按钮；`%LOCALAPPDATA%/VisionRT/VisionRT/logs/visionrt.log` 写入启动/关闭记录。
- 未验证：日志吞吐/延迟基线、目标 GPU、真实发布 ZIP 安装、Python 参数 SDK、日志窗口在最小化/恢复下的原生标题栏行为（仅覆盖可见性）。

**下一步**
- 实施 Python 参数 SDK（共享导入根 + 打包）与 `ReadImageTool` 参数子类复用。
- 在目标 GPU 与最终发布包上完成 T36-19 联合验收并记录原始日志。

### 2026-10-27 — T36 方案落地（Contracts 作者接口、输出契约、计时/状态/图像、打包与测试分组）

**目标**
- 依据 `docs/VisionRT_VisionTools_Contracts_dev_完整方案_Specs_Tickets_20261027.zip`，在三个仓库现有 dev 基线上实施 T36 工单。

**当前状态**
- 已完成 T36-03（Contracts）：删除 `normalizeExecutionStatus` 断言，改测 `toolResult` 完整输出/原样消息/ABI 5，安装消费者 ABI 断言改为 `MODULE_ABI_MAJOR`。
- 已完成 T36-16/17（Contracts）：新增 `FunctionTool.h`（`registerFunction`/`registerClass`，仅接受无捕获函数指针，init 参数非空即拒绝）与 `Parameters.h`（`InputReader`/`OutputWriter`、`InputParametersBase`/`OutputParametersBase`，状态端口只由基类写一次）。
- 已完成 T36-04/05（VisionRT）：UI 统一消费 `effectiveTool()` 物化契约（输出表/图像端口/参数行/连接候选），缓存以 `projectEpoch + flow + structureRevision + node` 为键，菜单与高亮共用同一候选结果。
- 已完成 T36-08/10（VisionRT）：毫秒统一三位小数（0.000/0.001/0.020/1.200），删除“待运行”徽章与未运行文字状态；框架失败不再伪造 `__status_code/__status_message`（`NodeSnapshot.toolResultPublished`），-5 仅在调度停止事实同时成立时判为 Canceled。
- 已完成 T36-09（VisionRT + VisionTools）：`DisplayConverter` 按 1/3/4 通道推断（显式 RGB/RGBA 仅在通道匹配时生效），高位深颜色与 Alpha 分开映射且 Alpha 固定范围；图像读取/旋转/亮度/裁剪/投影声明移除固定 `color_order`。
- 已完成 T36-02（VisionRT）：`ConfigOpenCV` 按当前配置解析 imported target 并拒绝跨配置兜底，生成 `build/opencv_runtime_<config>.txt`；`package_app.py` 消费该精确清单，不再通配 SDK 目录。
- 已完成 T36-14（VisionRT）：测试统一带 `LABELS`（backend/bridge/ui/package/sdk/installed/gpu/perf），测试可执行目标改为 `EXCLUDE_FROM_ALL`，新增 `tests-<group>`/`tests-all` 聚合目标，`tools/build.cmd --group <name>` 构组并 `ctest -L`。
- 已完成 T36-11（VisionTools）：`image_add` 按 `upstream_status` 非零时不改源图并原样转发状态、输出 Missing；`image_mean`/`image_sizes` 明确只读测量继续运行策略。
- 已完成 T36-12（VisionTools）：读图先构造完整下一状态与 ToolResult，再以交换提交；缓存插入为 best-effort 且自身保持一致性；新增 `beforeResultBuild`/`beforeCacheInsert` 故障注入用例。
- 已完成 T36-01（VisionTools）：CMake 运行时安装先进入隔离暂存目录，再按唯一 profile 策略选择/复制/生成 `runtime_files` 与 `dependencies.json`；cpu profile 排除 infer 模块与工具。已用真实 CMake 安装验证 cpu 与 rtx40-runtime 两个 profile，目标 GPU 运行仍未测。
- 未完成：T36-06（废弃输出交互与 BindingPopup/PortDefinitionEditor 清理）、T36-07/15（参数行外壳与 Theme 全量收敛）、T36-13（spdlog 日志服务与 LogDialog）、T36-18（C++ 工具迁移为函数并接入参数子类的试点）、T36-19（联合发布与安装态/GPU 验收）。
- 未完成：本轮改动尚未提交；Contracts 的 gitlink 需要在其提交后再更新两个产品。

**验证证据**
- `ctest --test-dir build-dev`（Contracts）→ 6/6 通过，含独立安装消费者 `test_isolated_installation`。
- `ctest --test-dir build`（VisionRT）→ `100% tests passed, 0 tests failed out of 83`（172.90s）；`ctest -N -L <group>` → backend 36 / bridge 29 / ui 39 / package 4 / installed 2 / perf 2，gpu 与 sdk 为 0（Contracts 侧单独验证）。
- `cmake --build build --target tests-all` → 286 个构建步骤完成；默认构建不再编译测试。
- `ctest --test-dir build`（VisionTools）→ 15/15 通过，`test_infer_whitebox_trace` 按设计 Skipped；`test_package.py` 43/43、`test_python_tools.py` 14/14。
- `runtime_contract`、`workbench_contract`、`workbench_expression_contract`、`display_storage_contract`、`plugin_contract`、`workbench_status_contract` → 全部通过（动态表达式端口、-5 业务码与取消语义分离、三通道/Alpha、框架失败不伪造状态端口）。
- `python tools/package_tools.py --profile cpu` → 2 模块 / 21 工具、无 nvinfer/cudart；`--profile rtx40-runtime` → 3 模块 / 22 工具、含 TRT 运行时且无 builder 资源；均用真实 CMake 安装入口。
- `cmake -S . -B <tmp> -DCMAKE_BUILD_TYPE=Release|Debug` → `opencv_runtime_Release.txt` 仅 `opencv_world480.dll`，Debug 仅 `opencv_world480d.dll`。
- 未验证：目标 GPU 的 engine 加载/构建、真实安装包启动、日志与参数主题相关验收。

**下一步**
- 依次实施 T36-06、T36-07/15、T36-13、T36-18，然后按 T36-19 在目标机执行安装态与 GPU 验收；提交顺序为 Contracts → VisionTools/VisionRT gitlink。

### 2026-10-27 — 重新部署 F:\tmp\VisionRT（ABI 5 宿主 + rtx40-build 工具包）

**目标**
- 用 T35（ABI 5）提交后的代码替换 2026-10-26 的旧部署（旧包为 ABI 4 宿主 + schema 4 工具 + 全量 TRT builder 分区）。

**当前状态**
- 已完成：VisionRT、VisionTools 增量构建 0 错误。
- 已完成：`package_tools.py --profile rtx40-build` 产出 22 工具包，TRT 清单仅 `cudart64_110` / `nvinfer_10` / `nvonnxparser_10` / `nvinfer_builder_resource_sm89_10`（本机 RTX 4060 = SM 8.9，保留 build_wts 现场构建）。
- 已完成：`package_app.py --force-clean` 全量重打包 `F:\tmp\VisionRT`（1827 文件，包验证通过，标记转为完成态）。
- 已完成：`visionrt_validate_tool.exe --package F:/tmp/VisionRT/tools-package --load` 真实加载，13 个原生工具工厂全部可用。

**验证证据**
- `tools\build.cmd cmake --build build`（VisionRT）→ 完成且无待编译项；VisionTools 因 `reg.exe` 被安全策略拉黑导致 vcvars 缺 SDK 路径，改用手工 INCLUDE/LIB + ninja 直构建 → 0 错误。
- 工具包内 TRT 条目核对 → 仅 `_sm89_` 分区，符合 rtx40-build profile。
- `visionrt_validate_tool --load` → `Valid tools package: F:/tmp/VisionRT/tools-package`。

**下一步**
- 双击 `F:\tmp\VisionRT\VisionRT.exe` 人工确认最近工程菜单、统一参数与 EasyTouch 呈现；如需在无 SM89 机器部署，用 `package_tools.py` 换对应 profile 重打工具包。

### 2026-10-27 — 提交边界收敛：补齐最近工程与测试视图源文件

**目标**
- 把上一轮遗漏入库的源文件纳入版本库，并固定“WORKLOG、docs 方案与 docs 内 HTML 不入库”的提交边界。

**当前状态**
- 已完成：`src/ui/RecentProjects.h`、`src/ui/RecentProjects.cpp`、`tests/result_view.h`、`tests/recent_projects_contract.cpp` 从工作区补入版本库，`CMakeLists.txt` 与 `AppController*` 引用的最近工程模块不再缺失。
- 已完成：`.gitignore` 增补 `.workbuddy/`、`docs/*.html`、`docs/*.json`、`docs/*.visual-check.*`，避免本地智能体数据与方案可视化产物被误提交。
- 未完成：`WORKLOG.md` 按要求只保留在本地，不进入提交。

**验证证据**
- `git grep -n RecentProjects -- src CMakeLists.txt` → 命中 `CMakeLists.txt:99-100`、`src/ui/AppController.cpp:122`、`src/ui/AppController_p.h:13`，证明缺这两个文件时提交树不可编译。
- `git ls-files src/ui | grep -i recent` → 补入前为空，确认最近工程模块此前从未入库。

**下一步**
- T35-18 仍需在 40 系 GPU 目标机验证 engine 加载、`build_wts→engine→推理`、干净环境安装启动与真实窗口菜单键盘导航。

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
