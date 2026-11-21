# EasyTouch 与训练开发票据

[产品规格与 A1–A8](../VisionRT_EasyTouch与训练实施方案.md) | [源码、测试与运行入口](evidence.md) | [历史实验依据](history.md)

按每张票的 **Blocked by** 取可开工项，编号是拓扑顺序，不是串行日程。`ready-for-agent` 表示描述可交付给开发者，并非依赖已解除或功能已经实现。需求归规格、阻塞关系归票据、实际执行进度归仓库任务账本。

01（SDK 声明读写）与 03（原生训练值）已完成；02（声明校验）和 04（Python 训练集视图）的依赖已解除，可以并行开工。随包线缆示例由 14 在安装验收时验证。

## 票据

1. [普通工具通过同一 SDK 声明往返](issues/01-sdk-declaration.md)
2. [作者与宿主报告相同的声明错误](issues/02-authoring-catalog.md)
3. [原生插件可消费同一张原图的多条记录](issues/03-native-training-set.md)
4. [Python 训练器持有子视图](issues/04-python-training-set.md)
5. [圆测量一次点击提交完整配置](issues/05-circle-pick.md)
6. [行间距复用声明点选](issues/06-row-spacing-pick.md)
7. [把声明指定的原始帧加入内存草稿](issues/07-memory-training.md)
8. [点击应用，成功一次发布、失败可重试](issues/08-apply-publication.md)
9. [在工作台手绘取样并发布颜色模型](issues/09-color-model.md)
10. [用参考原图训练两条实测边](issues/10-edge-model.md)
11. [在训练图点选和追加参考边](issues/11-edge-assist.md)
12. [另存后独立重开已发布模型](issues/12-portable-assets.md)
13. [自定义工具编辑与复制保留能力声明](issues/13-tool-editor.md)
14. [用唯一协议安装并验收全部内置工程](issues/14-install-cutover.md)
