# VisionRT EasyTouch 与训练实施方案

**设计主线是 VisionTools 发布工具 JSON，VisionRTContracts 定义并校验其含义，VisionRT 按声明使用现有工作台。** 普通工具继续显示原参数、图像和结果。有训练能力的工具沿用“加入训练”和“应用训练数据”两个入口。正在编辑的原图、区域、标签与参数只在 VisionRT 内存中。只有“应用训练数据”调用训练器且成功，才允许写入训练资产。

本文为拟实施设计。代码基线为 2026-11-18 核对的 VisionRT `dev`、VisionTools `dev` 和二者共同引用的 Contracts 提交。11 张线缆图的数据来自随包历史实验。

## 1. 一份工具 JSON 怎样决定工作台行为

能力由可选声明本身决定。处理页点选写在 `authoring.easy_touch`，训练界面写在 `authoring.training`，训练图点选写在 `authoring.training.easy_touch`。后者只在训练页解释。一个作用域只有一套点选行为，不需要选择助手的控件。

| 工具及声明 | 处理页 | 训练页 | 点击图像的效果 |
| --- | --- | --- | --- |
| ReadImage 等普通工具，省略 authoring | 现有正常界面，原有工具栏、图像、参数和结果按原布局展示 | 无训练页签、记录栏和加入训练入口 | 原有缩放、平移、选择及工具本身的 ROI 操作 |
| MeasureCircle / MeasureRowSpacing，仅 easy_touch | 现有正常界面，全局 EasyTouch 开启且当前条件满足时可点选 | 无训练界面 | 预览圆或条带。点击一次，将完整 ROI 和声明的普通输入参数一并写入节点 |
| RecognizeColor，仅 training | 原有检测圆、检测参数及五工位结果。原图可通过现有入口加入训练 | 显示记录、标签及手绘取样区 | 训练图沿用手绘。点击“应用训练数据”建模 |
| AlignStraightBorders，training 内含 easy_touch | 显示当前图、找到的边和姿态结果 | 显示参考原图、两条搜索区域及训练参数 | 在训练图悬停预览、单击替换、Ctrl+单击追加。点击“应用训练数据”拟合并发布参考边模型 |

训练页和处理页也可分别声明各自的点选行为。切页只激活当前页的映射。全局 EasyTouch 开关保留原位置及用户偏好。切到普通工具时它不会触发助手，也不会替用户更改开关值。界面复用已有按钮、页签、右键菜单、字段编辑器和画布手势。不添加按钮，也不给普通工具预留训练栏宽度。

## 2. 现有代码已经做到哪里

| 核对项 | 代码事实 | 对设计的影响 |
| --- | --- | --- |
| 工程和工具接口 | VisionRT `dev` 为 `8d1f6de7`，VisionTools `dev` 为 `97b8fc87`。两仓均锁定 Contracts `76bc8b6e`，模块 ABI 为 10，`PortValue` 还没有训练集 | 以两仓实际 gitlink 作为 SDK 基线。扩展值合同，同时把工具 JSON 的公共声明、读写和校验从宿主拆入 SDK |
| 工作台 | 已有处理/训练切换、训练记录、ROI 手柄、悬停候选和后台释放。处理态有批量写入字面量输入的事务。现有工具栏的 EasyTouch 是全局用户开关，训练页签由 trainingAvailable 控制 | 保留画布、参数页和手势，扩展现有事务和后台调度 |
| 取图与编辑 | 处理态助手已有从上游快照取得原图的路径。训练加图却取 `displayedImage()` 并保存 PNG，训练助手重新用 Qt 转灰。添加首图还可能先写入节点训练路径 | 训练图取声明输入所对应的原始帧。首次加入只持有内存像素，不创建文件、不改节点路径 |
| 直边 | `TrainingSession::save` 用矩形中心和长轴构造 `from/to`。`AlignStraightBordersTool` 初始化只加载几何。点选助手返回极性，训练提交只收 ROI | 宿主不再生成边端点。训练器必须在参考图像素中找到边，并将拟合结果写入模型 |
| 颜色 | `RecognizeColorTool::initialize` 调用 `ColorModel::train(loadColorTraining(...))`。应用按钮先写训练资产，再重载节点 | “应用训练数据”调用训练器，训练成功后一次写入来源与模型。运行初始化只加载模型 |
| 声明与调度 | `TrainingKind` 分直边、颜色。处理态存在 `firstRoiInput` 和固定助手端口含义。圆与行间距已分别声明输出映射 | 删除宿主的业务种类判断，但保留普通工具注册、调用、状态和有界调度 |
| 保存与另存 | `ProjectStorage::save` 用 `QSaveFile` 原子替换单个工程 JSON。`ExampleProject` 判定安装示例归属。现有示例另存不会收集资源 | 新增仅针对训练资产的复制和发布。普通工具路径继续按现有产品目录规则解释 |
| 当前输入覆盖 | 仓库里线缆示例的读取目录只有 `cable_11.png`，训练目录另有同内容副本。压缩包带 11 帧结果和图像摘要，没有另 10 张原图 | 参考图与清单的 SHA-256 已核对一致。其余 10 张原图的取得和核对是 11 图发布验收的前置项 |

