# JSON、PortId 与全部参数类型

这是目标协议规范，基线 schema5 不接受这些新增字段；实施后使用 schema6。没有把设计示例假称当前版本可直接加载。

## 一、参数身份与显示顺序

`PortId` 为 uint32，取值 0～4294967295。身份由“工具/节点 + 参数方向 + id”共同确定；同一方向不能重复。参数数组位置只控制显示，排序不得改 id。Python inputs[1] 按字典键 1 读取，不是读数组第二项。C++ 按数字 ID 查询，同样不按 UI 行号。

新建工具示例：

```json
{
  "schema_version": 6,
  "name_en": "custom.image.threshold",
  "name_zh": "图像阈值处理",
  "version": "1.0.0",
  "implementation": {
    "kind": "python_function",
    "package": "custom/image/threshold",
    "file": "tool.py",
    "symbol": "execute"
  },
  "ui": {"category": ["自定义", "图像处理"]},
  "next_port_id": {"init": 0, "inputs": 4, "outputs": 3},
  "ports": {
    "inputs": [
      {"id": 1, "name_zh": "阈值", "type": "int", "default": 128,
       "constraints": {"min": 0, "max": 255}},
      {"id": 2, "name_zh": "输入图像", "type": "image"},
      {"id": 3, "name_zh": "检测区域", "type": "roi", "default": [],
       "relations": {"image": {"stage": "inputs", "id": 2}}}
    ],
    "outputs": [{"id": 2, "name_zh": "输出图像", "type": "image"}]
  }
}
```

这里 inputs[1] 永远是阈值，inputs[2] 是图像。把图像行移到阈值前，JSON 数组改为 id=2、1、3，工具与工程仍按原 id 访问。不添加 order、UUID 或名称到编号的运行期回退表。

## 二、固定编号与分配

| 方向 | 系统编号 | 业务起点 | 分配规则 |
|---|---|---:|---|
| init | 无 | 0 | 独立空间；函数工具不声明 init |
| inputs | 0：上层工具状态，仅 Fixed 注入 | 1 | 所有模型保留 0；逻辑工具不因升级多出上层状态 |
| outputs | 0：状态码；1：状态信息 | 2 | 所有工具通过普通 outputs 返回这两项 |

公共字段不在 JSON 中声明。固定行的中文名、类型、编号、位置不可在编辑器修改；其运行值由工具正常处理。“上层工具状态”默认 0，宿主按契约传入，非零不触发普通工具的框架跳过。

每方向一个 `next_port_id` 高水位，值是下一次分配编号，不是当前参数数量。必填三个方向值；空函数工具通常为 init=0、inputs=1、outputs=2。它必须大于该方向所有业务 id，且不小于业务起点。允许保留空洞，删除不回退计数；复制整工具保留计数。4294967296 是耗尽哨兵，只能出现在 next_port_id，不能成为 port id；用 uint64 存计数，分配时先检查再加一。无需同时保存 retiredIds/tombstone 列表。

为了避免把新参数接到旧工程引用，新加参数使用高水位，已经发布的 id 不重新分配给另一个含义。关闭未提交草稿可以整体撤销草稿编号分配；提交后不能回收。外部编辑声明也必须遵守此作者契约，宿主仅凭删除后的 JSON 无法证明某个历史编号从未被复用；不为此建立历史数据库或哈希链。

id 是整数，拒绝字符串 "1"、布尔、负数、超范围或非整数。Python 键用 `type(key) is int` 检查，不能把 True 当 1。SDK应在写入dict之前拒绝bool键；宿主对作者直接返回的裸dict只能检查现存键，无法恢复Python构造字典时已经合并覆盖的历史键。名字可改，编号不可通过普通属性编辑器改写。

## 三、名称与参数定义

PortDefinition 仍是宿主的一条参数元数据：新增 id，保留 nameZh/type/default/constraints/ui/relations。端口 name_en 可省略，填写时只用于作者阅读和迁移说明；不参与身份、绑定、取值或别名兜底，也不要求依赖它保持唯一。

工具顶层 name_en 仍是全局唯一工具 ID，与 native 工厂、module_index、Python 装配关联，继续必填，不能因为端口编号化就删掉。代码中的枚举/常量由工具作者手写，不启用构建时代码生成器。

## 四、关系与交互映射

所有端口关系统一结构为 `{"stage":"inputs|outputs|init","id":整数}`，使用现有 PortRef 概念，禁止一部分仍写 inputs.images 或另一部分写 inputs.2。包含 image、alignment、same_geometry_as、path_mode，以及 Host 的 type_from.flow/node/port。类型合法性规则继续使用现有验证器；只替换引用的身份部分。

智能点选（目标格式片段）：

```json
"interaction": {
  "helper": "visiontools.assist.circle",
  "inputs": [
    {"helper_port_id": 1, "source": {"stage": "inputs", "id": 2}}
  ],
  "proposals": [
    {"helper_port_id": 2, "target": {"stage": "inputs", "id": 3}}
  ],
  "roi_kinds": ["circle"]
}
```

