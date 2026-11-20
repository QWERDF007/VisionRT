# 04：Python 训练器持有子视图

**What to build:** Python 作者在用户选择的解释器中直接读取训练集原图与字段，工具返回后持有的 NumPy 切片仍有效。

**Blocked by:** [03 原生插件可消费同一张原图的多条记录](03-native-training-set.md)

**Status:** ready-for-agent

**归属：** Contracts、VisionRT | **规格验收：** [A8](../../VisionRT_EasyTouch与训练实施方案.md#验收规格)

## 验收条件

- [ ] 给训练器只读结构视图：images 按 imageId 暴露原图和颜色顺序，records 暴露 recordId、imageId 和 fields；整数键保持精确，不依赖列表位置建立身份。
- [ ] 复用现有 NumPy 图像/数组桥和 Owner；验证原图像素、dtype、rank、颜色顺序、标量与 ROI 的形状。释放父对象、刷新工具后已持有的合法子视图仍可读。
- [ ] 用真实选定解释器执行一次训练器；保留子视图时关闭等待，释放后排空。错误类型和不可用环境沿原通道报告，无系统解释器兜底。
