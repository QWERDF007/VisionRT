# 实施工单

本轮5项：1项P1、3项P2和1项补验。产品代码主要修改VisionRT，不要求Tools/Contracts重做已确认合同。

## T-C01 · P1 · 关闭没有提交异步重置，运行时内部Python owner可能使关闭永久等待

**范围：**[src/ui/AppController.cpp#L650-L766](https://github.com/QWERDF007/VisionRT/blob/7e158d77375a7141aab5e2231956161870576161/src/ui/AppController.cpp#L650-L766)及相关入口。

**实施：**关闭获准后先进入Closing，再提交现有requestReset管理命令；继续消费runtime完成事件，直到coordinator在锁外reset实例与slot并通知完成，才清UI残余owner和prepareShutdown。给关闭增加一个小型阶段枚举（Resetting/AwaitOwners/Ready），poll只在AwaitOwners走owner等待分支，不能所有closing一律return。不要只把原if条件去掉或提前设closing而留下早退，亦不要恢复同步clearResults。

**完成标准：**已运行Python图像工具直接关闭，无外部引用应完成；另外保存Mat/ROI外部owner，稍后GUI事件释放后能自动关；慢release期间GUI心跳持续；重复关闭只发一次closeReady；测试不得先resetRuntime清结果再关窗。

**证据：**固定SHA、实际命令和日志；UI问题必须有真实UI路径，不能只断言模型新字段存在。

## T-C02 · P2 · 阶段时间只到模型未到画布，异常耗时标志也遗漏

**范围：**[src/ui/qml/FlowCanvas.qml#L612-L617](https://github.com/QWERDF007/VisionRT/blob/7e158d77375a7141aab5e2231956161870576161/src/ui/qml/FlowCanvas.qml#L612-L617)及相关入口。

**实施：**沿现有tooltip样式增加宿主准备、初始化、结果校验行，并消费相应字段；算法内部预/后处理没有上报继续—，不冒用宿主指标。把是否测量与ScopeTimer进入/结束绑定，正常或异常退出均完成测量；未进入保持false，真实0合法。

**完成标准：**真实QML悬浮核对三字段；准备失败、初始化失败、输出校验失败、热运行、真实0及未进入阶段分别断言。结果失败不代表该阶段未测，Python桥接子项不得重复加到Adapter耗时。

**证据：**固定SHA、实际命令和日志；UI问题必须有真实UI路径，不能只断言模型新字段存在。

## T-C03 · P2（条件性行为缺口） · DrainGuard只约束部分路径，排队请求被统一静默清除

**范围：**[src/ui/AppControllerReadback.cpp#L21-L128](https://github.com/QWERDF007/VisionRT/blob/7e158d77375a7141aab5e2231956161870576161/src/ui/AppControllerReadback.cpp#L21-L128)及相关入口。

**实施：**统一在调度入口拦截draining，结果消费只完成交付不再驱动新派发；异常/成功路径相同。按管理目的处理队列：已有导出或保存明确完成、取消或保留，不能把清队列当成功。保存与文本请求至少按现有variant区分，无需新框架。guard保存并恢复旧值，或明确拒绝重入。

**完成标准：**排空+异常+多个请求，以及outputTextReady同步触发另一次管理操作：无新读者/意外保存/提前ACK；每项请求有确定结果，已完成导出交付一次，嵌套结束仍保留外层draining。

**证据：**固定SHA、实际命令和日志；UI问题必须有真实UI路径，不能只断言模型新字段存在。

## T-C04 · P2（测试覆盖） · 新增Stop负例只立即poll，关闭/阶段的关键回归未补齐

**范围：**[tests/runtime_readback_contract.cpp#L130-L158](https://github.com/QWERDF007/VisionRT/blob/7e158d77375a7141aab5e2231956161870576161/tests/runtime_readback_contract.cpp#L130-L158)及相关入口。

**实施：**增加可控执行交错或协调器观察点，测试未ACK阶段确实经历Stop处理且不能发布完成；在旧实现/反向变异下确认新用例会失败。关窗用真实Python输出和未reset节点；计时测真实QML。只补能区分错误/正确实现的针对性用例，不扩大无关全量。

**完成标准：**Once/Node读者阻塞时Stop/Reset/Close均不越过；释放ACK才推进。原地写工具在旧结果读者未结束前不能再次写；C01～C03分别有失败路径回归。

**证据：**固定SHA、实际命令和日志；UI问题必须有真实UI路径，不能只断言模型新字段存在。

## T-C05 · 补验项 · 工具包链路已补证，完整应用与其Python运行目录仍需单独确认

**范围：**[tests/installed_pipeline_contract.cpp#L80-L92](https://github.com/QWERDF007/VisionRT/blob/7e158d77375a7141aab5e2231956161870576161/tests/installed_pipeline_contract.cpp#L80-L92)及相关入口。

**实施：**保留当前已通过记录；补实际应用启动、执行、最终显示和直接关闭。测试环境显式指向实际部署解释器目录或输出sys.prefix/stdlib/NumPy路径及已加载Python DLL来源，隔离开发PATH。根据实际包布局确认路径，不假定一律是E:/Softwares/VisionRT/python。

**完成标准：**记录固定SHA、76项原始日志、解释器与DLL路径；实际E盘应用完成单次、Loop停止、ROI、保存重开和C01直接关闭。TEMP/TMP及测试目录继续D盘。

**证据：**固定SHA、实际命令和日志；UI问题必须有真实UI路径，不能只断言模型新字段存在。
