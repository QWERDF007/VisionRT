# VisionRT 工具契约改进方案、Specs 与 Tickets

版本基线：2026-11-14。适用于 VisionRT、VisionTools、VisionRTContracts。本文是待实施的设计与验收依据。

本次复核在同一 dev 基线上扩展到工具名称、实例配置、读图状态、工程保存和交付验证。两仓实现与测试均为源码核查，未执行 Windows 构建、GUI 或 GPU 验收。下列行为差异可由代码分支推出；验收用例供实施后验证。

## 一、方案与决策

**删除 ToolServices。VisionRT 根据 JSON 声明展示参数编辑器，把最终的字符串值原样交给工具。工具开发者决定哪些字符串作为路径使用，以及如何读取或写入。** 对 VisionTools 内置工具，普通相对路径以运行该工具的可执行文件所在目录为基准。在桌面产品中，这就是 VisionRT.exe 所在目录。工程位置、工程是否保存、DLL 安装在哪个子目录、程序从哪个工作目录启动，均不参与这个计算。

例如软件在 D:/data/VisionRT，读图节点填写 images/a.png。节点收到的值是 images/a.png，读图工具尝试打开 D:/data/VisionRT/images/a.png。即便工程在 E:/projects/line-a 且该目录下恰好存在同名图片，工具仍只访问软件目录下的目标。该目标缺失时，读图节点报错并显示实际尝试的路径。

### 1. 三个仓库为什么都要改

| 仓库及核对版本 | 现有调用或行为 | 改造目标 |
|---|---|---|
| VisionRTContracts · 8070833 | ToolServices 只包含 PathResolver。工厂 create 接收服务指针，类注册要求能用 ToolServices 构造，函数适配器保存并传递一份服务。模块 ABI 主版本为 9。 | 删除服务类型及参数；类工具默认构造，函数工具只接收本次输入；原生模块 ABI 升到 10。 |
| VisionRT · 2a033a4 | NodeRuntime 用 configuration.directory 创建解析器。DomainToolAdapter 把服务传给原生工厂。PythonRuntime 在构造参数字典时识别路径端口并解析。ProjectStorage 另存时按工程目录改写，ExampleProject 按工程相对路径收集资源。 | 原生和 Python 都原样传值；保存、另存和示例复制均遵守新语义。宿主私有运行能力按现有调用者保留。 |
| VisionTools · a6784b1 | ReadImageTool 使用 services_.paths。YoloTool 保存 paths_，用于标签、模型、引擎输出。五个预处理工具仅保存 services_。读图的空值、无法解析、目标缺失、目标类型不支持共用 PathNotFound。 | 路径语义收回到工具实现；共享定位代码；错误按真实失败阶段报告；删除所有无效构造参数和成员。 |

