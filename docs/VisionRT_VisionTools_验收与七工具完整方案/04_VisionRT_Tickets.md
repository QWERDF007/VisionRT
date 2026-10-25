# VisionRT工单

本轮专项工单把已修复项的复验、现有响应性优化和七工具实施分开。原工单完整细节保留在[七工具实施工单](七工具目标规范/08_实施工单.md)，本表状态替代旧版统一“待实施”说明。

## T31-RT-01 · 将GUI同步排空改为异步读者屏障

优先级：P2。依赖：无。对应原工单：T-U01, T-U04。

位置：src/ui/AppControllerReadback.cpp；src/ui/DisplayConverter.cpp；src/ui/AppController.cpp；读者协调组件（新增）。

实施：

- 管理入口先关闭新读者，再以完成通知推进续体。
- 保留ACK与generation，不在GUI wait/get。
- Close优先且不允许陈旧Start续体复活。
- 正常关闭先排空再析构，析构等待保留最后防线。

验收：

- 300ms门闩期间GUI心跳仍推进。
- 未释放读者时无新execute/ACK/completion越界。
- 释放后续体一次。
- 同步槽重入与Close覆盖待Start均正确。

## T31-RT-02 · 完成本轮关闭及绑定修复的Windows复验

优先级：P1。依赖：无。对应原工单：T-F01, T-F07。

位置：tests/workbench_lifecycle_contract.cpp；tests/workbench_contract.cpp；tests/project_editor_contract.cpp；安装态证据。

实施：

- 运行最新提交实际Qt/Core测试。
- 覆盖Ready同步槽及信号后调用，AwaitOwners保存与快捷键。
- Literal草稿候选与bindFreeInput一致。
- 以实际安装目录再跑跨语言生命周期。

验收：

- 记录命令/提交/退出码而非仅截图。
- 构造/初始化/执行计数无新增。
- closeReady一次。
- 现有有效上游占用仍拒绝。

## T31-RT-03 · 明确effectiveSource借用接口与草稿语义

优先级：P3。依赖：无。对应原工单：T-F07。

位置：src/core/ProjectEditor.h；src/core/ProjectEditor.cpp；相关调用与测试。

实施：

- 选借用指针并拒绝rvalue或按值optional一种接口。
- 执行依赖只用有效来源。
- 保留参数编辑草稿用途，避免盲目替换所有source判断。

验收：

- 临时InputBinding无法形成悬空引用用法。
- 现有连接测试行为不变。
- Literal草稿切回Upstream的编辑行为正常。

## T31-RT-04 · 实现目标格式、节点契约与表达式编辑

优先级：P1。依赖：无。对应原工单：T-F02, T-F03, T-F04, T-F07, T-L03。

位置：src/core/DefinitionJson.cpp；src/core/NodeContracts.cpp；src/core/ProjectStorage.cpp；src/core/ProjectEditor.cpp；表达式QML编辑器（新增）。

实施：

- schema3/format4与包同步升级。
- 稳定port_id及显式TypeSpec。
- 一个物化入口供UI、冻结计划、Python校验复用。
- 原子配置事务及alignment_input参考坐标编辑。

验收：

- 同工具多节点端口隔离。
- 改name不破坏连接，删已绑定端口拒绝。
- JSON保存重开与int64无损。
- 无效事务零修改。

## T31-RT-05 · 实现分支门控、合流与跨流程终态

优先级：P1。依赖：T31-RT-04, T31-VT-03。对应原工单：T-L04, T-L05, T-F06。

位置：src/runtime/RuntimeEngine.cpp；src/runtime/NodeRuntime.cpp；src/runtime/Receiver.cpp；ExecutionGate/SelectTool（新增）。

实施：

- 静态DAG合并数据和guard依赖。
- 正式工具前判定门控。
- Skipped/Blocked每轮发布。
- select条件必需性统一在Gate，Receiver及时响应终态。

验收：

- false支路initialize/execute计数0。
- 条件错误不走else。
- 真假交替无旧值。
- 合流保留Owner且选中Missing不退选。
- 同级/上行依赖拒绝。

## T31-RT-06 · 实现智能点选与完整UI生命周期

优先级：P1。依赖：T31-RT-01, T31-RT-04, T31-VT-04。对应原工单：T-U01, T-U02, T-U03, T-U04。

位置：PreviewCoordinator（新增）；src/ui/ImageViewport.cpp；src/ui/AppControllerRoi.cpp；src/ui/AppControllerReadback.cpp。

实施：

- 独立helper实例和一在途一pending。
- 上下文含publication/imageIndex/revision。
- 普通替换Ctrl追加，提案一次事务。
- Loop不启动helper，Reset/Close异步排空内部Owner。

验收：

- 悬停不dirty不推进取图。
- 非单位alignment回写坐标正确。
- 上游ROI不被断链。
- 陈旧结果丢弃。
- 外部Mat延寿，内部引用不拖住AwaitOwners。

## 原七工具工单状态

| ID | 工作项 | 当前状态 |
|---|---|---|
| T-F01 | 关闭状态入口单一判定与预览接入预留 | 基础关闭修复源码审查通过；Windows复验及新helper接入待完成 |
| T-F02 | 声明schema 3及辅助映射校验 | 待实施或待新增目标验收，不能以既有测试替代 |
| T-F03 | 动态节点契约物化单一入口 | 待实施或待新增目标验收，不能以既有测试替代 |
| T-F04 | 工程format 4与原子编辑事务 | 待实施或待新增目标验收，不能以既有测试替代 |
| T-F06 | 状态、显示与时间口径落实 | 既有时间/中文展示基础已具备；Skipped/Blocked扩展待实施 |
| T-F07 | 数据连接与控制连接统一校验 | Literal占用修复源码审查通过；guard/动态端口扩展待实施 |
| T-L03 | 动态表达式编辑器 | 待实施或待新增目标验收，不能以既有测试替代 |
| T-L04 | 门控调度和本轮终态 | 待实施或待新增目标验收，不能以既有测试替代 |
| T-L05 | 分支合流和Receiver终结传播 | 待实施或待新增目标验收，不能以既有测试替代 |
| T-U01 | PreviewCoordinator有界任务与读者租约 | 待实施或待新增目标验收，不能以既有测试替代 |
| T-U02 | ImageViewport智能点选交互 | 待实施或待新增目标验收，不能以既有测试替代 |
| T-U03 | 学习提案原子提交 | 待实施或待新增目标验收，不能以既有测试替代 |
| T-U04 | 预览纳入停止重置关闭与循环读回 | 待实施或待新增目标验收，不能以既有测试替代 |
| T-Q01 | 真实跨语言端到端契约 | 待实施或待新增目标验收，不能以既有测试替代 |
| T-Q03 | 性能和资源稳定性验收 | 待实施或待新增目标验收，不能以既有测试替代 |
| T-Q04 | 完整工具包和离线安装态 | 待实施或待新增目标验收，不能以既有测试替代 |
| T-Q05 | 最终复验与文档收敛 | 待实施或待新增目标验收，不能以既有测试替代 |

最终集成仍须完成T-Q01至T-Q05，不因专项测试通过就跳过安装态与最终回归。没有新增算法/界面实现的工单不能标完成。
