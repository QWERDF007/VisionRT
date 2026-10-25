# Tickets

## T-D01 · P2 · 关闭期间保存与同步入口封闭

范围：AppController::save、Impl::synchronizeNodes、保存队列派发、必要QML Action状态。

实现：复用ClosePhase判断。后端入口在解析保存路径/发对话框/写文件之前拒绝不可逆关闭阶段的新保存；同步入口同样拒绝。保存未保存工程的决策发生在ClosePhase::None，保留现有行为。队列SaveRequest在批准关闭前完成或明确取消，不在prepareShutdown后重新配置节点。

验收：现有外部owner生命周期测试扩展Ctrl+S和直接save，检查实例构造/释放次数、文件内容和closeReady次数；未保存确认选保存仍正常。无需新测试目标，可扩展既有workbench_lifecycle与workbench_contract。

## T-D02 · P3 · 提升关键并发回归可判别性

范围：runtime_readback_contract、ui_readback_contract。

实现：保留现有正常用例，关键负例通过可控交错/事件观察或反向变异验证，使旧Stop谓词及错误guard确实被捕获。同步重入用例覆盖outer drain已生效的时刻。有限等待用于测试超时，不把50ms本身当worker处理证明。

验收：记录每个用例针对的错误实现和失败信号；正确实现通过，错误变异失败。属于测试质量改进，不阻断已验证的正常功能。

## T-D03 · 补验记录 · 收拢安装部署证据

范围：现有部署验证，不新增产品框架。

保留用户76/76、严格包、截图启动、installed_pipeline、installed_python_author结果。补原始日志与实际进程解释器/NumPy/DLL路径，记录实际App执行→Loop停止→最终显示→直接关窗。已有SHA256不作为新增要求，不加散列校验系统。

验收：来源明确，未跑项标未跑；截图存在与像素/ROI功能验证分别记录。测试scratch继续D盘。修改D01后运行相关回归，必要发布全量遵循现有流程，不反复跑未变算法消耗时间。
