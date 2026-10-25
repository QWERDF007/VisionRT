# VisionRT 改进方案

## 1. 先做两项小修复

关闭：保留 ClosePhase，不新增状态机库；增加一个内部谓词供所有入口使用。Ready 仍关闭，不能把 closing=false 当成重新可用。可先统一判断，后续同一小提交移除多余 bool；避免一次修改全部 pending 协议。

连接：复用已有 effectiveSource 语义，把状态分类收敛为一个普通函数。candidateInputsWithState、compatibleInputs、bindFreeInput 共同使用；validateBinding 继续只负责结构/类型合法性。活动占用检查与结构检查职责明确，不必把所有逻辑塞成巨型 validate 函数。

## 2. 大图 UI：先消除重复查找，再考虑增量模型

`RuntimeEngine::pollLatest()` 在 control 锁下遍历全部运行节点，拷贝轻量 NodeUpdate 和 shared snapshot 指针；这不是像素复制。`AppController::poll()` 对每条 NodeUpdate 调用 `nodeExists()`，后者再次扫描工程节点，形成更新数 U × 项目节点数 N 的重复查找。每次有效状态变化又会失效 nodeRows，`nodes()` 重建当前流程 QVariantMap 列表及大量字符串。

实施顺序：

1. 记录 pollLatest 锁持有时间、poll UI 时间、nodes() 行构造时间。用 10/100/1000 节点测，而非从代码行数推算毫秒。
2. 在一次 poll 内构造 NodeId 集合，或复用按结构版本更新的 ID 索引，把存在性查询从 O(U×N) 降为 O(N+U)。第一版优先局部 ID 集合，避免跨 GUI 回调保存可能失效的 NodeDefinition 指针。
3. 仅当节点行重建实际占比高，改为现有模型上的状态/时间字段更新或局部 dataChanged。布局、名称、端口结构仍走结构刷新；不要因为每轮状态变化重建工具树和线网。
4. Runtime 增量 NodeUpdate 若确有必要，应把 generation/session 切换的全量同步、首次订阅、诊断、完成/读回 token 分开处理。不能仅按 publication 去重而丢失 active/lifecycle 状态。

保持 Loop 重资源门控和最后一次读回；不能把“降低 UI 刷新率”当成允许异步读写同一可写图像的同步协议。

## 3. 候选查询：索引一次，规则仍只有一份

当前 validateBinding 每次找源/目标都扫 nodes；候选枚举对每个端口重复调用，compatibleSources/compatibleInputs 还可能反复 deriveRelations/containsCycle。大图菜单存在明显复杂度放大点，但尚无本机性能数值。

使用一次查询期的节点和端口索引/结构视图，让 validateBinding 内部重载消费相同规则；公开旧入口可构造视图转发。临时布局继续通过 PositionLookup。索引不跨项目结构变化存活，不缓存可写值验证结果。

普通同一 flow 严格向下的边天然无环；可在证明所有活动边满足该约束的前提下取消“每个候选重建整图查环”。跨 flow receiver 依赖和加载异常图仍需完整校验。不要直接删掉全部 containsCycle。

## 4. 调度：保留现有执行图复用

RuntimeEngine::compile 已按结构版本等条件复用 CompiledFlow；Node 模式单独处理，不替换正常完整流程缓存。先确认热路径没有无意义的同步/初始化，再考虑复用本地 Submission/FlowTiming 容量。每轮 CompletionClock 仍会由异步 future 捕获，不能为了省一个 shared_ptr 把它变成可能提前失效的栈指针。

同一工具实例的 mutable scratch 不允许多任务同时使用；共享上游输入的原地写冲突归工具/流程约定解决，RT 不新增自动 COW 或数据读写属性。

## 5. 参数、输出和校验

继续用已有 Qt JSON 类型化编解码。输出大文本只在用户请求且读回允许时做；取消旧上下文不等于丢弃已就绪且按协议应交付的保存结果。drain 保留前一层状态的 RAII 正确，应继续保留。

重复校验优化必须具体：Image 的 validateValue 扫 Mat 头；ROI 扫坐标记录；字符串数组遍历 UTF-8；普通数值数组不逐元素全扫。保留模块边界检查和直接工具调用防御；不要设“上轮已验证”缓存，因为数据可写。确有瓶颈时合并同一调用内的重复结构检查，不改变校验责任。

## 6. 代码简洁性

- 统一 closing 谓词和有效绑定分类，可直接消除本次两个缺陷根源。
- ExecutionMetrics 的 executeMs/adapterExecuteMs、convertMs/validateMs 存在兼容双存储，后续可保留一个源字段、兼容 getter；不要在本轮同时改全部测试和 UI 名称。
- AppController 的拆文件已经改善导航，但 PImpl 中仍有大量相关状态。只按责任收拢读回信箱、关闭转换等小内聚单元；不建立新的服务容器、命令总线、虚基类层级。
- 注册表、插件句柄、PythonStorage 等已有生命周期边界不应为了文件变短而合并。

## 7. 成功标准

E01/E02 针对性场景通过；小工程行为不变。优化必须提供同机前后 p50/p95、节点规模、Loop 实际轮耗时、GUI 卡顿和分配/峰值内存证据。数据不足时保留当前清晰实现，不预设“至少快 10 倍”等无依据目标。
