# C++设计与整体优化评审

## 当前可保留的设计

- IToolInstance与工厂将生命周期和算法隔离，RT无需知道OpenCV/InferRT内部实现；新工具沿用，不引入另一套插件入口。
- DataRef模板与Owner表达共享数据和依赖延寿，比std::any加隐式所有权规则更清楚；不要因增加表达式就退回无类型容器。
- effectiveSource将三处有效绑定规则收敛，统一isClosing封闭Ready入口，均属于减少分叉的合理修改。
- YOLO改动在候选进入NMS之前过滤无有效源图面积，修改范围小且有反例驱动，没有引入数据哈希、额外模型或重框架。
- BufferLease、计时守卫和现有工厂销毁路径继续使用RAII；借用的span/Mat视图必须与Owner生命周期一起设计。

## 值得改进的地方

| 主题 | 建议 | 不应做的扩张 |
|---|---|---|
| GUI排空 | 独立协调有界读者、管理续体、完成序号 | 用全局锁/忙等/processEvents掩盖阻塞 |
| AppController复杂度 | 将新增PreviewCoordinator和读者排空组件封装，只暴露少量生命周期API | 仅按行数拆成许多无复用helper，或继续把算法放进控制器 |
| 状态表达 | 入口统一isClosing；长期评估bool与ClosePhase重复状态 | 本轮为清理外观而重写全部已稳定状态机 |
| Binding接口 | 明确借用与draft/effective区分；临时值返回引用的用法编译期拒绝 | 把编辑草稿全部清空或误当依赖 |
| 测量内核 | EdgeProfile、CircleFit、GeometryTransform组合复用 | 深继承“所有视觉算法基类” |
| 模板 | 限定于已有数组rank/dtype、DataRef和像素分派 | 模板生成JSON、流程图或过重编译期DSL |
| 动态端口 | NodeContracts一个物化入口，稳定ID；配套契约测试 | 全局ToolDefinition随某节点编辑而变 |
| 工厂与声明一致性 | 测试真实工厂/entry的输入输出、包目录完整性 | 恢复构建生成器作为“省代码”措施 |
| 测试层 | 纯算法CPU、工具接口、RT真实运行、安装态四层 | 把所有测试都放到GPU模块或只mock关键下层 |
| 性能 | 先测调用边界、队列、转换、显示、Owner数，再优化 | 将各节点耗时相加伪装流程墙钟 |

## 具体的轻量实现建议

读者协调器维护活动读者数、是否接纳新请求、当前管理意图和单调操作号。可以用已有Qt queued回调驱动；不需要再引入第三方异步框架。显示、文本、点选提供“停止接纳/完成通知/释放”三个有限职责，Owner由任务自行持有到结束。

表达式配置是普通强类型值对象，派生variables/program是已有Array(String,2)初始化值；Python实例缓存AST。常规数学与列表函数以显式表登记，不对工具开放任意模块对象。

图像工具和测量工具保留独立小类，算法内核使用span、enum class及简单结构体。将几何实现放在单一源码目标，避免preprocess/metrology各复制一套ROI解析。对需要只借用的参数用const引用说明函数不改这个参数，这不等于给数据载体增加“只读权限”。

当前effectiveSource返回借用引用没有已确认误用。改进时可以返回const SourcePort*并对InputBinding&&重载=delete，保留零额外拷贝；或按值返回optional。两种选一即可，不需要为了它改变整个InputBinding布局。

## 发布与维护

只有产品代码变更时按仓库要求更新WORKLOG；本轮是外部审核交付，不改仓库账本或产品。下一轮报告必须将提交、真正执行的命令和未验证项对应起来。当前仓库归档上一验收报告是合理的历史留存，但不能据其中旧证据宣称新提交已经安装态通过。