**包内历史实验说明了优先级。** 两份结果各有 11 条记录。原配置只有 03、05、11 完成对齐和识色，其余 8 张均在右边定位返回 105006。仅将右侧搜索带短边从 44 改为 88 px，11 张均返回了五个工位结果。另一方面，在参考图上将底边名义端点与搜索中心沿法线一起移 10 px，矩阵的 Y 平移从 +0.896 变为 −9.049 px。前一个实验说明需要设置搜索容差，后一个说明需要从图像中重新训练参考边。两项修复要分别证明。

## 3. 工作台的两个训练入口

| 操作 | 编辑图、字段和画布 | 对工程与磁盘的影响 |
| --- | --- | --- |
| 加入训练 | 从声明绑定的原始图像端口取得当前帧，持有独立像素，进入已有训练页。直边选一张参考图，颜色可让多条标签记录引用同一张原图 | 零文件写入。节点初始化参数、工程修改标记和当前可运行模型均保持原值 |
| 点选、画框、拖动、改标签或训练参数 | 只更新当前节点在内存中的数据。直边第一条可见并可修改，选齐两条后可点击现有应用按钮。颜色可连续添加多个区域和标签 | 零文件写入。助手候选、预览线、缩略图、序列化副本和临时 PNG 都不写出 |
| 应用训练数据 | 对本次内存输入做完整性校验，交给声明的训练器。直边拟合并显示实测参考线，颜色按标签建模。保留当前训练页，可用现有“处理”页签查看结果 | 算法成功且产物通过内存校验后写出完整资产。文件就绪再一次更新节点引用与输出参数 |
| 本次操作失败 | 标签为空、边不足或训练失败时，使用已有控件状态和诊断提示具体原因。输入仍在内存，可修改后再次点击同一按钮 | 算法或内存校验失败时零文件写入。写盘失败时保留上一份完整结果，清理本次未发布文件 |
| 切页、换节点、保存或关闭 | 同一工程内切换可继续编辑。保存按现有工程流程执行。关闭工程、删除节点或退出后释放其编辑内存 | 保存、另存和自动恢复均只处理已成功发布的结果。普通工程的未保存修改沿用已有关闭提示 |

**运行采用节点最近一次成功发布的结果。** 已有模型时，可切回处理页查看其输出并继续运行。首次训练尚未完成时，运行该节点提示先完成训练。单独运行上游读图、运行其他工具和保存工程均沿用现有规则。画布在训练页展示当前编辑和预览。引用旧模型时，其实测线只在与当前训练输入一致时叠加，避免误认成刚刚训练的结果。

直边与颜色共用同一生效规则。悬停、点击第二条边、ROI 鼠标释放和参数失焦都不会自动发布模型。关闭工程前仅存在内存中的取样会随工程关闭释放，重新打开只加载上次保存的工程引用。

## 4. 重设 VisionRTContracts，让 JSON 可由三仓共同解释

今天的 Contracts 提供 PortValue、TypeSpec、ROI、工具生命周期、模块 ABI 等接口。`PortStage/PortRef`、训练种类和交互映射则定义在 VisionRT 的 `ToolDefinition.h`，主要读写逻辑在依赖 Qt 的 `DefinitionJson.cpp`。这会让工具作者能写出 JSON，却只能等宿主加载后才知道含义是否正确。新设计在 SDK 中增加**工具声明合同**，由两仓链接同一套实现。普通工具的现有字段和运行接口继续适用。

| 所属层 | 拟公开的数据或接口 | 所负责的决定 |
| --- | --- | --- |
| Contracts 基础值 | 沿用 PortId、TypeSpec、PortValue、ROI、Array、DataRef、ToolResult。将 PortStage、PortRef 的唯一定义移入 SDK。增加 TrainingSet | 端口的阶段与编号、字段类型、值如何通过 C++ DLL 或 Python 桥传递 |
| Contracts 声明模型 | ToolDeclaration 描述 JSON 自身的身份、实现元数据、端口和可选 AuthoringSpec。EasyTouchSpec 可被处理和训练作用域复用。TrainingSpec 包含图像来源、记录/参数、训练调用、结果映射 | 某工具支持哪些交互，哪些字段可读、可写，哪种输出构成训练结果 |
| Contracts 声明读写 | 提供 readToolDeclaration(UTF-8 JSON)、writeToolDeclaration(声明)。内部 JSON 实现私有。将现有声明相关的类型 token、默认值、数值约束和精确整数语义迁到同一入口 | SDK 校验工具、宿主发现和自定义工具编辑器均能读写同一格式。公开头文件不出现 QJsonObject、QObject 或特定 JSON 库类型 |
| Contracts 跨工具校验 | 提供 validateAuthoring(当前声明, 声明目录)。目录只需按工具 ID 查声明，返回包含工具 ID、JSON 字段路径、源/目标类型的诊断 | 对照真实助手、训练器和业务端口，检查跨文件引用和类型。无需加载 DLL 或启动 GUI |
| VisionTools | 工具 JSON、隐藏助手和训练器实现、模型编解码和工具业务错误 | 发布声明并实现其端口。打包时用安装的 SDK 对整批声明校验。原生工具另按 ABI 要求注册 |
| VisionRT | ToolDefinition 组合 SDK 声明与宿主路径、分类、可用性。运行时绑定上游结果、当前图、当前选中记录和现有 UI 控件 | 决定此刻能否调用、画什么预览、写内存还是工程、何时刷新画布。Qt 对象和工程资产位置仅存在于宿主 |

