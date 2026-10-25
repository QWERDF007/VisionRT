# VisionRT Specs

## S-RT-01 节点级契约

扩展现有 NodeContracts 为唯一 `materializeNodeContract(baseDefinition, nodeConfig)` 入口。全局注册表仍保存不可变工具声明和工厂；同一表达式工具的两个节点可以有完全不同的输入输出，不能修改全局 definition 来实现动态端口。

`ExpressionNodeConfig` 包含 variables、program、next_port_seq。variables 每项含稳定 port_id、可编辑 name、公式 symbol、TypeSpec、allow_missing；program 每项含稳定 port_id、name、symbol、expression、TypeSpec、exposed。新增端口使用节点内单调序号，删除后不复用；ID 不依赖中文名称或表达式文本。

物化结果统一进入候选连接、提交校验、ProjectStorage、InstanceConfiguration、Python 结果校验、冻结运行图、UI 类型显示和缓存失效。不得在 QML 或 Python 适配器里各自猜一次输出类型。输出类型在设计时明确，运行时只验证；没有“首次成功才确定类型”。

为保持 Contracts ABI，不给 ToolServices 增加宿主配置指针。RT 派生普通初始化参数：

- `variables`：Array(string,2)，每行 `[port_id, symbol, type, dtype, rank, allow_missing]`。
- `program`：Array(string,2)，每行 `[port_id, symbol, expression, type, dtype, rank, exposed]`。
- 非 Array 的 dtype/rank 单元格为空字符串；Bool 配置使用 `true`/`false`，rank 使用十进制字符串。

Python 类手动读取这些 Values，不读取 JSON。这些表是有固定列定义的内部初始化契约，不是 JSON blob，也不与工程配置重复保存。它们由 node_model 编辑器管理，不暴露成普通可手改的初始化表。

修改输入类型、删除已连接端口、隐藏已连接输出必须先解除相关绑定，否则整笔编辑拒绝。重命名 name 不影响 symbol；修改 symbol 后先解析全部公式进行 AST 名称引用替换并重新验证，不能用字符串全局替换改坏函数名或字符串常量。重排公式导致前向引用时拒绝提交。

## S-RT-02 编辑事务与连接规则

在 ProjectEditor 增加一次性校验并提交节点变更的事务入口。事务先在候选配置上物化契约、验证所有值/公式/连接，再提交一次 executionRevision、dirty 和必要的 structureRevision；失败不留下半个 ROI 或半个端口变更。不在本期额外引入整套撤销/重做系统；未来 Undo 也必须复用这个事务。

ROI 输入继续用 image_input 关联图像，新增 alignment_input 指向参考到当前图像矩阵。画布先以该矩阵呈现参考ROI，鼠标编辑点经逆矩阵回到参考坐标再提交，不能直接把当前图像坐标当参考值写入。对一般仿射下的圆/矩形使用显示变换，不把椭圆/平行四边形强行编码为另一种标准ROI；手柄编辑在原参考几何上进行。选择某张图时矩阵选择遵守统一广播规则。

数据边与 execution_guard 都要求同 flow、非自身、逻辑网格 `source.y < target.y`。相同 y 无论 x 如何都不能连接；屏幕缩放/拖动动画坐标不是依据。节点移动后若破坏已有边，整笔移动拒绝并显示原因，不能保留无效连接等运行才失败。跨流程沿用接收工具的显式语义。

只把 BindingMode::Upstream 的 source 当有效数据依赖。Literal 中保存的旧 source 仅是闲置信息，候选、提交、加载和执行不能各自采用不同规则。候选和提交必须调用同一 validateBinding。

输出名称右键直接显示可搜索的“下层节点 → 输入端口”菜单；不出现查看、复制数据、关联已有节点。菜单依据声明上下文（工程/流程/节点/端口/结构版本/交互号），新 publication 不使其过期。数据预览/导出依据结果上下文，带 publication，不能用旧图像执行新 ROI 操作。

表达式节点可以从该菜单创建一个新的变量输入，但必须在同一事务内确定 TypeSpec、唯一 symbol 和绑定；不先创建一个未定类型的临时端口。普通节点只列现有可连接输入。

## S-RT-03 执行门控与终态

`execution_guard` 是一个可选 SourcePort，源必须是 Bool。每节点首版只支持一个 guard；用户可通过表达式显式组合条件，不增加隐含 AND/OR 列表。branch 的两个 Bool 可用于 guard，也可作为普通数据输入，二者 UI 必须区分。

编译器合并数据依赖与 guard 依赖后查环，仍创建静态 Taskflow DAG。节点包装任务的顺序：检查取消 → 读取本轮 guard → 判定所需数据 → 必要时初始化 → execute → 校验 → 发布终态。false 分支的包装任务轻量运行，正式工具的 initialize/execute 都不执行；已有实例可保留到 reset，不因本轮未选中反复 release/create。

| 情况 | 节点结果 |
|---|---|
| guard 为 true | 继续输入检查和执行 |
| guard 为 false | Skipped；业务输出 Missing；不生成虚假算法 0 ms |
| guard 源本轮为 Skipped | Skipped |
| guard 源失败/取消/缺失/非 Bool | Blocked 或 Canceled；不得当 false 分支 |
| 必需数据源本轮跳过 | Skipped；本节点未运行 |
| 必需数据源失败或缺失 | Blocked |
| 可选数据源 Missing | 按端口定义传 Missing，可供 select/表达式显式处理 |