以上 helper 名称和编号仅为结构示例，落地必须取实际 helper 声明映射；不是对当前端口号的事实断言。输入映射左侧查 helper.inputs，proposal 左侧查 helper.outputs；右侧查当前工具的 PortRef。不要把数字键塞进 JSON 对象后再靠字符串转数字恢复。helper 公共状态输入/输出同样按公共编号处理。

动态表达式的 AST 变量继续使用 symbol，例如 `mean(values)`；symbol 不等于端口 id。表达式配置保存 port_id=数字，运行时按变量配置把 inputs[id] 放进符号环境。修改显示名、显示顺序不改 symbol；显式改 symbol 才涉及公式校验。不将公式全文替换成数字，也不恢复 name_en 作为取参键。

## 五、完整具体类型


| token | 显示 | C++ | Python | 声明默认 |
|---|---|---|---|---|
| int | 整数 | std::int64_t | int | 0 |
| float | 浮点数 | double | float | 0.0 |
| bool | 布尔 | bool | bool | false |
| string | 字符串 | std::string | str | "" |
| image | 图像 | DataRef<Image>；Image 载荷 vector<cv::Mat> | list[numpy.ndarray] | 不存像素默认值 |
| roi | ROI | DataRef<ROI>；载荷 vector<vector<vector<double>>> | list[list[list[float]]] | [] |

数组元素 token 为 `uint8/int32/int/float32/float/bool/string`，在前面加 1～4 个 `vec_`。例如二维 float32 是 `vec_vec_float32`，四维字符串是 `vec_vec_vec_vec_string`。全量 34 项见 `类型矩阵.csv`；不是仅做界面选项，全部须经生产 TypeSpec/ValueJson 和真实工具边界验证。

ABI8 当前 Array 是带存储/形状/Owner 的运行时载体，作者辅助允许嵌套 vector；不能在新文档中把所有运行值写成裸 vector。Python Dense 数值数组可能是 ndarray，ragged 和字符串是嵌套 list。图像始终是一个 Image 类型（C++ vector<cv::Mat> / Python list[ndarray]），不另起“图像批次”类型。ROI 使用既有三层 double/float 结构和类型码，含义与 Contracts/ROI 一致。

**与早期约定的差异：**此前要求 Python 数组统一为嵌套 list，当前 ABI8 已采用 Dense 数值 ndarray / ragged 与字符串 list。本方案建议沿用当前 ABI8 并明确作者观察类型，不把它写成已字面满足早期纯 list 约定。确需纯 list 的算法可以在工具边界显式转换并承担分配成本；不要在所有宿主端口无条件复制。

Missing/null 表示没有值，不是新的可选类型。标量没有 uint8/int32/float32 独立端口类型，它们只在数组元素选择中出现。路径是 string + ui.editor，不另加 Path 类型；矩形是 ROI 编辑器，不另加 Rect 数据类型。


本轮 ABI9 只改变参数身份/接口键，不改变 ABI8 已有 Array 的载荷布局和作者观察类型。上述纯 list 与 Dense ndarray 的历史差异仍明确保留，不偷偷改变图像/ROI 的表达。

## 六、默认值与约束

输入和 init 默认值、标量 min/max、合法 options、ui.step/decimals/参数页、图像关系继续复用当前能力。输出不写 default/constraints/ui。图像不保存像素默认值；Missing 是值状态，不是新类型。Int64 和嵌套数组默认值通过 QString 原始 JSON 文本传给 ValueJson，禁止先转 QML double。PortId 自身 32 位可被 JavaScript Number 精确表示，这不能推出 Int64 载荷也可以如此处理。

只做契约合法性/范围规范，不增加业务状态拦截；错误消息仍由工具决定。fixed 是 UI 定义角色，不是数据权限；不增加 readonly/writable/COW 标记。

## 七、工程与输出文本的 JSON

工程 format7 的输入绑定、初始化值、类型签名按带 `port_id` 的条目数组序列化。数组顺序不作为身份。图像/Array/ROI 值编码沿用 ValueJson Stored，不把实际像素直接塞进端口定义。

```json
{
  "format_version": 7,
  "node_fragment": {
    "tool_id": "custom.image.threshold",
    "inputs": [
      {"port_id": 0, "mode": "literal", "value": 0},
      {"port_id": 1, "mode": "literal", "value": 128},
      {"port_id": 2, "mode": "upstream",
       "source": {"flow_id": "flow_1", "node_id": 10, "port_id": 2}}
    ]
  }
}
```

这是字段结构片段，node_fragment 是说明容器，不是完整工程文件。执行条件 source、跨流程 Receiver/Select、输出类型来源、节点复制重映射和所有结果上下文也全部使用数值 port_id。复制节点只重映射 node/flow 身份，不能给同一工具的端口重新编号。

Python 执行信封为 `{"outputs": {0: code, 1: message, 2: image}, "detail": ""}`，这是 Python 对象而不是 JSON 字符串。输出文本整体导出用 `{"outputs":[{"port_id":0,"value":0},…],"detail":""}`；单个端口的“复制”仍只导出该参数值，按现有 ValueJson 规则，不能借此恢复诊断菜单。