### 4.1 可选字段就是能力开关

| JSON 路径 | 核心字段与固定规则 |
| --- | --- |
| authoring | 存在时 version 为 1。省略整块即普通工具，无需为已有普通 JSON 补 false、空列表或空 training |
| easy_touch | image 指定画布来源，call 明确助手及 init/inputs 的绑定，preview_output 为 ROI，valid_output 为 Bool，message_output 可选 String，writes 是点击后的完整写入列表 |
| training | image 指定处理图中“加入训练”的来源。records 声明记录数、字段和默认 ROI 字段。parameters 使用现有类型及控件约束。call 指定训练器。writes 和 artifacts 至少一项有效 |
| training.easy_touch | 结构与处理页 easy_touch 相同。图像来源可引用当前内存训练图，写入目标限于当前训练记录和训练参数 |
| training.artifacts | 每项声明产物 id、训练器输出端口、format、version 和业务节点目标 init String。模型用一维 uint8 Array 返回。允许仅输出普通参数的训练，此时省略 artifacts |
| 调用输入、可写目标 | 节点端口用 stage + 数字 id。当前记录、训练参数各有自己的数字 id。上下文只提供 cursor、phase、append、training_image、training_set。literal 带被调用端口要求的类型。field_id 用于把字段编号传给训练器 |

类型模型使用封闭枚举或 variant 区分来源和目标，`CallSpec` 与 `ValueAssignment` 供助手和训练复用。处理页目标只能是可编辑的 init 或字面量 inputs，不能覆盖连接。训练页点击只能写入内存。只有训练成功返回的写入列表能进入工程。每个作用域最多一套 EasyTouch，固定手势无需配置 Qt 事件名或脚本。没有训练作用域的工具若试图引用训练记录，SDK 直接指出字段路径。

**如何从声明判断能力。** 宿主选中工具时读取可选块，真正调度前再判断运行条件。下列伪代码中的声明来自 SDK 解析结果。

```cpp
const auto* training = declaration.trainingSpec();           // 源于 authoring.training
const auto* touch = declaration.easyTouchFor(currentView);   // 只查当前页的可选声明
view.showTrainingTabs(training != nullptr);
view.allowAddToTraining(training && inProcessingView && currentSourceFrame);
view.allowApply(training && inTrainingView && inputsMeetTrainingRules && idle);
viewport.acceptEasyTouch(touch && userEasyTouchEnabled && helperAvailable
                         && imageAvailable && targetsWritable && idle && gestureAllowsPick);
```

单次点选为 learn，Ctrl 点选同时令 append=true。cursor 是当前原图 (x, y) 的二元 float64 Array，phase 是 hover 或 learn，训练图使用恒等对齐。助手端口与返回角色完全由映射决定，端口 ID 的同一数字可以在 inputs 和 outputs 各使用一次。成功且 valid_output 为真才执行点击写入。助手缺失或环境不可用时，现有提示显示能力和原因，缩放及原有手动编辑仍可用。结构非法的声明在作者打包和宿主发现阶段产生相同诊断。

### 4.2 TrainingSet 与 ABI

TrainingSet 只作为普通工具调用的强类型值。图像表保存 imageId、独立的一帧原始 Mat 和颜色顺序。记录表保存 recordId、imageId 及以 fieldId 索引的字段。字段类型表从 JSON 的 records.fields 派生。多条记录可共享一张原图。字段允许现有标量、Array、ROI，不嵌套图像或训练集。Python 视图沿用精确 dtype、rank 和 owner 保活规则。模型作者收到真实像素和字段，不接收工程路径或编辑界面对象。

新增 `DataRef<TrainingSet>` 会改变 PortValue variant，模块 ABI 从 10 升至 11。这是二进制边界的变更。authoring.version=1 是 JSON 声明版本，工程格式 8 负责编码训练资产引用，三个版本各自独立。SDK 的安装导出应包含声明类型、读写/校验入口和对应测试夹具。两个父仓必须指向同一 Contracts gitlink。在 SDK 测试中覆盖整数精度、类型往返、保活和安装后独立消费者，再替换宿主中的旧定义与分支。宿主保留工程文件读写、动态节点端口物化、目录发现和 Qt 适配，消除重复的工具 JSON 权威解析器。

### 4.3 给 VisionTools 作者的三个 JSON 示例

以下均为拟议的 authoring 扩展片段，合入对应工具已有的 JSON 根对象即可。原有端口、实现入口与展示字段继续使用。ReadImage 直接沿用原文件。示例中的新端口必须和对应工具 DLL 同批发布。

**圆测量，仅处理页点选。** 原图来自 inputs.1。助手返回的三个值分别更新测量区域 inputs.2、径向半搜索宽度 inputs.5、边缘极性 inputs.9。source 和 target 上的 current_image 只在批量 ROI 或对齐矩阵需要选当前图时使用。共享组保持共享语义。

