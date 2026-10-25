# C++与简洁性评审

结论：已有设计基础合理，问题集中在运行/显示/关闭不变量没有完全封装，以及同一规则跨入口重复。无需为了模式数量而重构公共合同。

| 方面 | 当前判断 | 改进 |
|---|---|---|
| 继承、多态 | IToolInstance配合具体final/override工具适合运行时插件选择 | 保留薄虚接口，算法组合优先；不要扩展深继承 |
| 工厂 | 手写注册加ToolFactory模板可减少create/destroy重复 | 模板仅复用工厂胶水，JSON不生成工具实现；销毁仍回到提供模块 |
| RAII | DataRef/模块owner、UMatData、BufferLease方向正确 | A07尽早接管资源；A03明确销毁顺序，析构无异常不等于关闭顺序正确 |
| 模板 | NestedVector、受约束Array、variant/visit适合有限dtype/rank | 类型枚举与variant索引映射集中traits/静态断言；不叠加std::any |
| 封装 | AppController使用PImpl和分文件降低头依赖 | 分文件仍共享大量可变字段；将运行会话、读回和关闭的不变量收进具体组件，不是再拆几个cpp |
| 状态机 | enum运行状态已有基础 | 明确Once/Loop/最终读回和Closing转换，禁止用多个布尔值表达相互矛盾状态 |
| 单一规则 | validateBinding和DisplayTypes是本次实质进展 | A10统一所有调用方；A05消除普通界面typeText分支 |
| 通知 | Qt queued与回调适合UI | 结果先发布，通知后发送；不能依赖轮询或Observer ACK推进正常Loop |
| 同步 | 关键控制量已有mutex保护意图 | A02所有读写都遵守；不以局部atomic替代整个状态协议 |
| ABI | 现有模块边界可按同工具链部署 | C++/STL/OpenCV ABI需要匹配，不能宣称任意编译器二进制兼容 |

建议最小封装：BindingCheck纯值与校验函数；会话时间戳/有效测量快照；拥有token与状态的读回控制；能报告剩余引用的Python收尾。无须泛化为规则引擎、事件总线、每状态一个子类、每节点线程或全局Service Locator。

共享数据的可写语义保留：const句柄返回可写payload是已有合同，不能私自改为只读。共享所有权不提供并发写安全；工具负责共享写，RT负责自身不在不安全阶段读业务数据。显示缓存为UI用途准备独立存储，不改变工具输入的数据策略。

性能优化先测量：候选查询可以单次建立nodeId索引，避免每个端口重建关系图；轻量更新有界合并避免事件堆积。不要为减少shared_ptr开销未经验证换裸指针。先完成正确性，再根据分配计数、吞吐、p95和大图规模决定优化。
