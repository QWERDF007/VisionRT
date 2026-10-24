# C++封装、继承、多态、模板和设计模式评审

## 判断

已有现代C++基础合理，问题主要是职责与规则没有完全收敛。充分使用C++特性应体现为不变量容易保持、生命周期清楚、重复路径减少，而不是继承/模板越多越好。改进按真实问题投入，不进行全仓模式化重写。

| 维度 | 当前证据 | 评价与改进 |
|---|---|---|
| 接口继承/动态多态 | IToolInstance虚析构及initialize/execute/release；ReadImageTool final/override | 适合动态工具插件，保留；不额外构造多层ToolBase体系 |
| 工厂 | Registry的create/destroy描述符，Tools createTool<T>手写注册 | 模板减少重复工厂胶水，运行时接口稳定；可为Tool加derived_from/constructible_from约束改善错误提示，无需生成器 |
| Adapter | PythonTool适配Python函数/类到IToolInstance | 边界正确；持Python细节留RT适配层，不进Contracts |
| RAII | DataRef、unique_ptr实例、Storage/UMatData、模块owner | 方向正确，已实测DataRef共享/延寿；buffer lease和退出预检尚未闭环，不是再加shared_ptr就解决 |
| 模板/静态多态 | NestedVector、Array受requires约束、variant+visit、if constexpr | 适合有限dtype/rank集合；避免另写每种类型分支。ArrayScalars顺序与DType枚举/索引映射应有集中traits/静态断言，避免未来扩展悄悄错位 |
| 值语义/共享语义 | 标量值、可写DataRef共享payload、显式owner | 符合用户需求；const句柄返回可写T&是刻意契约，不误改为只读；共享不保证线程安全 |
| PImpl/封装 | AppController_p.h共享220行内部字段，主cpp1347行 | PImpl减少头依赖，但各分文件仍可随意改状态；计时字段漏清理证明不变量缺封装。抽具体时钟/显示职责，而非单纯拆文件 |
| 核心规则 | y检查复制在setBinding、候选、moveNodes，deriveRelations遗漏 | 抽统一校验结果；不是“策略模式”层层虚调用。候选重复建图查环可在统一规则后简化 |
| 并发封装 | PythonLoadState私有mutex、snapshot/update/status/revision/set | 修复裸读写是实质进展；收敛只需的接口，generation与状态revision分离，别提供多个重叠入口造成一致性负担 |
| Observer/事件 | Qt signal与queued回调 | 适合UI通知，复用；Loop不应以UI观察者ACK驱动业务循环，不引入新事件总线 |
| 状态机 | RunMode/RuntimeState与readback | 当前缺模式策略导致Loop/Once同路；用enum+明确状态转换即可，不必每状态一个继承类 |
| UI类型展示 | displayTypeName在OutputModel.cpp但参数/绑定另用typeText | 展示职责移入小型共享模块；保留机器类型API，所有展示单源 |
| QML对象模型 | parent.parent.modelData获取业务目标 | 业务ID应显式属性传递，视觉父链不是封装接口；遵循Qt动态菜单的显式id调用 |

## 建议的最小内部接口（设计示意）

```cpp
struct BindingCheck { BindingError reason; explicit operator bool() const; };
BindingCheck validateBinding(const FlowDefinition&, SourcePort, InputPort);

class RunSessionClock {
public:
    void begin(SessionId, TimePoint);
    void finish(TimePoint);
    Duration elapsed(TimePoint now) const;
    void reset();
private:
    // start/finish与身份作为整体，不让GUI直接修改零散double
};
```

这里只示意约束，不强行要求新增SessionId包装所有整数，也不要求类名完全相同。优先复用已有结构。输入校验/映射适合纯函数；拥有状态与生命周期的组件适合具体类；真正可替换工具实现才使用虚接口。模板用于编译期载体/工厂生成，不用于把运行状态机过度泛型化。

## 简洁性与效率

- ProjectEditor moveNodes逐边find_if寻找源节点可用一次nodeId索引，减少重复扫描；统一候选查询不应对每个端口全图deriveRelations+containsCycle。先保证新规则一致，再用大图候选基准验证收益。
- PythonLoadState一个snapshot即可同时取status与revision；不要先后status()/revision()形成两个时间点。期望修订号当前并非普遍使用，不应把存在字段当成完成迟到更新保护。
- DataRef两层shared_ptr和retainedBy包装有元数据开销，但不要未经测量改成裸指针或COW；先统计分配再决定是否优化，保持析构依赖顺序。
- Array的7种标量×4维=28个variant候选是可控复杂度；避免另加std::any形成第二份类型系统。构建时间/二进制大小与dispatch实测后再考虑更重类型擦除。
- ToolFactory裸指针出入是现有模块create/destroy边界，不等于内部乱用所有权；模块内部用unique_ptr直到成功release、销毁通过提供者destroy。保持调用方使用RAII包装，不盲目把ABI函数改为跨编译器STL对象接口。
- 工具需要显式release返回状态，析构负责最终不抛异常收尾；不能为了RAII删除需要报告错误的生命周期接口。
- 当前依赖清单加入开发机绝对windows_default路径，应迁到本地CMakeUserPresets/环境覆盖，仓库给通用说明；这是可移植性优化，不是本轮P1。

## 不建议引入

不增加全局Service Locator、通用规则引擎、复杂AbstractFactory层次、每节点独立线程、自动复制/COW、全局Type Caster上下文单例、哈希/数据库。保留Factory/Adapter/RAII和Qt通知这些已有合适模式即可。

参考：C++ Core Guidelines C.120/C.129（有真实层级才继承、接口与实现继承区分）、R.1（RAII）、I.9（模板接口约束）；Qt Menu官方动态Instantiator示例使用明确menu id进行insertItem。来源见07。上述是按项目代码作出的设计判断，不是“越多模式越好”。