```json
{
  "authoring": {
    "version": 1,
    "easy_touch": {
      "image": {"kind": "port", "stage": "inputs", "id": 1, "projection": "current_image"},
      "call": {
        "tool_id": "PickCircle",
        "inputs": [
          {
            "port_id": 1,
            "source": {"kind": "port", "stage": "inputs", "id": 1, "projection": "current_image"}
          },
          {"port_id": 2, "source": {"kind": "context", "key": "cursor"}},
          {"port_id": 3, "source": {"kind": "context", "key": "phase"}},
          {
            "port_id": 4,
            "source": {"kind": "port", "stage": "inputs", "id": 2, "projection": "current_image"}
          },
          {
            "port_id": 5,
            "source": {"kind": "port", "stage": "inputs", "id": 3, "projection": "current_image"}
          },
          {"port_id": 7, "source": {"kind": "context", "key": "append"}}
        ]
      },
      "preview_output": 2,
      "valid_output": 4,
      "message_output": 5,
      "writes": [
        {
          "output": 3,
          "target": {"kind": "port", "stage": "inputs", "id": 2, "projection": "current_image"}
        },
        {"output": 7, "target": {"kind": "port", "stage": "inputs", "id": 5}},
        {"output": 6, "target": {"kind": "port", "stage": "inputs", "id": 9}}
      ]
    }
  }
}
```

PickCircle 的 inputs.7 Bool 是本设计新增的追加意图，inputs.4 改为接收当前完整 ROI 组。旧助手的 inputs.1–6、outputs.2–7 已按源码核对。助手返回替换或追加后的完整集合，宿主不自行拼圆。行间距采用同一模板，PickRowSpacing 的 outputs.3、6、7、8 分别写 MeasureRowSpacing 的 inputs.2、6、7、8，表示 ROI、物体极性、合格宽度下限与上限。两种测量均不产生训练文件。

**颜色识别，手绘训练。** 省略 easy_touch。标签和 ROI 控件由 records.fields 产生。点击已有“应用训练数据”把内存记录传给 TrainColorModel。输出 2 是模型字节，宿主负责成功后的持久化和 init.0 的路径派生。

```json
{
  "authoring": {
    "version": 1,
    "training": {
      "image": {"kind": "port", "stage": "inputs", "id": 1, "projection": "current_image"},
      "records": {
        "min": 1,
        "roi_field": 2,
        "fields": [
          {
            "id": 1,
            "name_zh": "颜色名称",
            "type": "string",
            "required": true,
            "constraints": {"non_blank": true}
          },
          {
            "id": 2,
            "name_zh": "取样区域",
            "type": "roi",
            "required": true,
            "constraints": {"min_regions": 1, "roi_kinds": ["rectangle", "circle", "polygon"]}
          }
        ]
      },
      "call": {
        "tool_id": "TrainColorModel",
        "inputs": [
          {"port_id": 1, "source": {"kind": "context", "key": "training_set"}},
          {"port_id": 2, "source": {"kind": "field_id", "id": 1}},
          {"port_id": 3, "source": {"kind": "field_id", "id": 2}}
        ]
      },
      "artifacts": [
        {
          "id": "color_model",
          "output": 2,
          "format": "visiontools.color_uv",
          "version": 1,
          "target": {"stage": "init", "id": 0}
        }
      ]
    }
  }
}
```

**直边对齐，点选与训练联动。** 下面将点选写入目标与最终训练输出分开。PickStraightEdge 的输出只更新当前搜索框和三项内存参数，最低完整度保留用户值。TrainStraightBorders 收到同一张原图和两条搜索区域，返回模型及可直接叠加显示的拟合线。

```json
{
  "authoring": {
    "version": 1,
    "training": {
      "image": {"kind": "port", "stage": "inputs", "id": 1, "projection": "current_image"},
      "records": {
        "min": 1,
        "max": 1,
        "roi_field": 1,
        "fields": [
          {
            "id": 1,
            "name_zh": "参考边搜索区域",
            "type": "roi",
            "required": true,
            "constraints": {"min_regions": 2, "max_regions": 2, "roi_kinds": ["rectangle"]}
          }
        ]
      },
      "parameters": [
        {
          "id": 1,
          "name_zh": "边缘对比度",
          "type": "float",
          "default": 30.0,
          "constraints": {"min": 0.0, "max": 1024.0}
        },
        {
          "id": 2,
          "name_zh": "边缘宽度",
          "type": "int",
          "default": 3,
          "constraints": {"min": 1, "max": 63}
        },
        {
          "id": 3,
          "name_zh": "跃迁方向",
          "type": "string",
          "default": "any",
          "constraints": {"options": ["any", "dark_to_light", "light_to_dark"]}
        },
        {
          "id": 4,
          "name_zh": "最低边完整度",
          "type": "float",
          "default": 0.4,
          "constraints": {"min": 0.0, "max": 1.0}
        }
      ],
      "easy_touch": {
        "image": {"kind": "context", "key": "training_image"},
        "call": {
          "tool_id": "PickStraightEdge",
          "inputs": [
            {"port_id": 1, "source": {"kind": "context", "key": "training_image"}},
            {"port_id": 2, "source": {"kind": "context", "key": "cursor"}},
            {"port_id": 3, "source": {"kind": "context", "key": "phase"}},
            {"port_id": 4, "source": {"kind": "record_field", "id": 1}},
            {
              "port_id": 5,
              "source": {"kind": "literal", "value": [[[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]]}
            },
            {"port_id": 7, "source": {"kind": "context", "key": "append"}}
          ]
        },
        "preview_output": 2,
        "valid_output": 4,
        "message_output": 5,
        "writes": [
          {"output": 3, "target": {"kind": "record_field", "id": 1}},
          {"output": 7, "target": {"kind": "training_parameter", "id": 1}},
          {"output": 8, "target": {"kind": "training_parameter", "id": 2}},
          {"output": 6, "target": {"kind": "training_parameter", "id": 3}}
        ]
      },
      "call": {
        "tool_id": "TrainStraightBorders",
        "inputs": [
          {"port_id": 1, "source": {"kind": "context", "key": "training_set"}},
          {"port_id": 2, "source": {"kind": "field_id", "id": 1}},
          {"port_id": 3, "source": {"kind": "training_parameter", "id": 1}},
          {"port_id": 4, "source": {"kind": "training_parameter", "id": 2}},
          {"port_id": 5, "source": {"kind": "training_parameter", "id": 3}},
          {"port_id": 6, "source": {"kind": "training_parameter", "id": 4}}
        ]
      },
      "artifacts": [
        {
          "id": "alignment",
          "output": 2,
          "format": "visiontools.straight_borders",
          "version": 1,
          "target": {"stage": "init", "id": 0}
        }
      ],
      "reference_roi_output": 3
    }
  }
}
```

