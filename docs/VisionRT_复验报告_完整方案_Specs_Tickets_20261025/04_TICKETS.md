# 实施工单

优先级：T-B01/T-B02先行，T-B03接入同一收尾协议；T-B04～T-B06完成后进行T-B07发布验证。不要求重复实施已通过源码复核的A02/A04主体/A06/A07/A08。

## T-B01 · P1 · Once/Node 的 Stop 提前结束读回，完成状态可早于读者收尾

**范围：**[src/runtime/RuntimeEngine.cpp#L480-L497](https://github.com/QWERDF007/VisionRT/blob/7028c5723d1b2e72384730eccfea075aab0c2f84/src/runtime/RuntimeEngine.cpp#L480-L497)及对应调用方。

**实施：**Stop只停止新调度，不代表已有读者结束。Once/Node与Loop最终阶段共用读回完成协议：正常Stop仍等匹配token ACK；关闭想跳过显示时，宿主先排空/取消读者，再显式禁用读回。acknowledgeCompletion和start应拒绝未完成读回，清理已取消token。elapsed在真实必要收尾后固定。

**验收：**用barrier阻塞读者，Once/Node分别执行Stop、Reset、Close与快速重新运行；ACK前无completion/Idle、无release和新写者；ACK后推进。禁止靠sleep偶然通过，Loop最终读回也保持同样约束。

**完成证据：**固定SHA、相关目标/构建类型、实际命令和原始结果；静态审查不足以替代本项动态用例。

## T-B02 · P1（含条件风险） · 新clearResults在GUI线程持控制锁释放工具，且无运行态保护

**范围：**[src/runtime/RuntimeEngine.cpp#L806-L813](https://github.com/QWERDF007/VisionRT/blob/7028c5723d1b2e72384730eccfea075aab0c2f84/src/runtime/RuntimeEngine.cpp#L806-L813)及对应调用方。

**实施：**删除这条同步释放捷径，复用coordinator的管理命令收尾。锁内仅冻结状态/转移引用，锁外release与析构，再发布完成。Closing应在发出清空模型等可重入通知前成立，阻止新工作；内部Owner排空后准备Python关闭，外部Owner等待保持事件循环。

**验收：**慢release期间GUI心跳继续；release回调查询runtime状态不死锁；运行中管理调用拒绝或有序停机，不能并发销毁实例；release异常进入诊断且恰好释放一次。补真实UI外部Python Mat/ROI延迟归还测试。

**完成证据：**固定SHA、相关目标/构建类型、实际命令和原始结果；静态审查不足以替代本项动态用例。

## T-B03 · P2（状态边界风险） · drainReaders消费一个信箱时可启动下一项，尚不构成完整排空屏障

**范围：**[src/ui/AppControllerReadback.cpp#L21-L77](https://github.com/QWERDF007/VisionRT/blob/7028c5723d1b2e72384730eccfea075aab0c2f84/src/ui/AppControllerReadback.cpp#L21-L77)及对应调用方。

**实施：**明确排空的策略：冻结新的读取派发；已开始任务完成并消费，排队请求逐一完成或按明确规则取消；取消不能伪称已导出。完成消费者不得在关闭屏障内自动启动下一项。复用现有mailbox，不另建任务系统。

**验收：**多个不同端口导出排队，交错Stop/Refresh/Close；排空返回时无执行中的读者、无可再启动队列；已完成显式导出仍交付一次。异常和代次过期同样终结。

**完成证据：**固定SHA、相关目标/构建类型、实际命令和原始结果；静态审查不足以替代本项动态用例。

## T-B04 · P2 · T-A10加载入口仍非单源，手动模式旧引用诊断不一致

**范围：**[src/core/ProjectStorage.cpp#L437-L472](https://github.com/QWERDF007/VisionRT/blob/7028c5723d1b2e72384730eccfea075aab0c2f84/src/core/ProjectStorage.cpp#L437-L472)及对应调用方。

**实施：**Storage把有效边交给validateBinding，保存格式解析/定义迁移仍归Storage。闲置来源与有效依赖分开：Literal不因未使用的旧层级关系阻断当前值，切回Upstream才验证；引用缺失是否保留按一个明确规则处理。临时布局复用PositionLookup。

**验收：**候选、提交、保存重开、执行、移动对相同有效边结果一致；Literal闲置来源重开无虚假不可执行诊断；切回非法Upstream拒绝。

**完成证据：**固定SHA、相关目标/构建类型、实际命令和原始结果；静态审查不足以替代本项动态用例。

## T-B05 · P2 · 声明上下文仍携带publication，T-A05只完成类型和搜索部分

**范围：**[src/ui/AppControllerBindings.cpp#L9-L53](https://github.com/QWERDF007/VisionRT/blob/7028c5723d1b2e72384730eccfea075aab0c2f84/src/ui/AppControllerBindings.cpp#L9-L53)及对应调用方。

**实施：**用显式context kind或两种轻量上下文：Declaration含project/flow/node/port/结构身份；Result再含publication和必要交互身份。连接消费Declaration，查看/复制消费Result；名称菜单清理不能误伤待处理数据操作。

**验收：**未运行可连接；结果更新但定义不变时声明上下文仍有效；删除端口/切工程使其失效；数据上下文仍拒绝过期结果。实际点击搜索、子菜单绑定和已连接项也验证。

**完成证据：**固定SHA、相关目标/构建类型、实际命令和原始结果；静态审查不足以替代本项动态用例。

## T-B06 · P2 · T-A09阶段耗时未接入，只保留固定占位

**范围：**[src/ui/DurationText.cpp#L15-L22](https://github.com/QWERDF007/VisionRT/blob/7028c5723d1b2e72384730eccfea075aab0c2f84/src/ui/DurationText.cpp#L15-L22)及对应调用方。

**实施：**明确UI术语：宿主准备、初始化、工具调用、结果校验映射真实指标并提供measured/optional；算法内部预/后处理未上报则—。若保留现有四栏布局，要明示其含义，不能仅把字段重命名或填0。Python桥接子项含于adapter调用，不重复累计。

**验收：**冷启动/热运行/输入失败/输出失败/Python异常分别断言阶段有效性与显示值；未测≠0，纯算法时间不从adapter推断。

**完成证据：**固定SHA、相关目标/构建类型、实际命令和原始结果；静态审查不足以替代本项动态用例。

## T-B07 · P2（交付证据） · 生产可用结论超过当前提供的安装态证据

**范围：**[src/main.cpp#L25-L68](https://github.com/QWERDF007/VisionRT/blob/7028c5723d1b2e72384730eccfea075aab0c2f84/src/main.cpp#L25-L68)及对应调用方。

**实施：**保留76项结果；补固定SHA/配置与原始ctest日志、缺依赖警告清单。发布打包默认不放宽必需依赖；可选依赖须由实际包选择明确排除。复用installed_pipeline/installed_python_author等已有测试，指向实际E盘安装目录并隔离开发PATH。

**验收：**真实打开工作台、加载双根声明、执行C++和Python图像及ROI链路、单次/循环停止/关闭、保存重开；记录像素/ROI数值和关闭结果。TEMP/TMP及测试scratch保持D:\tmp，不能仅靠环境变量推断所有库都不写C盘。

**完成证据：**固定SHA、相关目标/构建类型、实际命令和原始结果；静态审查不足以替代本项动态用例。

## 仓库归属

本轮整改主要在VisionRT。VisionTools未出现新的变更，不应为完成RT的调度/UI工单重复调整工具公共接口。若需慢release/原地写测试工具，优先放既有测试夹具；真实安装链路继续消费Tools发布包。Contracts不新增访问策略、复制策略或另一套Image类型。
