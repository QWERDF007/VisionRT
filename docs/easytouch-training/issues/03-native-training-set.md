# 03：原生插件可消费同一张原图的多条记录

**What to build:** 独立原生训练工具通过现有工具调用收到 TrainingSet；在宿主释放调用对象后，持有的合法引用仍可读取原像素。

**Blocked by:** 无，可立即开始。

**Status:** ready-for-agent

**归属：** Contracts、VisionRT、VisionTools | **规格验收：** [A8](../../VisionRT_EasyTouch与训练实施方案.md#验收规格)

## 验收条件

- [ ] 在需要共同改动的集成分支中同时升级 ABI 至 11，保留现有类型的数值；同步两父仓 gitlink 并重编实际使用的 DLL。ABI 10 插件在创建实例前被拒，普通工具正例仍执行成功。
- [ ] 图像表持有独立单帧 Mat、颜色顺序及 imageId；记录表通过 imageId 关联，包含 recordId 与按 fieldId 标识的标量、Array 或 ROI。不嵌套图像或训练集；非法引用、类型和重复 ID 在入口失败。
- [ ] 用真实已安装 SDK 编译消费插件，覆盖 typeOf、validate、retain 和子对象寿命。验证两条记录引用一张图时像素仅持有一份，上游换帧或释放不改变训练像素。
- [ ] 宿主各类型分派明确处理新类型：训练集只作训练调用值，工程字面量和普通 JSON 不序列化它；不静默变成 Missing。仅运行受影响类型和模块契约。