上述直边助手拟新增 inputs.7 Bool、outputs.7 Float 和 outputs.8 Int。inputs.6 沿用助手默认搜索半径。搜索带短边另作为助手的数值输入扩展，由示例配置或手动调整 ROI，88 px 不设为整个平台的默认值。训练图中的单位矩阵明确写在绑定中。`reference_roi_output` 是可选 ROI 返回端口，其几何位于训练原图坐标。宿主无需打开私有模型解码拟合线。最低边数和必填约束只在“应用训练数据”前判断，不能据此拒绝第一条边进入内存。

## 5. VisionRT 如何把 JSON 接到现有界面

| 时机 | VisionRT 的处理 |
| --- | --- |
| 工具包装载 | 使用 SDK 读工具 JSON，再以整包的声明目录解析 call 和目标端口。存在有效 training 才启用已有训练页签和加入训练入口。辅助工具沿用隐藏可见性，不进普通工具树 |
| 选中节点或切页 | 处理页取 authoring.easy_touch，训练页取 authoring.training.easy_touch。QML 继续使用现有 ResultsModeTabs、ResultsTrainingRecords、ResultsImageToolbar 和 ImageViewport。无训练能力时清除上一节点的训练视图，沿用原来的缩略图栏、参数面板和可用画布宽度 |
| 判断点选可用性 | “当前页声明了点选、全局开关开启、助手可用、原始图像可读、目标可写、允许交互”同时成立才接管画布事件。无图或目标已接上游时用已有 tooltip 说明。声明成功与用户偏好分别判断，不增第二个开关 |
| 悬停与单击 | 复用 AssistPreview 的 40 ms 悬停合并、点击优先和过期结果丢弃。助手收到原图像素坐标、phase 与 append。hover 只画 preview_output。learn 且 valid_output=true 时校验并提交全部 writes。缩放平移、手绘和手柄拖动沿用现有事件优先级 |
| 回写目标 | 处理页经 ProjectEditor 一次写入普通参数，遇到上游绑定或任何非法值则整组拒绝。训练页只更新该节点的内存记录与训练参数，参数控件、画布、列表同时刷新，工程执行版本不变 |
| 加入训练 | 用 training.image 从上游原始发布值选当前帧，保留位深和颜色顺序。QImage 只显示。训练图列表从内存模型提供缩略图，替换当前“先存 PNG 再用 file URL 展示”的路径 |
| 应用训练数据 | 取 training.records 与 parameters 的当前内存内容。校验数量、字段和可写目标，调用训练器，检查输出及模型格式。成功后发布。失败留在训练页，用同一个按钮再次操作 |

删除宿主的 TrainingKind、按工具名称或“第一个 ROI 输入”选助手、固定助手端口号和训练页专用的两条边计数。两条边的要求来自本工具 records 约束。颜色标签来自该工具字段定义。保留普通工具和原生/Python 的注册及执行生命周期。固定手势由平台定义，助手端口、来源和写入目标由工具 JSON 定义。

**写盘的唯一时点。** “加入训练”不得调用 ensureTrainingAsset 创建路径或修改工程，“编辑训练图”不得调用 save、临时 PNG 或文件形式的快照。执行“应用训练数据”时连同训练器返回值都先保留在内存，用工具侧解码器检查模型字节。只有训练成功且内存检查通过才创建本次资产目录，写入训练来源及结果，最后一次更新节点引用。对仅输出参数的训练，同步发布正式参数及可重开的训练来源，不创建空模型文件。