每个发布带 generation/session/round；禁止使用上一轮成功值填本轮 Missing。Skipped 节点同样发布终态，以唤醒 Receiver 和完成计数。基础设施失败仍按原运行时取消策略结束；分支逻辑不能掩盖 Python 异常或工具失败。

单节点运行遵守 guard。必须能取得所选冻结上下文中的稳定有效上游控制值，否则提示先运行上游，不能默认 true 绕过条件。嵌套分支通过 guard 嵌套；重试不得连回上层，首版通过明确有限次展开或下一 Loop 轮次完成。

## S-RT-04 合流与跨流程

增加 host select，输入 selector Bool、if_true/if_false 两个相同 TypeSpec 可选值，输出 value 同类型。TypeSpec 在节点配置 `select_type` 中显式确定，连接只做一致性校验，不按“最后连的输入”来回漂移。两条数据依赖都进入 DAG，两个包装任务都能终结，因此等待不死锁。

select 只校验选中分支的值是否存在；未选分支 Skipped 正常。选中分支失败或 Missing 则 Blocked，不退到另一侧；无法用 select 局部恢复基础设施已触发的全局取消。传递 Image/Array/ROI 保留原 DataRef/Owner，不复制像素。

这个条件必需性在同一个 ExecutionGate 内处理：node_model=select 时，selector确定本轮哪一个输入必须存在；所选值不可用直接发布Blocked，正式host工具不执行。未选值保持可选。这样不需要给公开ToolStatus增加“Blocked”魔法错误码，也不在工具和调度器里各定义一套分支规则。host select的execute只做已校验值的透传及防御性类型检查。

Receiver 从本轮发布得知源节点 Skipped/Failed/Blocked 后立即返回对应不可用诊断，不能等到 timeout，也不能返回上一轮值。跨流程等待环继续由现有接收计划校验拦截；本次 guard 不允许直接跨 flow 绕过接收工具。

## S-RT-05 渲染、计时与状态

Once/Node：每个已执行工具的图像、ROI、文本结果按当前读回协议转换为可由 UI 持有的展示结果；全部读者 ACK 后再发布 completion/Idle。Skipped 节点清除旧轮业务结果或明确显示未执行，不能留下看似本轮成功的旧图。

Loop：轮内只发布节点耗时、badge、状态颜色、flow/round 计时；不构造逐轮重资源 token，不生成图像缩略图、不重建 ROI overlay、不序列化大数组、不触发智能点选。保留上次展示图时标注“上次结果”，不能把它当作当前轮可读数据。Stop 停止后续调度，当前写者结束后做一次最终完整读回，ACK 后 completion。

冻结画面必须来自读回时已经生成的独立展示资源或已上传纹理，不能只是让QImage继续引用工具下一轮会原地修改的像素。窗口重绘、缩放和截图也不能绕过Loop重资源限制去读取正在被写的Mat。为显示生成展示资源是显式UI消费行为，不是Contracts的数据自动复制策略。

底部固定 `流程名：xxx ms | 全流程：xxx ms | 已运行：00:02:34:456`。

| 指标 | 计时边界 |
|---|---|
| 节点工具调用 | 实测 execute 边界；当前 adapterExecuteMs 含包装开销，不能宣称是纯 OpenCV kernel 时间 |
| 当前流程 | 该 flow 提交到其 Taskflow 完成的墙钟时长，含该 flow 排队/调度 |
| 全流程 | 本轮首个 flow 提交前到全部选中 flow 完成后的墙钟时长，绝非各 flow 相加 |
| 已运行 | 会话接受开始到最终 completion 的连续墙钟时长；Loop 持续累加，Stop 收尾/最终读回也计入 |
| 读回与渲染 | 不纳入 flow/round 算法调度时长；可在诊断中分开测量，不覆盖上述字段 |

当前 RuntimeEngine 已在 readback 前记录 roundEnd，因此“0.02 ms vs 38 ms”不能仅凭数字认定都是 UI 渲染。应以时间戳核对首次创建/初始化、调度、转换、排队及 UI 读回是否分别计入。新增功能禁止通过扣固定常数或把 flow 时间替换成节点时间和来“修正”。

未进入阶段为“—”，真实测得 0 合法；跳过显示“未执行”。预处理/后处理没有独立计时时保持“—”。本期不为填满卡片虚构阶段值。刷新轻量状态建议 10–20 Hz 合并更新，执行频率与 UI 刷新频率解耦。

## S-RT-06 关闭与排空

复用 None → Resetting → AwaitOwners → Ready 单向关闭状态机。所有保存、新建、打开、开始运行、同步、参数提交和点选入口统一使用 `closing || closePhase != None` 防御，Ready 不重新开放入口。未保存确认在 None 阶段完成，保存成功后才进入不可逆关闭。

排空顺序：失效交互上下文/停止新增读者 → 结束已在途点选、文本、保存读者 → 按已有策略消费或取消信箱 → runtime 异步 reset/release → 释放展示与辅助实例内部 Owner → prepareShutdown → 等外部 Owner → closeReady 恰好一次。

不得在 GUI 持 control 锁同步等 release；不得持需要被完成回调获取的锁等待 worker；DrainGuard 保留并恢复外层 draining，防止同步槽重入重新派发。Start/Reset/Close 必须等待预览租约归还才能重用同一可写数据，超时可以显示“正在结束预览”，不能强制释放仍被读取的内存。
