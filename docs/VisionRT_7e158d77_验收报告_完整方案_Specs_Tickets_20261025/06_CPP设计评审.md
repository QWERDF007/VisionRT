# C++设计与简洁性

基础架构保持合理：IToolInstance接口多态、具体final/override工具、手写工厂模板、DataRef共享owner、BufferLease RAII和受约束容器模板符合职责。当前缺口是调用路径没有闭合，不是设计模式不足。

- **封装：**关闭应使用现有coordinator管理接口；删同步捷径后必须补上调用，不能只看坏函数已删除。ClosePhase收拢不变量，避免closing/pending/pendingReset组合含义模糊。
- **RAII：**DrainGuard恢复原值或禁止重入；阶段计时同时封装耗时和measured，成功/异常路径不分叉。
- **继承、多态：**只在可替换工具边界使用虚接口。关闭阶段不用类层次，Qt仍用现有queued通知。
- **模板：**继续用于工厂胶水和真实数组类型，不引入std::any或第二套Image类型。无需为这一轮关闭问题做模板化框架。
- **模型/UI边界：**新模型字段必须有视图消费者；完成标准既验证值，也验证真实tooltip呈现。
- **单一真相源：**Storage本次确实接入validateBinding，保留改进。Declaration/Result采用kind区分合理；兼容portContext可逐步收敛调用，避免长期存在含糊入口，但不列为本轮阻断。
- **并发：**所有权保证存活，Stop读回保证RT自身读取安全，工具协调业务写。不能借“shared_ptr安全”替代完整状态协议。

不引入新规则引擎、通用事件总线、每状态一个类、额外数据库或自动复制。建议这轮只实现4个针对性修复/测试工单并完成部署补验，随后结束本批整改，不无限扩大架构范围。