| 成功后的资产与操作 | 规则 |
| --- | --- |
| 已保存的普通工程 | 格式 8 的节点只含一个可选相对引用 training_asset。成功产物放在 `<工程名>.assets/training/<node-id>/<revision>/`。manifest.json 保存正式记录、参数及来源/模型索引，images 存无损原图，models 存版本化模型。每个 revision 完整且不可变 |
| 未命名工程或安装示例 | 首次成功训练后才允许把完整结果放入宿主管理的工作区，供现有 init String 路径立即加载。首次保存或另存时将当前引用复制到目标工程。安装示例自身的资源不被改写 |
| 保存与重开 | 先准备完整的成功资产，再用现有 QSaveFile 写工程 JSON。保存过程中不读取内存编辑区。未完成时已有节点仍引用上次成功的资产，新节点省略该引用。重开加载所引用的模型，指出丢失或损坏文件，不在初始化重训 |
| 运行与缓存 | nodeParameters 派生出模型 init.0 的实际路径。InstanceConfiguration、缓存比较和 initialize 使用同一物化值。成功发布后节点执行版本仅增加一次，下一次运行读取新模型。编辑过程中可继续使用之前成功的模型 |
| 写盘失败 | 本次临时发布文件仅允许在算法成功之后创建，失败清理本次未引用的文件。旧节点、旧工程引用和旧模型完整。未完成训练输入永不进入临时文件、项目文件或恢复文件 |

图像存储沿用 OpenCV FileStorage 的 JSON + BASE64 方案，验收按像素、位深和通道顺序逐项核对。重复使用同一张训练图只保存一份像素。普通 ReadImage 路径继续使用既有产品目录规则。

## 6. 两个训练器的算法与业务验收

**直边。** 点击“应用训练数据”后，给定一张参考原图和两个有方向的旋转搜索矩形，训练器依次复用 `EdgeProfile` 与 `StraightEdgeLocator` 的法向采样、候选检测和拟合。矩形长轴只给出初始采样方向。分别保存搜索区域、图像中实测的两条参考线段，以及训练时采用的对比度、宽度、极性、最低完整度。两边必须能稳定确定姿态，可为非直角。平行、退化或缺边时报告具体区域。运行时用同样的通道和梯度口径找当前图的两条边，按参考线到当前线求刚体变换。

点击时由 PickStraightEdge 对替换或追加后的完整集合估计参数。推荐用有效法向响应的稳健低分位估计最低对比度，利用响应峰宽映射到当前 1–63 的整数宽度。宽度确定后按同一平滑尺度复测对比度。统一法向定义，同号输出相应极性，可靠响应异号输出 any。最低完整度由用户指定。对比度分位数、峰宽映射先用已知阶跃、模糊、噪声和极性反转样本校准，发布条件看找边和参考姿态。**88 px 只作为这批线缆右侧搜索带的起始配置**。放宽搜索带后，必须检查是否吸附到邻边。

**颜色。** TrainColorModel 按标签汇总同一或不同原图中的取样区域，沿用当前 BGR 到 YUV 的 U/V 统计、协方差正则和类别顺序。首版支持矩形、圆、多边形训练 ROI，统一用现有掩膜实现。运行检测保持现有圆形工位。用现有训练记录做等价性回归时，保留原按区域累计像素的计数口径。模型保存标签、均值、逆协方差、对数行列式、样本计数、预处理约定及格式版本。识色工具只加载模型并计算结果。首个业务案例使用当前支持的 8 位 BGR 原图。对不支持的类型给出工具诊断，不以显示转换改变原图。

**颜色结果怎么解释。** 现有 ratio 是被任一类别接受的像素数除以检测区域像素数，score 是胜出类已接受像素的平均得分。随包的红蓝各半实验在最低比例 0.99 时仍返回 blue，score 和 ratio 都为 1。因此本轮冻结这两个输出口径，单独验纯色、背景、混色、空掩膜和并列票。若后续要求判断混色，应另定义胜出类占比和拒识策略。若增加线序判定，可在独立业务工程中用现有 EvaluateExpression 逐工位比对并汇总，明确上游失败为未检、unknown 为未确认，再约定产线放行规则。

## 7. 三仓开发工单

| 工单 | 归属与依赖 | 交付条件 |
| --- | --- | --- |
| T01 工具声明进入 SDK | Contracts，先行 | 抽取公共 PortRef、端口声明、AuthoringSpec 和 UTF-8 JSON 读写。用普通工具、圆、颜色、直边四类夹具做往返测试。普通工具省略 authoring 后原有语义一致 |
| T02 跨工具引用校验与安装 | Contracts + 两父仓，依赖 T01 | SDK 对真实声明目录解析输入、输出、作用域和目标。作者打包与宿主发现调用相同校验，安装消费者可独立读取声明。移除宿主私有的声明定义和重复校验 |
| T03 训练值与跨语言接口 | Contracts + VisionRT，可与 T01 并行 | TrainingSet、ABI 11、Python 值桥、模块保活。同一 gitlink 构建宿主及工具包。类型 token、复制、输出精度与安装 ABI 均有实测 |
| T04 处理页 EasyTouch | VisionRT + VisionTools，依赖 T01/T02 | 接入既有全局开关、当前图投影与普通参数事务。迁移 PickCircle 和 PickRowSpacing 的完整集合及 append。处理页单击写入成功或整组不变，普通工具对比基线布局 |
| T05 内存训练界面 | VisionRT，依赖 T01/T03 | 原图加入、内存缩略图、字段控件、手势和内存参数读写。复用“加入训练”“应用训练数据”。移除加图即创建资产、写 PNG、写节点路径等旧调用 |
| T06 真正的训练与成功后发布 | VisionTools + VisionRT，算法可先行，联调依赖 T02/T03/T05 | 实现两种训练器、私有模型内存编解码、对齐拟合线输出。接入工程格式 8、产物原子发布、失败恢复及配置物化。首条边、连续拖框、失败训练和保存前后的文件写入均有证据 |
| T07 四工具整包迁移 | 两父仓，依赖 T04/T06 | 两个测量工具、两种训练工具和隐藏工具 JSON 与二进制同步替换。更新加载器、自定义工具复制和编辑往返，删除旧 interaction/training.kind 路径。修正被引用端口时不静默丢失映射 |
| T08 安装示例和发布验收 | 两父仓，依赖 T07 | 11 个内置工程一起升格式，线缆工程默认读 cable_11，目录模式遍历全组。补齐并核对其余 10 张原图，用实际训练器生成模型。最终包通过 A1–A8 |

