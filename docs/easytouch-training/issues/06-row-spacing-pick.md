# 06：行间距复用声明点选

**What to build:** 操作员可点选条带并一并更新 ROI、物体极性、合格宽度下限和上限；工作台沿用圆测量已验证的提交机制。

**Blocked by:** [05 圆测量一次点击提交完整配置](05-circle-pick.md)

**Status:** ready-for-agent

**归属：** VisionTools、VisionRT | **规格验收：** [A1、A2](../../VisionRT_EasyTouch与训练实施方案.md#验收规格)

## 验收条件

- [ ] 发布匹配声明版本的 PickRowSpacing 和 MeasureRowSpacing；以真实条带图观察四个目标更新和测量输出。工具侧承担整组替换/追加；宿主不按工具名分支。
- [ ] 验证单击替换、Ctrl 追加、共享参数冲突和一项目标接上游时没有部分提交；退出节点后迟到预览不进入新节点，圆测量定向回归通过。
