# VisionRT补充Specs

以下补充原七工具规范，不改变公开工具ABI。

## S31-RT-01 异步读者排空

1. GUI入口不得调用阻塞等待在途显示、全文格式化、helper的wait/get/条件变量；后台工作者内部可以等待，但不能持有完成回调需要的控制锁。
2. 管理意图进入Draining时，停止接纳新读者，清空可取消pending，保留必须消费的已就绪结果；结束操作有唯一序号和generation。
3. 读者完成后发送queued通知归还租约；计数归零后恰好一次推进管理续体。回调可同步触发新操作，仍不得重开外层draining。
4. 排空过程中Start不得提交新写者，completion不得越过现有readback ACK。不要通过丢弃Owner、提前ACK或复制所有业务数据来解决等待。
5. Close优先且不可逆；待处理Start不得在Close完成后复活。多个互斥管理操作按当前pending规则拒绝或收敛，不能覆盖正在收尾的操作。
6. 析构仍是最后防线：正常交互关闭先完成异步收尾再销毁QObject。不能简单删除所有析构join导致工作线程访问失效this。

验收：用门闩将读者保持在途至少300ms，同时GUI10ms定时心跳持续推进；期间无新工具执行、无completion。释放门闩后操作完成一次。300ms是故障注入时长，不是产品性能承诺；不要求实时系统式每10ms绝对准时。

## S31-RT-02 已修复关闭状态的目标环境复验

Resetting、AwaitOwners、Ready及closeReady同步槽里，直接调用和快捷键都不得启动、保存、刷新、同步或编辑。所有入口经统一谓词；关闭前的未保存决策仍可以正常保存。增加未来helper后，其内部Owner也必须在AwaitOwners前归还。

验收复用并实际执行workbench_lifecycle_contract、workbench_contract、ui_readback_contract、ui_completion_contract与runtime_readback_contract。记录真实新提交，不复制旧通过日志。

## S31-RT-03 有效来源与草稿来源

BindingMode::Literal的source不是图依赖。候选、bindFreeInput、compatibleInputs、ProjectStorage和执行使用有效来源规则；参数编辑器可以显示草稿，但应有明确字段命名，不能让该字段参与可执行性和占用判断。

动态变量与guard必须接入既有validateBinding，不另造位置/类型规则。显式测试Literal旧source、Upstream占用、缺失源、同级、移动后非法、publication更新时仍可提交声明级连线。

## S31-RT-04 七工具目标设施

原S-RT-01至06和S-ASSIST-01至05继续有效。现有NodeContracts的跨flow类型引用解析不等于任意动态端口物化，现有RoiPreview不等于helper任务系统。

先落地配置→契约→冻结计划这一条事实链，再接表达式编辑器和Python执行。helper输入图像及ROI/alignment选取必须在同一publication中完成；proposal逆变换回参考坐标。原子事务失败不得部分更新、重复同步或推进取图序列。

select的条件必需输入判断由同一个ExecutionGate处理；Skipped/Blocked是RT内部终态，不占用工具错误码。每轮都发布终态，Receiver在源跳过/失败后及时结束，不靠超时或旧轮数据填充。