线缆工程仍是 ReadImage → AlignStraightBorders → RecognizeColor 三节点。两识别工具 init.0 改为从训练资产派生。对齐保留图像 inputs.1 和 outputs.2–4，原执行 inputs.2–5 移至训练参数。颜色 inputs.1–5、outputs.2–8 保持现有含义。圆和行间距沿用既有产品参数及其端口编号。按原包同步升级策略，用户自建旧格式工程在新版给出版本提示，内置工程全部随包迁移。

## 8. 验收规格

| 编号 | 验证方法 | 通过条件 |
| --- | --- | --- |
| A1 JSON 控制范围 | 用同一测试工具身份分别装载无 authoring、只含处理点选、只含训练、训练内含点选四种合法声明，并验证不改宿主代码即可改变行为 | 页签、记录区、字段和事件路由符合第 1 节矩阵。ReadImage 的现有参数、缩略图和画布占位与基线一致。工具名、是否有 ROI、是否存在隐藏助手均不能单独启用训练 |
| A2 手势与字段联动 | 切换全局开关、处理/训练页和节点，测试无图、上游绑定、单图/批量、共享/逐图 ROI 及非单位对齐 | 仅当前声明的助手收到一次事件。悬停不写数据，点击只写声明列出的目标。处理页普通参数整组写入，训练页改动仅在内存。旧预览不串图、不串节点 |
| A3 训练前零写入 | 同时记录训练资产写入调用和工程目录、安装目录、工作区及其临时目录的文件变化。依次加入训练、点一条/两条边、拖框、改标签、切页、保存、另存，并分别制造训练失败和产品输出校验失败 | 本轮未完成输入的训练写入计数为零，包括临时文件的创建。首次训练场景下保存的工程不含训练引用。已有模型时另存只允许复制之前成功的资产，不能写出本次编辑的图片、几何或参数。关闭重开不恢复未完成内容 |
| A4 成功发布及重开 | 对直边、颜色分别点击现有“应用训练数据”，测试失败后再次点击、首次成功、已有模型重训、写盘失败、另存和重开 | 成功一次生成一份完整结果并切换节点一次。失败保持旧结果可运行。无旧模型时运行对应节点给出明确提示。普通工程与安装示例另存均能加载相应模型，重开不再训练 |
| A5 直边训练语义 | 在真实工作台对同一参考图分别按初始搜索框和法线 ±10 px 的框点击应用，验证错边、缺边、平行和非直角 | 框仍包住同一物理边时，拟合线仍落在该边。参考图相对单位变换角误差 ≤0.05°、平移模长 ≤0.75 px。旧基线平移约 1.274 px。88 px 只作为右侧搜索带的线缆验证起点 |
| A6 颜色与模型 | 对同一训练集和检测掩膜比较内存模型、保存后加载和工程重开的逐工位输出。覆盖同名标签、多形状、混色及 unknown，并只改检测阈值 | 标签与 float32 分数、比例逐值一致。成功前处理页用之前的正式模型，模型发布后使用新结果。只改检测阈值不调用训练器 |
| A7 真实线缆 11 图 | 核对输入 SHA-256，逐图归档两条检测边、五个圆、姿态矩阵及五工位分类数据 | 11/11 可执行对齐和识色，实际线边与工位经目视核对。03 前两根为红、紫，11 为紫、红、蓝、黄、紫。其余按独立目视复核，包括 unknown。工具成功状态与产品是否合格分别判断 |
| A8 SDK 和安装包 | 单独构建 SDK 消费者，以损坏的 JSON 引用、错误类型、冲突写入和递归隐藏工具作反例。用最终 DLL 加载内置工程，测试 Python 子视图释放及普通 UI | 作者端与宿主对同一错误定位一致。正常工具目录全通过。记录两仓和 SDK 提交及真实加载的 DLL 摘要，核对 11 个内置工程、两项既有训练入口和普通界面 |

**逐图复核的参照。** 下表直接来自包内两份 JSON，方便后续区分定位回归、真实换序与颜色拒识。88 px 列是旧 DLL 放宽搜索范围后的输出，不充当人工真值。

