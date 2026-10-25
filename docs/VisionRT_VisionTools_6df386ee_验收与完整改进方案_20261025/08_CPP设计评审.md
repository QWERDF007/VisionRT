# C++ 设计与简洁性评审

结论：当前设计已合理使用现代 C++，没有必要为证明“用了设计模式”增加层级。最值得做的是让状态和规则有唯一来源，并优化有证据的热点。

| 方面 | 当前优点 | 可改进点 |
|---|---|---|
| 封装 | RT/UI、工具实现、Python bridge、Contracts 分层；Yolo State 封装资源 | AppController PImpl 仍集中很多相互关联字段；统一关闭判断优先于机械拆类 |
| 继承/多态 | IToolInstance/工厂让宿主不依赖工具算法；MatAllocator 是必要的外部扩展点 | 不需为每种图像算法建立多层基类；输入 helper 用函数即可 |
| 模板 | DataRef<T>、NestedVector、Array variant、ToolFactory、visit/if constexpr | 限制模板实例规模；rank≤4 是实际设计边界；约束为诊断服务，不做代码生成 DSL |
| RAII | unique_ptr 构建实例、模块 owner、BufferLease、ScopeTimer、CUDA State | 不把持有者降为裸指针来省 shared_ptr；析构调用 Python 必须保留 GIL/解释器时序 |
| 类型安全 | variant 明确值域，TypeSpec/dtype/rank 区分，Missing 独立 | 不改为到处 std::any；读取与写入必须依声明校验 |
| 组合 | 图像视图、Owner 和实例生命周期组合，没有强迫复制 | 进一步抽公共图像校验小函数可行，不能合并不同工具存储语义 |
| 状态模型 | ClosePhase、RunMode、Pending 显式 | close bool 与 phase 并存导致实际遗漏；计时兼容字段双存储增加维护负担 |
| 算法与性能 | 已有编译图缓存、mask 本轮复用、CUDA 固定缓冲复用 | 查询重复扫描、逐像素分支、每类 sigmoid 是具体可测热点 |

## 推荐保留的设计

工厂和注册解决动态创建；策略由具体工具对象多态承载；适配器隔离 Python/native；RAII 负责外部资源；PImpl 限制 UI 头文件依赖。此处已经足够，不需要为每个容器定义抽象接口。

DataRef::get() 即使从 const 句柄调用仍可获得可写载体，是本项目“句柄不代表只读数据”的明确约定，不应按普通值对象直觉改成隐式只读或写时复制。ResultSlot 的 const snapshot 固定的是发布描述，内部 DataRef 指向的载体仍可写；引用计数只能保活，不能替代共享写入同步。

std::vector<bool> 是标准特化，不能假设可直接取得连续 bool*；Python list 映射应沿现有类型化转换，不把所有 Array 当连续可零拷贝 buffer。

## 值得收敛的重复

关闭状态判断、有效绑定状态、重复图像格式检查是优先项。计时兼容别名可在后续做单存储。异常统一转状态应保持工具边界清楚，不为了减少 catch 行数隐藏哪个阶段失败或强行更改工具 message。

并发关键测试应观察行为和次序，不仅查源码字符串。已有 50 ms 轮询有价值但依赖调度机会；必要时用测试内部计数/屏障确认协调器到达目标阶段。不要添加纯为测试存在的生产公共接口。

## 不建议的重构

- 全局万能 type_caster/std::any 框架替换已经分离的 Image/Array/ROI 转换。
- 新增 readonly/readwrite/COW、自动 clone、按工具声明推断锁权限。
- 每个工具都继承通用的多层图像算子基类。
- 未测量就用对象池复用已发布的可写像素缓冲。
- 为审计再建哈希数据库、服务总线、任务调度框架。
- 删除所有循环检查、所有同步点、所有边界验证，以换取表面简洁。

代码简洁的衡量是调用链和规则更容易验证，不是删掉必要的所有权句柄，也不是类和模板越多越好。
