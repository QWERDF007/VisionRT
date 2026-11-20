# 01：普通工具通过同一 SDK 声明往返

**What to build:** 工具作者用已安装的 SDK 读写普通工具，工作台加载同一份声明仍能运行 ReadImage。这是拆出声明读写的第一条可独立交付路径。

**Blocked by:** 无，可立即开始。

**Status:** done（2026-11-21，验证见 [任务账本](../../../WORKLOG.md) 最新条目）

**归属：** Contracts、VisionRT | **规格验收：** [A1、A8](../../VisionRT_EasyTouch与训练实施方案.md#验收规格)

## 验收条件

- [x] 先用 SDK 独立消费者读取真实普通工具，重写后由宿主包加载器发现并执行；元数据、默认值、动态端口引用及省略 authoring 的语义一致。
- [x] 把 PortRef 和声明值的读写移入 SDK，复用已有 PortStage；宿主仅补路径来源、目录分类、可用性和运行时派生信息。SDK 公共头无 Qt 或 JSON 库类型，安装消费者无需链接 Qt。
- [x] 以 int64 边界、超界整数、整数位置的小数及数组元素验证精度；保留数字枚举编码、阶段独立编号、宿主保留端口和可定位的字段错误。
- [x] 在迁移分支中尚未转接的旧能力只允许走原有入口；普通字段不得有两个实现。开发完成的消费者均能编译运行。

## 落地位置

- SDK 声明模型：[Declaration.h](../../../3rdparty/VisionRTContracts/include/visionrt/contracts/Declaration.h)
- SDK 声明读写：[DeclarationJson.h](../../../3rdparty/VisionRTContracts/include/visionrt/contracts/DeclarationJson.h)
- 宿主加载适配：[DefinitionJson.cpp](../../../src/core/DefinitionJson.cpp)
- 契约：[SDK 声明契约](../../../3rdparty/VisionRTContracts/tests/test_declaration_contract.cpp)、[宿主声明契约](../../../tests/definition_json_contract.cpp)、[安装态消费者](../../../3rdparty/VisionRTContracts/tests/installed_consumer/main.cpp)

## 遗留

- VisionTools 仍指向旧 Contracts gitlink，按票 03 与 14 的集成分支一并同步；该仓不消费声明读写，当前编译不受影响。