| 帧号 | 原 44 px 右侧搜索带 | 88 px 实验的五工位输出，按原顺序 |
| --- | --- | --- |
| 01 | 右边未找到 | 红、紫、蓝、黄、紫 |
| 02 | 右边未找到 | 紫、红、蓝、黄、紫 |
| 03 | 完成 | 红、紫、蓝、黄、紫 |
| 04 | 右边未找到 | 紫、红、蓝、黄、紫 |
| 05 | 完成 | 紫、红、蓝、黄、紫 |
| 06 | 右边未找到 | 红、紫、蓝、黄、unknown |
| 07 | 右边未找到 | 紫、红、蓝、黄、紫 |
| 08 | 右边未找到 | unknown、红、蓝、黄、紫 |
| 09 | 右边未找到 | unknown、紫、蓝、黄、红 |
| 10 | 右边未找到 | 紫、红、蓝、黄、紫 |
| 11 | 完成 | 紫、红、蓝、黄、紫 |

## 9. 可复查的依据

JSON 片段是新合同设计，验收条目是后续开发目标。源码链接固定到本次核对的提交。压缩包中的 DLL 结果是历史测量，基线分析复核了 JSON、示例图与探针摘要，未在 Windows 安装版复跑 11 张图，也没有 MERLIC 实机结果。

- **SDK 与值桥。** [公开值类型](https://github.com/QWERDF007/VisionRTContracts/blob/76bc8b6eb4477a16addd18a56eda48180d610b7d/include/visionrt/contracts/Value.h) ｜ [模块 ABI](https://github.com/QWERDF007/VisionRTContracts/blob/76bc8b6eb4477a16addd18a56eda48180d610b7d/include/visionrt/contracts/Module.h#L15) ｜ [工程值复制](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/core/ProjectValues.cpp#L5) ｜ [Python 值桥](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/python/PythonInterop.cpp)
- **宿主声明与编辑。** [声明解析](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/core/DefinitionJson.cpp#L377) ｜ [助手上下文](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/ui/AppControllerAssist.cpp#L195) ｜ [训练入口](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/ui/AppControllerTraining.cpp) ｜ [从矩形生成端点](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/ui/TrainingSession.cpp#L396) ｜ [参数事务](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/core/ProjectEditor.cpp#L823)
- **运行与保存。** [上游结果有效性](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/runtime/RuntimeEngine.cpp#L79) ｜ [实例配置及比较](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/runtime/NodeRuntime.cpp#L93) ｜ [工程保存](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/core/ProjectStorage.cpp#L979) ｜ [安装示例路径规则](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/core/ExampleProject.h)
- **真实算法。** [点选直边](https://github.com/QWERDF007/VisionTools/blob/97b8fc87dec230f0af2694cf764b040195c4bfa0/src/recognition/StraightEdgeProbe.cpp#L105) ｜ [直边拟合与姿态](https://github.com/QWERDF007/VisionTools/blob/97b8fc87dec230f0af2694cf764b040195c4bfa0/src/recognition/StraightEdgeLocator.cpp#L60) ｜ [颜色训练与统计](https://github.com/QWERDF007/VisionTools/blob/97b8fc87dec230f0af2694cf764b040195c4bfa0/src/recognition/ColorModel.cpp#L75) ｜ [识色初始化](https://github.com/QWERDF007/VisionTools/blob/97b8fc87dec230f0af2694cf764b040195c4bfa0/src/recognition/RecognizeColorTool.cpp#L56)
- **可扩展的现有测试。** [工具算法契约](https://github.com/QWERDF007/VisionTools/blob/97b8fc87dec230f0af2694cf764b040195c4bfa0/tests/test_recognition_contract.cpp) ｜ [工作台训练](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/tests/workbench_training_contract.cpp) ｜ [安装示例运行](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/tests/example_pipelines.cpp)
- **压缩包。** 目标范围见《VisionRT_EasyTouch与训练统一开发方案.md》，业务对照见《MERLIC_线缆颜色方案对照.md》。历史实验在 `evidence/merlic-cables-runtime/` 的 `manifest.json`、`local-baseline-results.json`、`local-wide-results.json` 和 `local-probe.cpp`。MERLIC 的内部拟合及分类公式未获实证，本方案使用 VisionTools 自有算法。
- **现有界面入口。** [处理和训练页签](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/ui/qml/ResultsModeTabs.qml) ｜ [加入训练](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/ui/qml/ResultsPanel.qml#L16) ｜ [应用训练数据](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/ui/qml/ResultsTrainingRecords.qml#L123) ｜ [现有全局点选开关](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/ui/qml/ResultsImageToolbar.qml#L64)
- **厂商操作说明。** 直边点选后调整对比度、边宽和跃迁方向，最低完整度由用户设定。颜色通过命名取样区域并应用来训练。[MERLIC 26.10 直边对齐](https://www.mvtec.com/doc/merlic/26.10/manual/en-us/Content/Tool_reference/Alignment/determine_alignment_with_straight_border.html) ｜ [MERLIC 26.10 颜色训练](https://www.mvtec.com/doc/merlic/26.10/manual/en-us/Content/Tool_reference/Processing/Checking/recognize_color.html)
- **原始 Mat 存储选项。** [OpenCV 4.5 FileStorage 模式](https://docs.opencv.org/4.5.0/da/d56/classcv_1_1FileStorage.html)
