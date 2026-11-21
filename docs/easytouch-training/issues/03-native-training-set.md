# 03：原生插件可消费同一张原图的多条记录

**What to build:** 独立原生训练工具通过现有工具调用收到 TrainingSet；在宿主释放调用对象后，持有的合法引用仍可读取原像素。

**Blocked by:** 无，可立即开始。

**Status:** done（2026-11-21，验证见 [任务账本](../../../WORKLOG.md) 最新条目）

**归属：** Contracts、VisionRT、VisionTools | **规格验收：** [A8](../../VisionRT_EasyTouch与训练实施方案.md#验收规格)

## 验收条件

- [x] 在需要共同改动的集成分支中同时升级 ABI 至 11，保留现有类型的数值；同步两父仓 gitlink 并重编实际使用的 DLL。ABI 10 插件在创建实例前被拒，普通工具正例仍执行成功。
- [x] 图像表持有独立单帧 Mat、颜色顺序及 imageId；记录表通过 imageId 关联，包含 recordId 与按 fieldId 标识的标量、Array 或 ROI。不嵌套图像或训练集；非法引用、类型和重复 ID 在入口失败。
- [x] 用真实已安装 SDK 编译消费插件，覆盖 typeOf、validate、retain 和子对象寿命。验证两条记录引用一张图时像素仅持有一份，上游换帧或释放不改变训练像素。
- [x] 宿主各类型分派明确处理新类型：训练集只作训练调用值，工程字面量和普通 JSON 不序列化它；不静默变成 Missing。仅运行受影响类型和模块契约。

## 落地位置

- 值类型：[TrainingSet.h](../../../3rdparty/VisionRTContracts/include/visionrt/contracts/TrainingSet.h) + [TrainingSet.cpp](../../../3rdparty/VisionRTContracts/src/TrainingSet.cpp)
- 变体与语义：[Value.h](../../../3rdparty/VisionRTContracts/include/visionrt/contracts/Value.h) + [Value.cpp](../../../3rdparty/VisionRTContracts/src/Value.cpp)
- ABI 与拒绝：[Module.h](../../../3rdparty/VisionRTContracts/include/visionrt/contracts/Module.h)、[PluginLoader](../../../src/plugins/PluginLoader.cpp)、[ABI 10 夹具](../../../tests/fixtures/previous_abi_plugin.cpp)
- 契约：[训练集契约](../../../3rdparty/VisionRTContracts/tests/test_training_set_contract.cpp)、[安装态消费者](../../../3rdparty/VisionRTContracts/tests/installed_consumer/main.cpp)

## 落地说明

- 记录的字段用独立枚举而非 PortValue：字段按构造就排除了图像与训练集，不需要在入口再拒绝一次嵌套。
- imageId/recordId 是 `std::map` 的键，重复 ID 无法被表示；无效图号与非法像素在 `validateTrainingSet` 失败。
- 同一个值的两次出现只在与自身同一对象时相等；训练集不进缓存比较，也不写工程。

## 遗留

- Python 训练器视图是票 04，当前 Python 桥对训练集明确报错，不静默转换。
- 本机 `build/bin/tools-package` 已用 ABI 11 重新打包并验证（25 个工具、原生工厂可加载）；正式安装包与发布证据归票 14。
