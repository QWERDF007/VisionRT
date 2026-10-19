# 02: SDK 可选 PythonInterop 贯通真实工具执行

**What to build:** 宿主执行真实 Python 工具时使用 SDK 中唯一的通用转换实现，C++ 结果和 Python 对象生命周期保持正确；只使用核心 SDK 的消费者仍无需 Python。

**Blocked by:** None (can start immediately after draft approval).

**Status:** completed — 已完成并通过验收测试。

**Specification:** [D-10、D-11、D-12；US-25 至 US-32；T-06、T-07、T-10](../spec.md)。

- [x] 可选目标集中提供现有输入/输出转换；宿主切换到该实现，删除重复实现，不引入 UI、调度器或宿主指标类型依赖。
- [x] 核心 SDK 关闭 Python 支持后仍可独立构建、安装并由真实消费者使用；启用可选目标后宿主实际 Python 执行通过。
- [x] 输入的地址、dtype、shape、真实 strides、只读标志和 owner 生存期符合规格；覆盖 ROI、空图和多 dtype，不仅断言未抛异常。
- [x] 输出仍由纯 C++ 存储持有；Python 原数组释放或修改后结果符合复制契约，错误 dtype/rank 和不支持布局明确拒绝。
- [x] 模型对象跨执行复用，异常、停止、刷新及退出通过真实 Python/NumPy 生命周期测试，不用 `.release()` 丢弃引用。
- [x] 解释器继续由宿主管理；不引入新的解释器单例、任意输出零拷贝或另一套数组绑定库。
- [x] 先在现有 Python 工具执行与 SDK 独立消费 Seam 写失败测试，再迁移；SDK 与宿主锁定版本同步后可独立构建。

**验证演示：** 在同一 Python 类实例上连续处理 ROI 图像，核对输入布局与输出数值；停止、刷新并销毁后确认引用按约定释放，随后验证纯 C++ SDK 消费者。
