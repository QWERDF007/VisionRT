# 实施规格

| ID | 必须行为 | 代码边界 | 验收 |
|---|---|---|---|
| S01 | Stop不解除正在使用结果的读者屏障 | RuntimeEngine run/coordinate/start/completion | B01 barrier用例 |
| S02 | Loop仅最终读回，Once/Node/Loop终点同一规则 | RuntimeEngine/AppController | 循环无逐轮ACK，最终显示正确 |
| S03 | 工具release和owner析构不持control、不阻塞GUI管理等待 | RuntimeEngine manage/NodeRuntime | B02慢release/重入/并发 |
| S04 | Closing先阻断新工作，按阶段排内部owner，未就绪继续事件循环 | AppController/PythonRuntime | 外部Mat/ROI延后释放可自动完成 |
| S05 | drainReaders返回时没有可继续读取旧数据的任务 | Readback/mailbox | B03多请求排空 |
| S06 | effective binding规则单源，包括Storage | ProjectEditor/ProjectStorage | B04 Literal闲置来源 |
| S07 | 声明context不含publication，数据context必须校验结果身份 | Bindings/OutputPanel | B05结果更新不使定义失效 |
| S08 | 阶段名称与实际测量对应，未测≠0 | Snapshot/DurationText | B06冷/热/失败路径 |
| S09 | 安装态验证实际代码加载和执行 | package_app/安装消费测试 | B07真实CPU+Python链路 |

## 同步不变量

- 有有效readback token时，不发布允许重用/释放结果的完成状态。
- Stop取消写任务调度，不等同于取消读者。
- coordinator统一管理工具释放；control仅保护管理状态和引用转移。
- 用户回调和Python调用不可在control内进行。owner的最后析构也视为可能回调。
- drain后的旧mailbox回调可安全丢弃；已完成显式导出按确定规则交付一次。
- Closing期间新执行/刷新拒绝；批准关闭前可取消，破坏Python状态后不可悄然恢复Idle。

## 所有权不变量

DataRef提供可写payload和owner；不加readOnly/readWrite属性。业务写并发归工具，RT保证自身读取阶段与owner寿命。Mat外部data必须锚定真实分配者，非连续视图按合同验证，不隐式复制。BufferLease的失败/成功/移动路径均只有一个最终释放者。

## 验收数据

使用会原地更新像素与ROI/数组的受控工具、可阻塞的读者、可延时release的工具、Python生成图像和外部Mat ROI持有人。运行状态交错用promise/barrier，不依赖“等40ms应该够了”。负例与正常用例均调用真实下层实现，不用mock绕过本次被审查的路径。
