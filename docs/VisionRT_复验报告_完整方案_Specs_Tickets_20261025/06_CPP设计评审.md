# C++设计与简洁性

当前Factory、IToolInstance接口多态、final/override、DataRef共享owner、BufferLease move-only RAII和受约束模板均适合项目，不需要替换成更多模式。

| 方面 | 判断与改进 |
|---|---|
| 封装 | clearResults名字掩盖release与销毁副作用；把工具管理留在一个coordinator入口，public API表达请求/状态限制 |
| 继承、多态 | 保留工具虚接口与具体final实现；算法优先组合，禁止为每种运行状态建立继承层次 |
| 模板 | 工厂和NestedVector/Array的模板减少重复；可用concept约束工厂类型，避免std::any再建类型系统 |
| RAII | BufferLease立即接管资源是正确修复；析构最终可能调用Python/工具，不可随意置于control锁内 |
| 状态机 | Once/Loop终点应复用一个读回过程，避免复制代码后只一处加stop谓词；Closing先于可重入通知 |
| 单一规则 | validateBinding加PositionLookup合理；Storage仍需接入，不新建通用规则框架 |
| Qt模型 | 声明身份和结果身份分别表达；菜单搜索复用现有组件，不改原工作台视觉 |
| 异步 | mailbox解决发布先后，但消费与继续派发职责应分清，drain有可证明后置条件 |
| 性能 | candidate对每端口线性找节点、再查环可能重复；正确性收敛后按大图实测建一次索引，不先加长期缓存 |
| 代码整洁 | 改动中旧readback测试输出仍称gate loop，需更新描述；删除未用局部type变量/iostream等，统一行尾避免整文件伪diff |

建议做小范围职责收敛：一个读回结束函数/过程、一个coordinator管理入口、一个连接规则、两种上下文语义。已有功能无需重写，禁止堆Service Locator、规则引擎、全局Caster上下文或额外事件总线。

“const句柄可写payload”是当前合同，不能以常规const习惯改成只读；shared_ptr保证寿命不保证共享写安全。SDK的STL/OpenCV ABI仍要求匹配工具链，不宣称跨任意编译器兼容。