基线代码：[Contracts 接口](https://github.com/QWERDF007/VisionRTContracts/blob/8070833/include/visionrt/contracts/Tool.h) · [工厂适配](https://github.com/QWERDF007/VisionRTContracts/blob/8070833/include/visionrt/contracts/FunctionTool.h) · [ABI 版本](https://github.com/QWERDF007/VisionRTContracts/blob/8070833/include/visionrt/contracts/Module.h) · [宿主实例创建](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/runtime/NodeRuntime.cpp) · [原生加载](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/plugins/PluginLoader.cpp) · [Python 转换](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/python/PythonRuntime.cpp) · [读图](https://github.com/QWERDF007/VisionTools/blob/a6784b1/src/preprocess/ReadImageTool.cpp) · [YOLO](https://github.com/QWERDF007/VisionTools/blob/a6784b1/src/infer/YoloModel.cpp)。

### 2. 目标接口和调用顺序

以下为核心签名。沿用现有命名空间、调用约定、状态类型和销毁入口。

```cpp
struct ToolFactoryDescriptor {
    const char* tool_id;
    IToolInstance* (VT_CALL *create)(ToolStatus*) noexcept;
    void (VT_CALL *destroy)(IToolInstance*) noexcept;
};

// 无状态函数工具的业务签名
ToolResult run(const Values& inputs);

// 有状态工具：默认构造；节点初始化参数由 initialize 接收
// initialize(const Values&) -> ToolStatus
// execute(const Values&) -> ToolResult
// release() -> ToolStatus
```

1. VisionRT 根据 JSON 声明创建路径编辑器。手工输入的相对文本保持相对；文件选择器选中的绝对路径按选择结果保存。
2. VisionRT 完成已有的输入取值、连线和表达式求值、类型检查。这里的“原样”指最终字符串值，不限制表达式产生新值。初始化参数和执行输入采用相同原则。
3. 原生适配层交付 Values，Python 桥将字符串转为等值 Python str。路径编辑器元数据不触发运行时转换。
4. 需要访问文件的 VisionTools 工具在读写前解释字符串。普通文本工具直接使用字符串。只在工具内部复用路径函数，不给实例构造函数添加目录参数。
5. 工具按原契约返回结果。输出端口 0、1 承载状态码和消息，业务输出从 2 起；输入端口 0 可承载上游状态，业务输入可从 1 起。工厂负责创建失败；实例创建失败或执行抛异常仍由宿主按既有框架诊断处理。

### 3. 工具内的路径规则

在 VisionTools 现有 common 层提供一个小型内部函数，供读图与推理调用。输入为已通过现有 UTF-8 校验的非空字符串，输出为本地文件系统路径。Windows 产品可用 GetModuleFileNameW(nullptr, ...) 取得进程主程序路径，处理缓冲区不足后取 parent_path()；若已有等价实现则复用。其他受支持平台按已有构建目标提供对应实现。获取失败要返回工具可报告的原因，不能回退到当前工作目录。命令行消费者或测试程序的基准是其自身可执行文件目录。

| 输入或场景 | 处理约定 |
|---|---|
| 绝对路径、完整 UNC 路径 | 使用指定位置；保留合法 Unicode 和空格。 |
| 普通相对路径，如 images/a.png、../shared/a.png | 与可执行文件目录拼接；允许显式上级目录。错误信息展示实际访问目标。 |
| 空字符串 | 工具决定必填或可选。必填读图路径报输入错误；可选引擎输出为空则跳过保存。 |
| 输出文件尚不存在 | 允许将路径交给写入阶段。创建父目录、覆盖和写入策略沿用工具的既有行为。 |
| 普通字符串、字符串输出 | 内容原样传递。生产工具自行决定输出原始文本还是实际文件名，消费工具解释自己声明的输入。 |

不对所有字符串做批量扫描或转换。不增加 Type::Path、不以目录存在与否选择其他基准、不引入全局 setCurrentDirectory。只在真正访问路径的 Python 工具中按同样规则定位；现有无文件操作的 Python 工具不需要路径辅助层。嵌入式 Python 如需取得主程序位置，按实际部署验证对应入口，不能用脚本目录或解释器安装目录冒充软件目录。

### 4. 读图和推理的错误归属

| 条件 | 责任与验收信息 |
|---|---|
| 读图必填值为空，或值类型错误 | 沿用已有公共输入错误体系，指明读图路径端口。 |
| 目标不存在 | ReadImageError::PathNotFound；消息包含实际访问路径。 |
| 目标存在但既非普通文件也非目录 | 输入值错误，说明只支持文件或目录。 |
| 目录无可读候选、文件读取失败、图像解码失败 | 保留既有分类及业务输出；继续支持目录遍历、排序、游标及重载。 |
| YOLO 的标签、模型或引擎输出失败 | 由对应初始化分支报告资源角色、目标和原因。写引擎失败应明确指出“引擎输出”，避免误导用户重新选择输入模型。 |
| 软件目录获取失败 | 工具报告无法确定可执行文件目录及系统原因；无需检查工程状态。 |

### 5. 工程、示例、升级与交付

- **工程持久化**。已保存、未保存、另存后的工程都保留字符串值。删除 ProjectStorage 中基于新旧工程目录的路径换算，核查文件选择器向模型回写的入口。JSON 正常转义允许变化，比较解码后的值。
- **产品示例**。现在的 ExampleProject 会按路径端口寻找工程相对资源。同步调整官方样图安装布局与示例 JSON，例如把样图安装到软件目录 examples/assets/a.png，参数填写 examples/assets/a.png。将示例工程复制为可编辑工程后继续引用同一安装资源。示例所需的工程文件仍照常复制；打包资源依据产品安装清单登记，避免把任意用户输入或尚未生成的输出路径当作待复制资源。
- **破坏性切换**。随产品交付的声明、模板、工程和测试直接换成当前格式。旧用户文件不作为兼容目标。删除过渡读取器、迁移脚本、旧工厂重载及仅供其使用的源文件、构建与安装条目。对仍承载其他功能的文件只删无用符号。
- **插件与 SDK**。ABI 9 → 10。两个消费者固定同一新版 Contracts 提交，所有原生 DLL、内置探针与安装 SDK 消费者重编。沿用加载器主版本检查，在调用不匹配的工厂前拒绝加载。Python 字符串内容变化写入升级说明，Python 工具按实际业务核查相对路径。
- **发布批次**。先发布新契约提交，宿主与工具分别完成适配后按同一安装包交付。需要撤回时整体换回匹配的宿主、插件及示例包。项目文件由用户控制。

源码入口：[路径端口识别](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/core/ToolDefinition.cpp#L357-L359) · [工程保存](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/core/ProjectStorage.cpp#L829-L843) · [示例资源](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/core/ExampleProject.cpp) · [读图错误码](https://github.com/QWERDF007/VisionTools/blob/a6784b1/src/preprocess/errors/ReadImageErrors.h)。

### 6. 工具注册、英文名与工程边界

发布清单中的 22 份 JSON 已逐一核对。顶层 `name_en` 同时是注册键：VisionTools 打包器用它生成索引的 `tool_id`；VisionRT 用它查声明和工厂；保存的节点使用 `tool_id` 找回工具。建议保留这个单一身份，英文名统一使用简短 PascalCase 功能名，中文名继续用于中文界面。工具语言与实现方式已由 implementation.kind 表达，工具树分类由 VisionRT 从声明目录读取。下表是拟采用的新名。

| 中文名 | 现有 name_en | 新 name_en / 注册 ID | 实现 |
|---|---|---|---|
| 图像尺寸 | `visiontools.python.image_sizes` | `ImageSizes` | python_function |
| 缺陷过滤 | `visionrt.detection.filter` | `FilterDetections` | cpp |
| 分支合流 | `visionrt.control.select` | `SelectBranch` | host |
| 跨流程接收 | `visionrt.flow.receive` | `ReceiveFlowResult` | host |
| 圆智能点选 | `visiontools.assist.circle` | `PickCircle` | cpp |
| 测量圆 | `visiontools.measure.circle` | `MeasureCircle` | cpp |
| 行间距智能点选 | `visiontools.assist.row_spacing` | `PickRowSpacing` | cpp |
| 测量行间距 | `visiontools.measure.row_spacing` | `MeasureRowSpacing` | cpp |
| YOLO目标检测 | `visionrt.dl.yolo` | `DetectObjectsYolo` | cpp |
| 条件分支 | `visiontools.logic.branch` | `ConditionalBranch` | python_class |
| 评估表达式 | `visiontools.logic.expression` | `EvaluateExpression` | python_class |
| 图像读取 | `visionrt.image.read` | `ReadImage` | cpp |
| 投影ROI | `visionrt.roi.projection` | `ProjectRoi` | cpp |
| 图像裁剪 | `visionrt.image.crop` | `CropImage` | cpp |
| 图像预处理 | `visionrt.image.preprocess` | `PreprocessImage` | cpp |
| 旋转图像 | `visiontools.image.rotate` | `RotateImage` | cpp |
| 自适应二值化 | `visiontools.preprocess.adaptive_binary` | `AdaptiveThreshold` | cpp |
| 调整亮度 | `visiontools.image.brightness` | `AdjustBrightness` | cpp |
| ROI几何 | `custom.python.roi` | `RoiGeometry` | python_function |
| 图像加一 | `custom.python.image_add` | `AddOneToImage` | python_function |
| 计算图像均值 | `custom.image.mean` | `ImageMean` | python_class |
| 调用计数器 | `custom.call.counter` | `CallCounter` | python_class |

实施时以发布目录的声明为准，更新原生工具 ID 常量、生成索引、内置和自定义示例、宿主内建节点引用、测试及作者文档。圆测量的 interaction.helper 明确写有旧 ID，行间距测量也需随其辅助节点一并核对。Python 包位置、入口符号、DLL 名、implementation.binding 和数字端口号有各自用途，只有实际引用工具 ID 的字段才替换。既有注册表负责检测重名，保留一份英文身份即可；不引入名称翻译服务。表中旧名称只供仓库内改名时定位引用。接受破坏性变更，运行时不并行维护两套注册名。

**工程与插件的边界。** 工程文件、流程图、工程路径、节点编号分配、已保存状态、跨流程结果读取、运行会话和节点配置快照留在 VisionRT。普通 C++、Python 工具只接收已声明的初始化参数和本次输入，返回声明结果；禁止通过隐式字典键、全局变量、环境变量或新上下文恢复工程注入。宿主已有的分支合流和跨流程接收继续由宿主实现。Receiver.cpp 的 ReceiveTool 确实调用 IResultReader::receive，移除 ToolServices 时保留该依赖在宿主私有创建路径。用户显式配置的路径字符串仍正常传递。

还须清掉宿主内部的间接耦合：NodeRuntime.cpp 89–109 将 directory 放进 InstanceConfiguration 并参与相等比较。移除路径注入后，也要从普通插件配置及其比较中删除这项；仅改变工程目录不应单独触发读图实例重建、游标重置或模型重新初始化。切换到另一个工程仍遵循正常的工程生命周期。

源码依据：[发布清单](https://github.com/QWERDF007/VisionTools/blob/a6784b1/tools/package_catalog.json) · [生成注册键](https://github.com/QWERDF007/VisionTools/blob/a6784b1/tools/package_tools.py#L190-L207) · [注册与查重](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/core/ToolRegistry.cpp#L89-L108) · [工程节点载入](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/core/ProjectStorage.cpp#L458-L477) · [实例配置比较](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/runtime/NodeRuntime.cpp#L89-L109) · [宿主接收工具](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/runtime/Receiver.cpp#L48-L106)。

### 7. 新增源码发现及最小修改

| 优先级 | 代码问题与触发条件 | 实施建议 |
|---|---|---|
| 高 | 读图执行丢弃 args.parse(inputs) 返回的 ToolStatus。readFields 会返回首个读参错误；Readings 对失败字段赋占位值，例如 bool 为 false。合法图片、错误类型的 RELOAD 在直接调用时可能继续进入读图。亮度工具已经正确处理同一接口的返回值。 | 仿照亮度工具，解析失败立即返回原状态与完整 Missing 业务端口。先保留上游跳过，再处理正常输入。空路径检查只保留一处；任何输入缺失都映射为 PathNotFound 的代码一起修正。 |
| 高 | 目录重扫后只在非空时替换计划。循环读过唯一图片、随后移走它，再次执行时计划指向的文件消失，重扫为空仍保留旧计划；若与上一批路径相同，能直接返回缓存图片。 | 利用已有的重扫结果，空目录立即返回 NoImagesFound。正常的非空重扫沿用原游标规则。无需目录监听、后台刷新或重试。 |
| 中 | ProjectStorage::load 会接受包含有效 JSON 的 .JSON 文件，而 save 用大小写敏感的扩展名比较，导致可打开的同一文件不能按原名保存。 | 保存端复用大小写无关的 JSON 后缀判定。沿用无扩展名补全、QSaveFile 成功后提交工程对象的流程。 |
| 中 | reconcile 先生成形如 inputs.端口 3 的诊断字段，再用前缀、最后一个空格、stoul 和 catch (...) 从展示文字中取回 PortId，决定是否增加 executionRevision。 | 在已经持有端口编号和参数类别的分支直接记录影响执行的信息；或给这一内部诊断附带已有的类型化端口引用。去掉中文字符串反向解析。保留非生效绑定不触发重建的规则。 |
| 清理 | ReadImageTool::decode 接收未读取的 catalog，复制整份 plan.keys 到无读者的 DecodedBatch::keys；DecodedBatch::decoded 只有递增；pending 作为出参填充后又移回同一个批次。 | 删除两个死字段和无用形参；直接向 batch.pending 写入。保留计划本身实际使用的 keys 及测试中的 decodeCount。目录有几千张图时，该修改去掉一次整目录键拷贝；未实测速度收益。 |
| 清理 | GeometryTransform 的 Alignment::isIdentity 在本次 VisionTools 源文件、测试及说明中只见声明与定义；五个 services_ 成员也只有存储动作。 | 删除未使用的内部 isIdentity 声明与定义及五个死成员。保留不同用途的 identity()。全仓检查删改后的旧工厂、适配器、路径 helper 与测试夹具；仅删除已确认失去全部调用者的符号。 |
| 验收 | 默认 CTest 的 test_installation_consumer 实参为构建目标 TARGET_FILE，能够证明构建树 DLL 可加载；单凭这个用例不能判定安装包内的 DLL 可加载。 | R-01 复用该消费者，额外以最终安装目录 DLL 为实参运行并记录实际路径。该项说明证据范围，不推断其他打包流程全部缺失。 |

源码依据：[读参和执行](https://github.com/QWERDF007/VisionTools/blob/a6784b1/src/preprocess/ReadImageTool.cpp#L304-L345) · [读参占位值](https://github.com/QWERDF007/VisionTools/blob/a6784b1/src/common/InputValues.h#L165-L176) · [目录刷新和上一批缓存](https://github.com/QWERDF007/VisionTools/blob/a6784b1/src/preprocess/ReadImageTool.cpp#L387-L446) · [批次结构](https://github.com/QWERDF007/VisionTools/blob/a6784b1/src/preprocess/ReadImageTool.h#L65-L79) · [工程协调](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/core/ProjectStorage.cpp#L908-L940) · [工程读写](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/core/ProjectStorage.cpp#L847-L862) · [保存后缀](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/core/ProjectStorage.cpp#L948-L958) · [几何函数](https://github.com/QWERDF007/VisionTools/blob/a6784b1/src/common/GeometryTransform.cpp#L40) · [消费者测试注册](https://github.com/QWERDF007/VisionTools/blob/a6784b1/tests/CMakeLists.txt)。

**已检查的复杂度。** 保留批次成功后再推进游标、失败后可重试和已发布图像的生命周期。已有 read_image 用例覆盖损坏文件、删除序列尾项、缓存引用和重试，这些行为有用户可见结果。插件 ABI 检查保护真实的跨版本加载。上述修改不新增互斥锁、重试层、缓存管理器或专门覆盖内存分配失败的框架。旧方案关于 Windows 特殊路径的单列策略收敛为遵循现有平台路径规则，不为这些输入单独设计兼容层。来源：[读图回归](https://github.com/QWERDF007/VisionTools/blob/a6784b1/tests/preprocess/read_image.cpp#L408-L522) · [加载器版本检查](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/plugins/PluginLoader.cpp#L274-L293)。

### 8. 工具声明：直接列出初始化参数、输入和输出

schema 6 的 ports 是 JSON 分组对象。读取后，三组数组分别进入 ToolDefinition.initParams、inputs 和 outputs。包装对象本身没有身份、属性或执行行为。新格式删除声明级 schema_version 及 ports 包装，顶层使用 init_params、inputs、outputs。空组可省略。端口数字 ID、类型、约束、可见性以及工程连线仍按原定义工作。初始化参数进入 initialize，输入进入 execute，输出用于解释和校验 ToolResult。

工具的端口 ID 只用于在当前方向内定位端口。业务编号允许复用；当前同方向的编号须唯一，保留状态端口仍遵循既有规则。工具声明和定义编辑器都无需为已删除的工具端口记录历史最大编号。新增端口从本组当前可用的业务编号中选择；已有端口增删、改名、调整显示顺序时，不主动重排其他端口。动态表达式节点的工程内部变量编号属于另一项编辑功能，本次工具更新不依赖该计数器。

**VisionRT 处理工具更新。** 打开工程或刷新工具目录时，以更新后的声明核对已有连线。沿原连线的节点、方向、端口 ID 查找两端。两端仍存在且当前解析后的完整类型相同，保留原连线，即使端口改名或编号曾被复用。当前两端类型不同，或新版声明删除了任一端口，实际移除该连线，同时清除目标输入的上游引用；仍存在的输入按现有未绑定规则处理。两个工具同时更新时以双方新声明为准。端口 ID 改变视为原端口删除；不在一组同类型端口中猜测替代目标。工具定义整体缺失与已取得新版声明是两种已有状态，缺失工具仍走原有缺失提示。

宿主弹窗标题为“工具已更新”。一次打开或刷新汇总本工程涉及的更新，列出工具名、受影响节点、保留和断开的连线数量；断开项写清两端和原因。即使所有连线均保留，也告知已检测到的工具更新。利用工具版本及工程中已有声明快照识别变化；当前工程格式的节点快照记录工具版本。更新的连线和快照随正常工程保存持久化。同一版本、同一声明重开后不重复弹窗。

现有加载代码会把不兼容输入的旧上游引用保留下来，并把上游输出已变化的有效连线也标为不可执行；这两个分支要同时改，避免出现界面提示断开、工程仍保存旧引用的情况。来源：[输入绑定恢复](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/core/ProjectStorage.cpp#L253-L295) · [输出变化与连线校验](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/core/ProjectStorage.cpp#L630-L686)。

**工具树分类来自声明目录。** VisionRT 在现有递归扫描中已经持有本次声明根和 JSON 文件路径。取文件父目录相对该声明根的各级名称作为分类，叶子显示 name_zh。内置工具使用 tools/builtin，自定义工具使用实际配置的声明根；根目录名与 JSON 文件名不计入分类。根目录直属声明沿用现有未分类展示。工具级 JSON 移除 ui.category；若 ui 仅包含分类，同步删除空对象。辅助工具的 visibility 与端口编辑器配置仍按各自职责处理。生成索引只记录定位声明所需的路径，分类无需另外存储，工具执行入口无需接收分类。

例如声明位于 tools/builtin/Python/示例/图像尺寸.json，树中显示“Python → 示例 → 图像尺寸”。归入“采集 → 图像信息”时，移动该声明到相应目录并更新发行清单的文件位置；刷新后原注册名 ImageSizes、Python 入口和工程连线继续有效。仅分类变化不触发“工具已更新”的契约提示。来源：[声明目录扫描](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/core/ToolPackage.cpp#L37-L62)。

以下是改造后的完整图像尺寸声明：

```json
{
  "name_en": "ImageSizes",
  "name_zh": "图像尺寸",
  "version": "1.0.0",
  "implementation": {
    "kind": "python_function",
    "package": "examples/image_sizes",
    "file": "tool.py",
    "symbol": "execute"
  },
  "inputs": [
    { "id": 1, "name_zh": "输入图像", "type": "image", "display": { "visible": true } }
  ],
  "outputs": [
    { "id": 2, "name_zh": "图像尺寸", "type": "vec_vec_int" }
  ]
}
```

修改 DefinitionJson 的根字段校验、数组解码和序列化，连同类型化 JSON 定位路径从 ports / inputs / i 改为 inputs / i。仓库内声明一次性改写为 init_params。同步迁移 22 份发布声明、模板、定义编辑器和测试夹具。发布索引、工程文件和 Python 入口有独立格式，只更新其中实际引用工具声明的部分。运行时只读写当前字段；用已有字段校验报告无效声明。删除旧版本分支和旧入口。工具 version 用于更新通知；模块 ABI 10 用于加载前检查。

代码依据：[数组和类型化字段解码](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/core/DefinitionJson.cpp#L52-L84) · [声明读取](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/core/DefinitionJson.cpp#L442-L462) · [声明写出](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/core/DefinitionJson.cpp#L533-L545)。

### 9. 新建空工程的保存提示

ProjectDefinition 的 dirty 成员默认初始化为 true。默认 ProjectEditor 使用默认工程，执行新建操作也构造 ProjectDefinition{}。关闭路径检查 editor.project().dirty 并发出 unsavedDecisionRequired，主窗口随后打开保存弹窗。于是从未编辑的空工程也满足提示条件。

将新建默认工程初始化为 dirty=false。用户实际增删流程或节点、修改参数后沿用现有标脏入口；保存成功再清除标记。关闭仍依据 dirty 判断。不要通过节点数、是否命名或有无保存路径绕过弹窗，否则用户删除已保存工程的最后一个节点后会丢失这项修改。

依据：[默认标志](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/core/Project.h#L125-L135) · [新建工程](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/ui/AppController.cpp#L934) · [关闭决策](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/ui/AppController.cpp#L891-L898)。


### 10. 日志系统现状与三个来源

源码核查基线沿用本文两仓提交。已有异步写盘、按日轮转、Qt 消息桥接、错误聚合、可见时批量推送、窗口缓存和基本底部跟随。LogDialog.qml 仍用 ListView 的逐行 TextEdit，只能在单行文本控件内选择；日志窗口没有“复制全部”“清空”按钮，模型已有 clear()。附件中的跨行选择和按钮属于待开发项。此处为源码核查，尚未验证 Windows GUI、剪贴板或性能。

**来源由产生消息的事件决定。** 时间、线程、等级、错误码、工具名称仍各有原来的用途。窗口、复制内容及当日日志文件的来源位置只出现以下三种值。

| 来源 | 纳入范围 | 示例 |
|---|---|---|
| 系统 | VisionRT 管理工程、节点及连接关系、流程运行调度、模块载入、后台服务、Python 环境检测和日志设施 | 打开或保存工程、绑定或解绑端口、请求停止、加载 DLL 失败、Python 环境不可用 |
| 界面 | 窗口、画布、参数编辑器及交互反馈，Qt Quick/QML 的显示与交互诊断 | 参数草稿被拒绝、请先选择节点、点选时坐标变换不可用 |
| 工具 | VisionTools 原生或 Python 工具在自身初始化、执行、释放中给出的业务消息与错误 | 图像读取失败、模型初始化失败、圆拟合失败 |

**现有来源怎样迁移**

| 当前实际来源 | 新来源与处理 |
|---|---|
| 工程、运行、连接 | 系统。按钮触发的宿主工程操作也归系统。 |
| 参数 | 已核对的“参数草稿被拒绝”归界面。工具执行时拒绝业务参数归工具。 |
| 智能点选 | 已核对的界面对齐矩阵警告归界面。辅助测量工具本身的执行错误归工具。 |
| 应用 | 删除统一标签。report() 目前总写“应用”，需在知道原因的调用点指定来源并传到日志入口。工程打开失败、Python 环境检测失败归系统；节点选择提示归界面；来自工具本身的业务诊断归工具。 |
| Qt 动态 category，或回退 Qt | 处理器对可识别的 QML、Quick 或控件消息记界面；其余 Qt 底层或未指明来源的消息记系统。必要的原生 category 留在正文或详情。 |
| 来源为空 | 队列丢弃摘要、日志目录不可写等设施提示归系统。 |

**实现边界**。在 VisionRT 的现有日志接口使用三个固定值（如内部枚举），由一个格式化入口生成中文来源。应用诊断仍可在界面显示，同时由原日志入口写一次；分类在知道事件含义的位置给出，不解析中文正文猜来源。原生及 Python 工具已通过状态和详情交付业务结果；需要记录的业务诊断由宿主接收时标为工具。插件加载、解释器探测、框架拒绝执行等宿主失败记系统。普通工具成功返回不自动新增逐节点流水日志。工具端无需引入 LogService、工程对象或日志回调。保留当前等级过滤、批处理和错误合并机制；正文保留具体业务对象，避免不同工具的错误失去辨识度。

源码依据：[日志正文与跟随](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/ui/qml/LogDialog.qml) · [Qt 桥接与服务诊断](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/log/LogService.cpp#L484-L528) · [应用诊断与参数编辑](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/ui/AppControllerDiagnostics.cpp#L11-L54) · [连接](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/ui/AppControllerBindings.cpp#L162-L185) · [Python 环境结果](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/ui/AppController.cpp#L1118-L1174)。

### 11. 工具卡片悬停时显示工具 API 耗时

用户反馈当前安装版本的 tooltip 仅显示预处理、后处理。核对 dev 基线发现中间行已存在，名称为“算子时间”；DurationText 只在相应调用已测量且数值有效时生成该行。AppController 将阶段写入 timingPhases，经 NodeListModel 传到 FlowCanvas 的悬浮卡片。实际安装包的缺行原因仍需复现。检查时先核对构建版本及同一节点同一轮的快照、阶段列表和界面，不另设计时通道。

目标顺序为 **预处理 → 工具 API 耗时 → 后处理**。中间行表示本次执行入口的墙钟时间。原生工具使用当前 instance->execute 调用边界的 adapterExecuteMs（包括适配器自身的少量工作），Python 工具使用真实函数或类方法的 pythonCallMs。等待 GIL、输入映射、输出转换沿用已有分段，不能在中间行重复累计。工具内部更细的算法耗时不在这个指标内。实际执行且被测得的 0 显示 0.000 ms；未进入工具调用则不给出虚构的时长。沿用节点运行中、跳过、阻塞和取消的现有显示规则。

代码依据：[运行时执行计时](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/runtime/NodeRuntime.cpp#L487-L498) · [耗时取值与阶段生成](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/ui/DurationText.cpp#L40-L85) · [卡片悬停渲染](https://github.com/QWERDF007/VisionRT/blob/2a033a4/src/ui/qml/FlowCanvas.qml#L700) · [现有三行测试](https://github.com/QWERDF007/VisionRT/blob/2a033a4/tests/flow_canvas_scenarios.cpp#L451-L482)。

## 二、Specs：可验证的行为规范

| ID | 要求 | 验收方法与通过条件 |
|---|---|---|
| S01 契约 | 已安装头文件删除 ToolServices、契约 PathResolver、工厂服务参数。删除已无生产者的 PortIssueKind::PathUnresolved 及描述分支。 | 使用安装后的 SDK 构建最小类工具和函数工具；两者都无需上下文。核查保留的枚举值，显式保持已有数值以减少无关变化。 |
| S02 生命周期 | registerClass 支持默认构造；registerFunction 只收 Values。创建、销毁在插件侧配对；创建异常按现有状态返回。 | 类实例跨执行保留状态；两个节点状态独立。函数工具执行、初始化参数校验、失败和销毁通过已有适配器测试。 |
| S03 原生传值 | 路径编辑器只影响编辑交互；初始化和执行的字符串值等于宿主求值后的字符串。 | 用回显工具分别接收字面量、连线、表达式产生的 images/a.png，三处都收到原值；普通文本、空串及中文空格路径同样保真。 |
| S04 Python 传值 | inputDictionary 保留类型校验和标准转换；去掉路径专用预解析。 | Python 回显工具的初始化和执行均收到原字符串；字符串形似路径但仅做文本处理时也可正常返回。 |
| S05 定位 | 读图与 YOLO 共用 VisionTools 内部的定位规则；绝对与普通相对路径行为符合方案。 | 将软件、DLL、工程、当前工作目录分置。在各处放不同内容的同名图片，必须读到软件目录版本；删除该版本后报告缺失。 |
| S06 业务错误 | 存在性与格式检查按输入角色执行。 | 覆盖空读图路径、不存在、坏图、空目录；可选引擎输出为空应跳过保存；指定尚不存在的输出文件可进入写入流程。 |
| S07 持久化 | 保存及另存保留参数文本；工程状态不决定能否用相对路径。 | 新建未保存工程运行，保存到 A、另存到 B，再重启打开 B；参数字符串和实际访问目标一致。 |
| S08 示例 | 已交付示例按软件目录组织资源。 | 在干净安装目录打开并运行一个真实读图示例；复制为可编辑工程后运行成功；移走安装样图后准确报错。 |
| S09 ABI | 同版消费者与 ABI 10 模块联通；旧模块按已有诊断拒绝。 | 加载 ABI 10 插件并创建、执行和销毁工具；再加载 ABI 9 产物，看到主版本不匹配且未进入工厂。 |
| S10 回归 | 保留已有状态端口、框架诊断、Python 类型校验、非路径服务及读图批次行为。 | 从安装包运行类工具、函数工具、Python 工具，抽查 Once/Node/Loop；读图目录连续执行仍按原规则推进和结束。 |

**新增验收项**

| ID | 通过条件 |
|---|---|
| S11 名称一致 | 打包目录 22 个新名全局唯一，索引与声明、适用的原生工厂一致。中文工具树保持中文，英文名及节点注册键使用表中功能名。测量辅助、跨流程接收、Python 和官方工程能按新名运行；未知 ID 仍由现有机制报告。 |
| S12 工程隔离 | 普通原生和 Python 工具入口没有工程对象、目录或隐藏上下文字段；更改工程存放目录不改变普通插件的实例配置比较结果。跨流程接收仍能经宿主读取源节点结果。 |
| S13 读参错误 | 直接调用读图工具，合法图片加上字符串型 RELOAD，返回该端口的类型错误，五个业务输出均 Missing；随后以合法输入执行仍从原游标位置读取。 |
| S14 目录清空 | 含一张图的目录成功循环读取后移除该图，再次执行直接报告无可用图像，业务输出 Missing。非空目录删除尾项时现有顺序/逆序及循环测试通过。 |
| S15 工程保存和重建 | 有效 .JSON 工程可打开并按原名保存。修改诊断展示文案不影响 executionRevision；生效初始化参数、当前字面量及未激活绑定对修订号的影响与现有行为一致。 |
| S16 可维护性和安装证据 | 目标死成员、参数、死函数连同声明和定义清除；读图原有缓存、游标及所有权回归通过；消费者以实际安装 DLL 运行。记录源码扫描范围、构建配置和真实模块路径。 |

**S17 声明格式验收**。用新版解析器读取上例并重新导出，输入 1、输出 2、类型和显示属性完全一致；再用含初始化参数、默认值和约束的原生工具验证类型化字段往返。所有发布声明均无 ports 包装，固定工具无 next_port_id。工具定义编辑器可重新使用已删除的业务编号。带旧包装或声明级 schema_version 的文件由常规字段校验拒绝。

**S18 更新与连线验收**。同 ID 同类型改名或复用，原连线继续执行；源或目标变更导致两端类型不同，连线从图、执行依赖和保存文件中移除；删除端口后即使另有同类型端口也不自动转接。两端同时改为同一新类型，保留连线。一次更新一次汇总弹窗，零断开也提示；保存重开后结果一致且不重复提示。分别通过打开工程和已有目录刷新入口验证。

**S19 分类验收**。两个不同声明根使用同一相对分类时分类结果一致；直接位于根下的声明仍可见；覆盖一层及多层目录、重新扫描和辅助工具隐藏行为。移动同一份声明后，树的位置随目录更新，保存声明不写回分类，注册名与连接不变。

**S20 空工程关闭**。启动后直接退出、新建后未编辑直接关闭，均不弹保存提示；添加节点、仅修改流程、删除已保存工程最后一个节点之后关闭均提示；保存成功后关闭不再提示。

**S21 清理交付物**。核对源码、模板、构建目标及最终安装目录，只保留当前声明读写和工厂调用。删除只支撑旧格式的代码、样例、生成文件和安装规则，删除已失去读者的字段；在干净构建目录构建、打包并运行现有消费者。

**S22 日志窗口与来源验收**。按附录 LOG-S01～LOG-S52 和 AT-01～AT-18 验证窗口。再分别触发保存工程、端口解绑、Python 环境不可用、编辑器草稿被拒绝、工具读取不存在的图像，验证来源依次为系统、系统、系统、界面、工具。同一业务事件只记录一次；核对文件与窗口的同一条记录标签一致。Qt 桥接与日志服务自身的摘要也在三类内。

**S23 工具卡片耗时**。在交付安装包中分别成功执行一个原生工具和一个 Python 工具，悬停核对三行顺序及中间值与该轮快照相符。预处理和后处理均有值时，已执行且有有效测量的中间行不得消失；已测零值保留，未进入 API 的用例不伪造耗时。

**联合验收样例**：将可执行文件置于 D:/data/VisionRT，DLL 置于其 plugins 子目录，工程置于 E:/projects/demo，从 F:/launch 启动。给 images/a.png 配置一张特征可识别的测试图，分别记录界面值、原生入口值、Python 入口值、实际读取的图片和缺失时报告的路径。前三者均为 images/a.png，最后两项均对应 D:/data/VisionRT/images/a.png。验收报告保存构建提交、运行命令、结果和失败项。上表是计划验证项。

## 三、Tickets：按仓库拆分的实施工单

| 工单 | 仓库及主要落点 | 实施内容 | 依赖及完成证据 |
|---|---|---|---|
| C-01 | VisionRTContracts · Tool.h、Registry.h、FunctionTool.h、Issue.h/.cpp、Module.h | 删除服务、解析器及相关分支；更新模板约束、函数指针、适配器与示例；主版本 9→10。保留 createWith 异常映射、destroy 和现有状态封装。 | 首项；提交 SDK 安装头与双类型最小消费者的编译执行结果。S01、S02、S09。 |
| H-01 | VisionRT · NodeRuntime、ToolRegistry、PluginLoader、DomainToolAdapter | 更新子模块，去掉原生工厂服务参数、适配器存储和节点创建时的路径注入；梳理 PlatformToolServices 中 reader 等宿主私有依赖，仅在其真实消费者之间传递。 | C-01；真实 DLL 可创建、执行、销毁；路径和有状态工具记录通过。S02、S03、S10。 |
| H-02 | VisionRT · PythonRuntime、Python 适配器与调用入口 | 移除 inputDictionary 中按 isPathPort 解析的分支，删除失去用途的形参，覆盖初始化与执行。保留字典端口号、类型校验和异常映射。 | C-01，与 H-01 配套；回显工具记录中文、相对、空值；已有 Python 工具用例通过。S04、S10。 |
| H-03 | VisionRT · ProjectStorage、路径编辑器回写及 tests/project_storage_contract.cpp | 移除按工程目录换算的保存与另存逻辑；核查编辑器、加载、复制节点、另存及重开对字符串的处理。 | 可先行；提交前后 JSON 值及 A/B 工程回归记录。S03、S07。 |
| T-01 | VisionTools · common、预处理/计量/推理模块注册、探针和样例 | 更新子模块；迁移全部工厂调用、类构造与函数签名；清理 AdaptiveBinary、DetectionFilter、ImageCrop、ImagePreprocess、RoiProjection 的死成员。实现工具包内部路径函数。 | C-01；全模块可编译，安装 SDK 消费者通过；记录基准目录与当前工作目录不同的验证结果。S01、S02、S05。 |
| T-02 | VisionTools · ReadImageTool、ReadImageErrors、tests/preprocess/read_image.cpp | 在工具访问文件前定位；区分空值、目标缺失、目标类型不支持、读取与解码错误。保留文件排序、缓存、游标和声明输出。 | T-01；提供正反两组同名图、空目录与损坏图的结果，核对返回码及中文消息。S05、S06、S10。 |
| T-03 | VisionTools · YoloModel、推理测试；扫描内置 Python 工具 | 模型输入、可选标签、引擎输出按各自角色定位。逐个核对实际打开文件的 Python 工具，普通字符串处理工具无需改动。 | T-01；提交三类路径的结果。实际引擎加载、构建与保存由具备推理依赖的环境验证，并标明软硬件版本。S04–S06。 |
| H-04 | VisionRT · ExampleProject、安装资源清单、官方示例 JSON、tests/example_project_contract.cpp | 按安装布局登记样图，替换工程相对资源收集逻辑，保证示例打开、复制和执行。 | H-03、T-02；提交干净安装目录清单、示例路径值、成功运行与移除样图后的报错记录。S07、S08。 |
| R-01 | 三仓联合 · CI、安装包、SDK 作者指南、升级说明 | 锁定同一 Contracts SHA；更新 ABI 元数据、测试夹具、探针、SDK 示例和文档。检查旧工厂、旧术语及依赖工程目录的断言；按 S01–S23 联测。 | 前述工单完成后发布；提交 SHA 清单、ABI 9 拒绝/10 成功记录、安装包示例及全部验收结果。 |

**补充工单与依赖**。以下工单和上表一起执行；R-01 是最终发布工单，依赖这些新增工单。涉及同一文件时直接合并到现有改动，避免独立建层。

| 工单 | 归属 / 依赖 | 具体交付及验收 |
|---|---|---|
| N-01 | VisionTools，同时协调 VisionRT；先确定第 6 节映射 | 一次更新 22 个声明、辅助工具引用、C++ 常量和官方发布夹具；用现有打包脚本重建索引；按配置核对包中实际收录的工具子集。S11。 |
| N-02 | VisionRT；依赖 N-01 | 将映射应用到官方工程、宿主注册引用、示例、UI 样例及测试。分别验证建节点、保存、重开、测量辅助绑定和 Python 执行。按新注册名执行；产品中删除旧名别名、迁移分支及仅服务旧名的夹具。S11。 |
| H-05 | VisionRT；与 H-01/H-02 同改 | 从普通插件实例配置及相等比较移除工程目录，梳理创建点；仅在宿主内部接收节点保留 reader。以相同参数更改目录验证配置等价，并验证跨流程接收。S12。 |
| H-06 | VisionRT；并入 H-03 | 统一 JSON 保存后缀识别；去掉 reconcile 从展示字段解析端口的逻辑。扩充 project_storage_contract 的原文件保存、有效参数和休眠绑定用例。S15。 |
| T-04 | VisionTools；并入 T-02 | 读图消费 parse 的失败返回；空目录重扫立即报错。分别增加 S13、S14 的最小顺序用例，沿用现有 read_image 测试入口。 |
| T-05 | VisionTools；与 T-01/T-04 协调 | 删除第 7 节点名的内部未使用符号；简化批次 pending 写入；保持测试注入点、已发布图像引用和实际使用的缓存功能。S16。 |
| R-02 | 两仓联合；在 R-01 前 | 从最终安装目录运行与新版同步的原生消费者；GUI 运行中文工具树和示例，分别验证 Python 与 host 节点；列明未具备环境的用例。S09–S12、S16。 |

**D-01｜VisionRT、VisionTools 声明格式迁移**。与 N-01 同批改声明，与 R-01 同批发布。VisionRT 修改声明读写与编辑，清除工具定义历史编号维护；VisionTools 修改声明、模板、依赖旧 JSON 路径的脚本和测试。用完整安装包加载 22 个工具并执行 ImageSizes，按 S17、S19 验收。VisionRT 扫描时生成目录分类，ToolTreeModel 按层级展示；删除工具级 category 的读写、作者模板和 22 份发布声明中的重复配置，保留仍有用途的 ui 字段。

**H-07｜VisionRT 工具更新处理**。与 D-01、H-06 配套，覆盖加载、目录刷新、连线调整、工程快照与更新弹窗。用原工程升级声明验证 S18；把新产生的连接状态与更新摘要交给宿主界面，插件入口保持原有职责。

**H-08｜VisionRT 空工程误报**。修改默认 dirty 初值，核对启动、新建、保存和关闭入口，扩充既有关闭流程测试，按 S20 验收。

**R-03｜三仓清理**。按 S21 核对本次被替代的配置、源文件、符号及安装内容，提交删除清单并注明原用途。R-01 在本项之后执行。

**T-LOG-09｜VisionRT 来源收敛**。更新日志入口、report 调用点、工具诊断交接、Qt 桥接及服务自身提示；替换原自由来源的相关断言。与附录 T-LOG-01～08 合并验收，参见 S22。R-01 完成前运行四项现有日志契约测试与目标平台窗口测试。

**H-09｜VisionRT 工具卡片耗时**。用实际安装包复现，核对 measured 标志、耗时、timingPhases 和 QML 展示，修复首个丢失环节；若为旧构建则更新安装产物。将现有中间行统一命名为“工具 API 耗时”，同步更新 workbench_contract、flow_canvas_scenarios 的断言，按 S23 在目标 Windows 界面验收。

**完成标准**。实现提交与测试证据逐项对应 S01–S23；从安装后的产品运行验收样例；旧 DLL 的拒绝信息与旧工程相对路径的行为变化进入发行说明。原 contract-review.md 的讨论结论可在实施完成后归入 SDK 编写指南与 docs/README.md 索引。


# 附录：日志窗口实施方案、Specs、Tickets 与验收矩阵

以下为附件的窗口实施要求，来源示例及服务接口已按正文第 10 节调整。

## 二、现有架构与保留内容

### 2.1 现有架构

```text
业务模块 / Runtime / Qt 日志
           │
           ▼
       LogService
           │
           ▼
     spdlog 异步队列
           │
     ┌─────┴──────┐
     ▼            ▼
 每日日志文件   BufferedSink（UI 信箱）
                  │
               分批推送
                  │
                  ▼
               LogModel
                  │
                  ▼
             LogDialog.qml
```

这一分层继续保留：

- `LogService.h/.cpp`：日志采集、异步写入、文件轮转、等级控制、错误聚合、Qt 消息桥接和 UI 分批通知；
- `LogModel.h/.cpp`：保存最近的有界日志记录，提供行文本和等级；
- `LogDialog.qml`：负责窗口显示、选择、滚动和工具栏；
- `AppController`：仅承接 QML 与 C++ 剪贴板之间的必要接口。

### 2.2 现有参数保持不变

| 参数/约定 | 当前值/行为 | 本次处理 |
|---|---|---|
| 异步日志框架 | spdlog | 保留 |
| 异步队列容量 | 8192 条 | 保留 |
| UI 窗口缓存 | 2000 条 | 保留 |
| UI 批量间隔 | 50 ms | 保留 |
| UI 单批最大量 | 200 条 | 保留 |
| 单条消息最大长度 | 4096 字节 | 保留 |
| 日志文件 | `logs/visionrt_YYYY-MM-DD.log` | 保留 |
| 文件轮转 | 每日轮转 | 保留 |
| 日志等级 | 由现有设置管理 | 保留 |
| 窗口生命周期 | 非模态独立窗口；关闭时隐藏 | 保留 |
| 窗口隐藏时 | 暂停 UI 推送，日志服务继续运行 | 保留 |

继续使用现有格式，例如：

```text
[2026/11/11 14:08:22.106] [I] [1234] [系统] 开始执行流程
[2026/11/11 14:08:22.127] [I] [1234] [工具] 图像读取： 图像读取完成
[2026/11/11 14:08:22.208] [E] [1234] [工具] 圆测量： 圆拟合失败
```

> `LogModel` 当前由 `QVector<LogRecord>` 维护有界记录，达到上限时移除前部旧数据。本次无需为 2000 条的容量改成自研环形缓冲区。

---

## 三、日志窗口 UI 设计

### 3.1 最终结构

```text
┌──────────────────────────────────────────────────────────────────────────────┐
│ 日志                                                              ─  □  × │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                 [复制全部]   [清空]        │
├──────────────────────────────────────────────────────────────────────────────┤
│ [2026/11/11 14:08:22.106] [I] [1234] [系统] 开始执行流程                    │
│ [2026/11/11 14:08:22.127] [I] [1234] [工具] 图像读取： 图像读取完成              │
│ [2026/11/11 14:08:22.134] [D] [1234] [工具] 圆测量： 输入参数校验通过       │
│ [2026/11/11 14:08:22.185] [W] [1234] [工具] 圆测量： 有效边缘点数量较少    │
│ [2026/11/11 14:08:22.208] [E] [1234] [工具] 圆测量： 圆拟合失败            │
│                                                              │           │
│                                                              │ 滚动条    │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 布局规范

1. 沿用现有的独立非模态 `Window`，窗口大小及最小尺寸不变。
2. 工具栏在日志正文上方，仅放置两个按钮，靠右排列，顺序为 **“复制全部”“清空”**。
3. 工具栏不加标题说明、图标说明文字或二级菜单。
4. 日志正文填满窗口剩余空间，随窗口大小变化自适应。
5. 按钮使用项目现有 QuickUI / VisionRT 主题组件及图标风格，不自创一套控件。
6. 日志正文沿用现有等宽程度、字号和颜色习惯；长日志允许按宽度自动换行。
7. 日志为空时正文显示空白，不额外写“暂无日志”提示。
8. 不做搜索栏、筛选栏或跟随按钮。

### 3.3 工具栏按钮

| 按钮 | 图标建议 | 功能 |
|---|---|---|
| 复制全部 | `copy` | 将当前窗口缓存的全部日志按行复制到系统剪贴板 |
| 清空 | `trash-2` | 清空 `LogModel` 当前历史和界面正文，不删除日志文件 |

空日志时两按钮禁用；日志到来后恢复可用。若现有控件没有便捷的模型行数绑定，可由 `LogModel` 暴露一个只读 `count` 属性，不为此新增控制类。

---

## 四、功能与交互设计

### 4.1 跨行选择与快捷键

正文应像普通只读文本编辑器一样：

- 可以选择单行中的任意片段；
- 可以从第一行中间拖选到第三行中间；
- 可以跨几十行连续选择，而不是逐条选择整行；
- `Ctrl+C` 复制当前选区；
- `Ctrl+A` 全选正文；
- `Ctrl+A` 后 `Ctrl+C` 可以复制全部显示正文；
- 文本不可编辑；
- 复制结果为纯文本，自动换行不应在原文中增加新行；
- 中文、路径、时间戳及已转义字符保持原文。

**不采用 `ListView` 的 Shift/Ctrl 整行多选**，因为它不能满足“鼠标任意跨行拖选部分文字”的目标。

### 4.2 “复制全部”

点击按钮时：

1. 从 `LogModel` 获取当前保留的全部日志；
2. 按原始顺序用 `\n` 拼接；
3. 写入系统剪贴板；
4. 不改变正文选区和滚动位置。

复制范围仅为当前窗口缓存，**不读取历史磁盘日志**。

不通过 `TextArea.selectAll()` 模拟按钮，因为那会改变用户已选内容。直接从模型获取完整文本更加简单、明确。

### 4.3 “清空”

点击按钮时：

1. 调用现有 `LogModel::clear()`；
2. 模型重置后，正文清空；
3. 自动跟随恢复默认底部状态；
4. 日志服务和磁盘文件完全不变；
5. 后续到达模型的新日志继续正常显示。

**不需要二次确认，不生成清空成功的日志，也不增加额外提示。**

清空的是已保存在 `LogModel` 的窗口历史；不要求额外清理尚未推送到模型的 `BufferedSink` 数据。这样与现有模型的职责保持一致，不增加复杂的同步机制。

### 4.4 自动跟随

用户无需认识或操作“跟随模式”：

| 场景 | 行为 |
|---|---|
| 首次打开窗口 | 定位到最新日志 |
| 当前位置在底部 | 新日志到来后自动滚到底部 |
| 用户向上滚动 | 不再自动拉回底部 |
| 用户在历史位置选取文字 | 保持用户阅读位置，不主动跳转 |
| 用户手动滚动回最底部 | 自动恢复跟随 |
| 清空后再次出现日志 | 自动显示最新日志 |

仅需要一个简单的底部判断：

```text
接收新批次前，检查当前是否在底部
  ├─ 在底部：追加后定位到新底部
  └─ 不在底部：追加后保持原有阅读位置
用户返回底部后，下次追加自然恢复跟随
```

**不要额外引入 Running / Paused / Selecting 等状态机。**

### 4.5 等级颜色

保留现有色彩语义：

| 日志等级 | 颜色 |
|---|---|
| Trace / Debug / Info | `Theme.textPrimary` |
| Warn | `Theme.warning` |
| Error / Critical | `Theme.error` |

颜色只属于显示效果，不能改变日志原文、选择和复制结果。

---

## 五、技术实现方案

### 5.1 正文控件选型

建议将每行独立 `TextEdit` 改为**单一只读 `TextArea`**，放在 `ScrollView` 内。

核心结构示意：

```qml
ScrollView {
    id: logScroll
    anchors.fill: parent
    clip: true

    TextArea {
        id: logText
        readOnly: true
        selectByMouse: true
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.Wrap
        font: QuiFont.Body
        color: Theme.textPrimary
        selectionColor: Theme.highlight
        selectedTextColor: "#ffffff"
    }
}
```

上述为实现结构，不是完整可复制的最终 QML 文件；工具栏、滚动条和布局应直接使用项目现有组件。

选择理由：

- 文本只有一个原生选区，可以跨日志行拖动；
- 原生提供选择、复制、全选；
- 支持 `append()`、`remove()`、`clear()` 等文本编辑接口；
- 当前缓存上限只有 2000 条，先采用原生控件，而不是开发自定义虚拟文本编辑器。

**性能原则：** 追加日志时做增量更新，避免每批都执行 `text = allText()`。最终需在目标 Qt / Windows 环境验证实际滚动、换行和刷新表现；若单一文本控件不能满足实际性能，再根据测试结果调整，而不是预先构建复杂备选框架。

### 5.2 模型与正文同步

现有 `LogModel` 已有以下能力：

```cpp
Q_INVOKABLE void clear();
Q_INVOKABLE QString text(int row) const;
```

通过模型标准信号更新 `TextArea`：

| 模型事件 | 正文操作 |
|---|---|
| `rowsInserted` | 按本批新增行号读取文本，批量追加 |
| `rowsRemoved` | 从正文开头移除对应数量的日志记录 |
| `modelReset` | 从模型重新初始化正文；清空模型时正文自然为空 |
| 窗口创建后 | 从模型建立一次初始显示 |

**处理原则：**

- 每个 `LogRecord` 对应正文中的一条逻辑日志行；
- 自动换行后的视觉行不参与日志条数计算；
- 新数据到来尽量批量更新文本；
- 文本必须与模型当前记录一致；
- 文本裁剪只按模型淘汰的记录数进行；
- 不引入第二套长期完整日志缓存。

对于删除正文前部记录的实现，可在 QML 中记录各条逻辑日志在正文中的字符长度，删除旧记录时计算需要裁剪的前缀，再调用 `TextArea.remove()`。这里需要注意分隔符 `\n`，不允许产生空行或把两条记录拼接到同一行。

该长度记录只服务于文本裁剪，不承担新的日志数据存储职责。

### 5.3 日志等级高亮

单一 `TextArea` 默认统一文本颜色。使用一个轻量的 `QSyntaxHighlighter` 根据已有日志行的 `[W]`、`[E]`、`[C]` 等标记着色。

建议放在 UI 层：

```text
src/ui/LogHighlighter.h
src/ui/LogHighlighter.cpp
```

主要职责：

- 判断每个文本段对应的日志等级；
- 为该行应用主题颜色；
- 不修改文本、不解析业务日志内容、不增加业务逻辑。

可以通过 Qt Quick 提供的文本文档接口关联高亮器；应按当前项目最低 Qt 6.5 的公开接口实现，避免依赖私有 Qt Quick 内部类。不通过 C++ 直接重写 Qt Quick 内部文档来控制追加或删除。

如选择更简单的实现，应以**保留原始文本、跨行选区和复制正确**为前提；不能为了颜色退回独立行委托。

### 5.4 `LogModel::allText()`

在 `LogModel.h` 增加：

```cpp
Q_INVOKABLE QString allText() const;
```

在 `LogModel.cpp` 增加：

```cpp
QString LogModel::allText() const
{
    QStringList lines;
    lines.reserve(rows_.size());

    for (const auto &record : rows_)
        lines.append(record.line);

    return lines.join(QLatin1Char('\n'));
}
```

需要包含相应的 `QStringList` 头文件。

沿用原有 `QVector<LogRecord>`，**不改造成自研环形容器**。

若为了控制按钮禁用状态需要暴露数量，可以直接增加只读属性和简单的数量变更信号；不需要另外封装一个按钮状态管理类。

### 5.5 复制全部接口

`AppController.h` 增加：

```cpp
Q_INVOKABLE void copyAllLogs();
```

`AppController.cpp` 的核心实现：

```cpp
void AppController::copyAllLogs()
{
    QGuiApplication::clipboard()->setText(
        impl_->logModel->allText()
    );
}
```

此处以项目现有 `impl_` 成员及对象归属为准，不新增额外 Controller。

QML 的“复制全部”按钮调用 `app.copyAllLogs()`；“清空”按钮调用 `app.logModel.clear()`。

### 5.6 来源收敛与已有写盘机制

来源约束与 Qt 桥接分类按总方案第 10 节更新。保留以下已有机制：

- spdlog 的队列初始化和溢出策略；
- Sink 管理；
- UI 信箱和定时批量推送；
- 生产日志等级设置；
- 文件轮转；
- 错误合并逻辑；
- 关闭与重启生命周期。

不因为“可能会发生某种竞态”引入新的锁、代号、屏障、额外队列或测试钩子。

---

## 六、Specs：功能规格与验收条件

### SPEC-LOG-01：窗口布局

| ID | 需求 |
|---|---|
| LOG-S01 | 保留非模态独立窗口 |
| LOG-S02 | 维持现有窗口大小及最小尺寸 |
| LOG-S03 | 顶部工具栏只放“复制全部”“清空” |
| LOG-S04 | 两按钮右对齐，使用已有 UI 样式 |
| LOG-S05 | 正文填满剩余空间，支持窗口缩放 |
| LOG-S06 | 关闭窗口只隐藏，不销毁 |
| LOG-S07 | 空日志时正文保持空白 |
| LOG-S08 | 不出现搜索、筛选或跟随按钮 |

**完成标准：** 布局与现有 VisionRT 一致，窗口缩放无溢出和无意义空白。

### SPEC-LOG-02：文本选择与显示

| ID | 需求 |
|---|---|
| LOG-S09 | 所有日志属于同一个可连续选取的文本区域 |
| LOG-S10 | 支持鼠标选取单行任意字符范围 |
| LOG-S11 | 支持鼠标跨行连续拖选任意字符范围 |
| LOG-S12 | 支持 `Ctrl+A` 全选 |
| LOG-S13 | 支持 `Ctrl+C` 复制选区 |
| LOG-S14 | 文本只读，不可编辑 |
| LOG-S15 | 保留日志等级颜色 |
| LOG-S16 | 日志文本、中文、特殊字符显示正确 |
| LOG-S17 | 长日志按窗口宽度折行 |
| LOG-S18 | 自动折行不改变实际复制内容 |

**完成标准：** 从第一行中间拖选到第三行中间，复制得到的仅是实际选区，而不是强制整行复制。

### SPEC-LOG-03：复制全部

| ID | 需求 |
|---|---|
| LOG-S19 | 提供“复制全部”按钮 |
| LOG-S20 | 范围为当前 `LogModel` 全部记录 |
| LOG-S21 | 按原始顺序拼接 |
| LOG-S22 | 日志间使用换行分隔 |
| LOG-S23 | 复制结果是纯文本 |
| LOG-S24 | 不改变原有选区、滚动位置 |
| LOG-S25 | 无日志时按钮禁用 |
| LOG-S26 | 不读取全部历史磁盘文件 |

**完成标准：** 粘贴到记事本的内容与窗口所保留的逻辑日志一致。

### SPEC-LOG-04：清空

| ID | 需求 |
|---|---|
| LOG-S27 | 提供“清空”按钮 |
| LOG-S28 | 点击后清空 `LogModel` 当前记录 |
| LOG-S29 | 正文同步清空 |
| LOG-S30 | 不删除/截断磁盘日志 |
| LOG-S31 | 不修改日志等级和设置 |
| LOG-S32 | 不停止日志采集和写入 |
| LOG-S33 | 后续新日志继续正常显示 |
| LOG-S34 | 不弹二次确认 |
| LOG-S35 | 不生成“清空成功”日志 |
| LOG-S36 | 清空后回到默认的底部跟随行为 |

**完成标准：** 清空后窗口为空，后续新日志正常显示，既有磁盘日志仍可读取。

### SPEC-LOG-05：自动跟随

| ID | 需求 |
|---|---|
| LOG-S37 | 打开窗口默认定位到最新日志 |
| LOG-S38 | 在底部时，新日志自动向下跟随 |
| LOG-S39 | 向上滚动后不自动跳到底部 |
| LOG-S40 | 滚回底部后自动恢复跟随 |
| LOG-S41 | 选取历史文字期间不主动拉回底部 |
| LOG-S42 | 不设置单独跟随按钮 |
| LOG-S43 | 滚动区域无越界滚动行为 |
| LOG-S44 | 窗口隐藏时不做无意义的文本渲染 |

**完成标准：** 持续产生日志时，可正常浏览历史、复制内容，再返回底部继续看最新内容。

### SPEC-LOG-06：缓存与更新

| ID | 需求 |
|---|---|
| LOG-S45 | 继续复用已有 `LogModel` |
| LOG-S46 | 缓存容量仍为 2000 条 |
| LOG-S47 | 超出容量时移除最早的记录 |
| LOG-S48 | 正文内容与模型记录一致 |
| LOG-S49 | 新日志批量增量追加，不逐批整体重建正文 |
| LOG-S50 | 保留 50ms 批量刷新间隔 |
| LOG-S51 | 保留每批最多 200 条的现有设置 |
| LOG-S52 | 不修改 spdlog 和日志文件写入机制 |

**完成标准：** 长时间滚动追加后正文仍能正确选择、复制，逻辑日志数与模型保持一致。

---

## 七、Tickets：可执行开发任务

### T-LOG-01：重构日志正文

- **优先级：** P0
- **修改文件：** `src/ui/qml/LogDialog.qml`
- **工作：** 用单一 `TextArea + ScrollView` 替换独立行 `TextEdit` 的 `ListView`；保持只读、折行与主题样式。
- **验收：** 单行与跨行任意选区、`Ctrl+A`、`Ctrl+C` 正常。
- **对应规格：** LOG-S09～LOG-S14、LOG-S17～LOG-S18。

### T-LOG-02：实现模型与文本的增量同步

- **优先级：** P0
- **修改文件：** `src/ui/qml/LogDialog.qml`
- **工作：** 处理 `rowsInserted`、`rowsRemoved`、`modelReset`；新增日志批量追加，淘汰旧日志时裁剪对应正文前缀。
- **验收：** 超过 2000 条后仍只显示当前模型记录，没有多余空行、漏行、错行。
- **对应规格：** LOG-S45～LOG-S51。

### T-LOG-03：保留日志等级颜色

- **优先级：** P1
- **修改文件：** `src/ui/LogHighlighter.h/.cpp`、必要的 UI 注册位置、`CMakeLists.txt`
- **工作：** 以 Qt 原生 `QSyntaxHighlighter` 为不同等级着色；只影响视觉，不改原文。
- **验收：** 警告、错误、致命日志颜色与现有主题一致；复制文字为原文。
- **对应规格：** LOG-S15～LOG-S16。

### T-LOG-04：增加“复制全部”按钮

- **优先级：** P0
- **修改文件：** `LogModel.h/.cpp`、`AppController.h/.cpp`、`LogDialog.qml`
- **工作：** 增加 `LogModel::allText()`，由现有 `AppController` 调用系统剪贴板，增加按钮。
- **验收：** 全部当前缓存日志按行复制，保留顺序和换行，不改变滚动与选区。
- **对应规格：** LOG-S19～LOG-S26。

### T-LOG-05：增加“清空”按钮

- **优先级：** P0
- **修改文件：** `LogDialog.qml`
- **工作：** 增加按钮；直接复用 `app.logModel.clear()`；响应模型重置清空正文。
- **验收：** 仅窗口历史被清除；新日志继续显示；磁盘文件不变。
- **对应规格：** LOG-S27～LOG-S36。

### T-LOG-06：实现基于滚动位置的自动跟随

- **优先级：** P0
- **修改文件：** `LogDialog.qml`
- **工作：** 追加前判断是否位于底部；底部自动滚动，上滚保留阅读位置，回到底部自然恢复。
- **验收：** 持续写入时浏览历史不被打断，无需跟随按钮。
- **对应规格：** LOG-S37～LOG-S44。

### T-LOG-07：统一日志窗口 UI 样式

- **优先级：** P1
- **修改文件：** `LogDialog.qml`
- **工作：** 布置右对齐“复制全部”“清空”工具栏，调整边距、文本与滚动条，复用现有主题组件。
- **验收：** 与软件风格一致、缩放正确、无多余 UI。
- **对应规格：** LOG-S01～LOG-S08。

### T-LOG-08：测试与回归验收

- **优先级：** P0
- **修改文件：** 现有日志测试及必要的 QML UI 测试
- **工作：** 验证文本选择、复制、清空、自动跟随、缓存容量，运行已有日志契约测试。
- **验收：** 新功能通过验收，原有服务行为不退化。

---

## 八、代码变更清单

| 文件 | 计划变更 | 是否新建 |
|---|---|---|
| `src/log/LogService.h` | 将来源约束为系统、界面、工具 | 否 |
| `src/log/LogService.cpp` | 统一格式化来源，处理 Qt 桥接与服务内部消息 | 否 |
| `src/log/LogModel.h` | 增加 `allText()`，必要时增加 `count` 只读属性 | 否 |
| `src/log/LogModel.cpp` | 实现 `allText()`，保留原有容量逻辑 | 否 |
| `src/ui/qml/LogDialog.qml` | 重构正文、增加两个按钮、增量同步和自动跟随 | 否 |
| `src/ui/AppController.h` | 增加 `copyAllLogs()` | 否 |
| `src/ui/AppController.cpp` | 使用剪贴板复制模型文本 | 否 |
| `src/ui/LogHighlighter.h` | 简单日志等级高亮器 | 是 |
| `src/ui/LogHighlighter.cpp` | 简单日志等级高亮器 | 是 |
| `src/ui/qml/Main.qml` | 保留原有日志窗口入口 | 否 |
| `CMakeLists.txt` | 注册新增高亮器/测试文件 | 否 |
| `tests/` | 补充功能测试，保留现有日志契约测试 | 按需 |

**注意：** 新增高亮器仅为 Qt Quick 文本显示服务，不增加新的日志业务抽象或独立 Controller。

---

## 九、验收测试矩阵

| AT | 操作 | 预期结果 |
|---|---|---|
| AT-01 | 鼠标选中单行部分文字 | 只选中该片段 |
| AT-02 | 从第一行中间拖到第五行中间 | 得到连续跨行选区 |
| AT-03 | `Ctrl+C` 复制跨行选区 | 剪贴板内容与选区一致 |
| AT-04 | `Ctrl+A` → `Ctrl+C` | 复制整个正文 |
| AT-05 | 点击“复制全部” | 复制当前 `LogModel` 全部记录 |
| AT-06 | 无日志时查看两个按钮 | 按钮禁用 |
| AT-07 | 点击“清空” | 正文与模型当前历史清空 |
| AT-08 | 清空后再次执行流程 | 新日志继续显示 |
| AT-09 | 清空后检查当日日志文件 | 旧日志仍在文件中 |
| AT-10 | 停留底部时连续产生日志 | 自动跟随最新内容 |
| AT-11 | 上滚浏览历史，同时继续产生日志 | 不被拉回底部 |
| AT-12 | 手动滚到底部 | 自动恢复跟随 |
| AT-13 | 连续产生超过 2000 条日志 | 模型与文本保留最新记录 |
| AT-14 | 输出 Debug / Info / Warn / Error | 颜色符合原设计 |
| AT-15 | 输出中文、路径、长文本与特殊字符 | 正确显示与复制 |
| AT-16 | 调整窗口尺寸 | 正文换行及布局正确 |
| AT-17 | 关闭日志窗口再打开 | 窗口正常显示，文件日志持续写入 |
| AT-18 | 持续产生日志并手动滚动、选取 | 正常操作，无明显持续卡顿 |

### 9.1 必须运行的原有测试

```text
tests/log_text_contract.cpp
tests/log_aggregation_contract.cpp
tests/log_lifecycle_contract.cpp
tests/log_level_contract.cpp
```

原有日志文本、批量聚合、生命周期及等级契约继续保持通过。

### 9.2 新增测试建议

- `LogModel::allText()` 对顺序、换行、空模型和容量淘汰的验证；
- QML 跨行选区、快捷键、两个工具栏按钮；
- 上滚暂停、回到底部恢复跟随；
- 清空不影响磁盘文件。

**不为假设性竞态增设复杂并发测试框架**，只测试本次实际改变的用户行为及原有回归内容。

---

## 十、实施顺序

| 顺序 | Ticket | 阶段目标 |
|---|---|---|
| 1 | T-LOG-01、T-LOG-02 | 先实现跨行选择和正确的增量文本显示 |
| 2 | T-LOG-04、T-LOG-05 | 实现复制全部、清空 |
| 3 | T-LOG-06、T-LOG-03 | 完善自动跟随和等级着色 |
| 4 | T-LOG-07、T-LOG-08 | 统一 UI，完成测试与验收 |

