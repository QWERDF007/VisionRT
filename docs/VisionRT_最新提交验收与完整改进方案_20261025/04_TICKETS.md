# 实施工单

只描述尚待整改项，不要求重做已达成的持续时钟、加载执行校验及菜单显式目标传递。

## T-A01 · P1 · 循环仍逐轮等待 UI，并开放重资源读回

- 范围：[src/runtime/RuntimeEngine.cpp#L469-L497](https://github.com/QWERDF007/VisionRT/blob/adb01bdc6f22bcd1126ded3ff72a6da8d98d98f0/src/runtime/RuntimeEngine.cpp#L469-L497)及对应调用入口/测试。
- 实现：Loop只发布有界合并的轻量状态，不创建逐轮重读回token；Once/Node保留读回事件，Stop待真实写入完成再最终读回。显示缓存必须独立于仍可能被原地写入的业务像素，不能只保留同一Mat浅头后宣称画面冻结。
- 完成标准：循环至少100轮：图像转换/ROI几何/全文格式化的新任务计数均0；可预热缓存后统计业务纹理提交。让GUI暂不消费事件，执行轮数仍增长；停止后最终像素/ROI正确。修改runtime_readback_contract.cpp:95–115旧的每轮ACK断言。
- 交付：实现与必要回归测试、原始命令/日志、固定SHA；不能仅修改报告状态。

## T-A02 · P1 · 读回通知后的共享状态在锁外读取，存在数据竞争

- 范围：[src/runtime/RuntimeEngine.cpp#L475-L491](https://github.com/QWERDF007/VisionRT/blob/adb01bdc6f22bcd1126ded3ff72a6da8d98d98f0/src/runtime/RuntimeEngine.cpp#L475-L491)及对应调用入口/测试。
- 实现：锁内发布token及复制回调，锁外调用回调，随后无条件重新加锁，再根据共享谓词决定等待。禁用读回/停止/关闭必须清理或满足谓词并通知。不要只把两个bool改atomic却留下token状态不一致。
- 完成标准：在发布、通知、ACK、禁用和析构边界设置barrier，多次交错；TSan无竞争，过期token拒绝，不丢唤醒，不在control锁内执行用户回调。
- 交付：实现与必要回归测试、原始命令/日志、固定SHA；不能仅修改报告状态。

## T-A03 · P1 · 关闭先破坏 Python 运行时，再因宿主自身引用未释放而静默退出关闭

- 范围：[src/ui/AppController.cpp#L631-L698](https://github.com/QWERDF007/VisionRT/blob/adb01bdc6f22bcd1126ded3ff72a6da8d98d98f0/src/ui/AppController.cpp#L631-L698)及对应调用入口/测试。
- 实现：进入显式Closing并禁止新启动/刷新；停止调度、收尾读者和真实写任务，释放运行节点结果、快照、输出及显示/文本捕获等内部owner；再准备Python收尾并检查剩余外部owner。未就绪保持Closing和事件循环，显示原因，owner释放后重试；不要回到可运行Idle，也不要在GUI析构中无限等待。就绪后只在解释器所属线程finalize。
- 完成标准：Python生成图像并在面板显示后关闭；另持外部Mat/ROI，关闭请求返回后在GUI事件释放它；持续有进展且最终能关，不能挂死/静默失效。覆盖文本进行中、循环停止、失败工具、重复关闭。
- 交付：实现与必要回归测试、原始命令/日志、固定SHA；不能仅修改报告状态。

## T-A04 · P2 · 文本完成通知早于future就绪，异常分支没有通知

- 范围：[src/ui/AppControllerReadback.cpp#L122-L132](https://github.com/QWERDF007/VisionRT/blob/adb01bdc6f22bcd1126ded3ff72a6da8d98d98f0/src/ui/AppControllerReadback.cpp#L122-L132)及对应调用入口/测试。
- 实现：在受控结果邮箱中先发布成功值或exception_ptr，再发queued完成事件；GUI直接消费已发布结果。或使用项目可用的QFutureWatcher finished机制。所有出口统一完成通知，携带代次并丢弃过期结果。不要用sleep/重复排队掩盖时序。
- 完成标准：禁用轮询，控制工作线程在“已发通知、尚未返回”点停住，仍不得误认为完成；修复后一次通知即可消费。文本异常、切工程和关闭均不会悬挂。附C++时序模型探针，不冒充Qt集成测试。
- 交付：实现与必要回归测试、原始命令/日志、固定SHA；不能仅修改报告状态。

## T-A05 · P2 · 中文类型入口尚未统一，名称菜单仍缺搜索

- 范围：[src/ui/AppControllerBindings.cpp#L74-L85](https://github.com/QWERDF007/VisionRT/blob/adb01bdc6f22bcd1126ded3ff72a6da8d98d98f0/src/ui/AppControllerBindings.cpp#L74-L85)及对应调用入口/测试。
- 实现：所有用户可见类型统一displayTypeName；机器typeText仅用于机器格式/显式技术详情。名称菜单加工具名/输入名搜索，保留直接级联。声明上下文只依赖工程/节点/端口定义，不绑定结果publication；数据操作继续使用结果上下文。
- 完成标准：输入、输出、源选择、关系菜单、tooltip均显示中文；一维字符串数组/整数/图像一致。未运行也能连接；大候选列表可搜索；名称区无查看/复制/关联已有中间项。
- 交付：实现与必要回归测试、原始命令/日志、固定SHA；不能仅修改报告状态。

## T-A06 · P2 · 新会话未完成一轮时，0仍可能被当作有效全流程耗时

- 范围：[src/ui/AppController.cpp#L487-L499](https://github.com/QWERDF007/VisionRT/blob/adb01bdc6f22bcd1126ded3ff72a6da8d98d98f0/src/ui/AppController.cpp#L487-L499)及对应调用入口/测试。
- 实现：round计时携带measured或optional以及session/round身份；仅收到完整轮结果更新。首轮未完成显示— ms（或明确保留上次完成值，不能混为当前测量），真实0必须合法。保持现有持续时钟。
- 完成标准：首次长算子执行期间全流程未测，不显示伪造0；注入真实0完成值应显示0.00 ms；切工程清空，小时超过24正常，停止后elapsed固定。
- 交付：实现与必要回归测试、原始命令/日志、固定SHA；不能仅修改报告状态。

## T-A07 · P2 · 新增buffer租约正常路径正确，但分配失败前缺RAII接管

- 范围：[src/python/PythonImages.cpp#L165-L180](https://github.com/QWERDF007/VisionRT/blob/adb01bdc6f22bcd1126ded3ff72a6da8d98d98f0/src/python/PythonImages.cpp#L165-L180)及对应调用入口/测试。
- 实现：先用栈上move-only guard接管成功GetBuffer，再移动进共享owner；最好由guard封装获取和释放。任何后续分配/验证失败都恰好Release一次，释放发生在解释器存活且持GIL的阶段。
- 完成标准：故障注入逐个分配失败点，导出计数归零、无重复释放；正常Mat浅拷贝/ROI仍保活至最后一个使用者。需要原生桥接环境执行。
- 交付：实现与必要回归测试、原始命令/日志、固定SHA；不能仅修改报告状态。

## T-A08 · P2 · Windows加载错误在恢复线程错误模式后才读取

- 范围：[src/plugins/PluginLoader.cpp#L211-L220](https://github.com/QWERDF007/VisionRT/blob/adb01bdc6f22bcd1126ded3ff72a6da8d98d98f0/src/plugins/PluginLoader.cpp#L211-L220)及对应调用入口/测试。
- 实现：紧接LoadLibraryExW保存错误码，再恢复模式并报告已保存值；RAII恢复仅在初次设置成功后启用，保留已有错误模式位，处理初次设置失败。
- 完成标准：真实缺失传递依赖、无效DLL、中文路径的失败码/文本正确；线程原错误模式恢复，其他线程不受影响。
- 交付：实现与必要回归测试、原始命令/日志、固定SHA；不能仅修改报告状态。

## T-A09 · P2 · 节点预处理和后处理时间仍为固定占位

- 范围：[src/ui/DurationText.cpp#L9-L22](https://github.com/QWERDF007/VisionRT/blob/adb01bdc6f22bcd1126ded3ff72a6da8d98d98f0/src/ui/DurationText.cpp#L9-L22)及对应调用入口/测试。
- 实现：按Snapshot真实已测字段展示准备、初始化、工具调用、输出校验；未测保留—。定义嵌套阶段关系，不重复求和。工具自主上报算法时间时另有明确字段，不能从adapterExecuteMs猜测。
- 完成标准：冷/热执行、输入准备失败、Python异常和成功调用覆盖；UI值与快照一致，未测与0不同。
- 交付：实现与必要回归测试、原始命令/日志、固定SHA；不能仅修改报告状态。

## T-A10 · P2（设计收敛） · 连接校验新增了公共函数，但规则仍在多入口复制

- 范围：[src/core/ProjectEditor.cpp#L351-L420](https://github.com/QWERDF007/VisionRT/blob/adb01bdc6f22bcd1126ded3ff72a6da8d98d98f0/src/core/ProjectEditor.cpp#L351-L420)及对应调用入口/测试。
- 实现：统一有效绑定语义和BindingCheck，候选/提交/加载/执行/临时布局都消费同一规则。批量移动用候选坐标视图，不在真实图上试改；只在每次查询构建节点索引，无需通用规则引擎。
- 完成标准：同级/上级/自身/跨flow/类型错的结果在所有入口一致；手动模式保留无效旧source不影响实际执行，切回source时重新验证；多节点移动失败整批回滚。
- 交付：实现与必要回归测试、原始命令/日志、固定SHA；不能仅修改报告状态。

## 集成门禁

T-A02和T-A03先完成；T-A01与旧ACK测试同时修改，T-A04保证单次/最终读回无轮询推进依赖；其他工单可在相应模块收敛。最终验证需要真实宿主validator与Tools安装包，不能以stub替换原生加载/桥接来宣称端到端通过。代码只有通过相关原生和GUI回归后才关闭工单。
