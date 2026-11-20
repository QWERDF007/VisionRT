# 开发基线与验证入口

[规格](../VisionRT_EasyTouch与训练实施方案.md) | [开发票据](README.md) | [历史实验来源](history.md)

## 基线身份与差距

2026-11-21 的本地状态：VisionRT 指向 Contracts `5db4497`（声明读写已在 SDK），VisionTools 仍为 `c862304b32a0d31a4afb80425589094ea6047b44` 与旧 Contracts gitlink，按票 03 和 14 同步。以下是源码入口，不是已通过候选版验收的声明。工具仓链接固定到核对过的提交。

| 交付前须消除的差距 | 直接证据与可复用边界 |
| --- | --- |
| 公开合同与宿主声明分散，旧示意字符串不是产品编码 | [SDK 枚举](../../3rdparty/VisionRTContracts/include/visionrt/contracts/ProtocolEnums.h)、[PortValue](../../3rdparty/VisionRTContracts/include/visionrt/contracts/Value.h)、[模块 ABI](../../3rdparty/VisionRTContracts/include/visionrt/contracts/Module.h)、[SDK 声明读写](../../3rdparty/VisionRTContracts/include/visionrt/contracts/DeclarationJson.h)、[宿主声明](../../src/core/include/core/ToolDefinition.h)、[宿主加载适配](../../src/core/DefinitionJson.cpp)。当前 ABI 10，声明根 schema 1，项目格式 8，旧训练资产版本 2；authoring 的版本单独处理。 |
| 声明校验与真实 DLL 注册是两个不同检查 | [工具包](../../src/core/ToolPackage.cpp)、[宿主 CLI](../../tools/visionrt_validate_tool.cpp)、[工具打包](https://github.com/QWERDF007/VisionTools/blob/c862304b32a0d31a4afb80425589094ea6047b44/tools/package_tools.py)。工具仓目前调用宿主校验器；迁移声明校验到可安装的 SDK，保留真实安装闭包和工厂注册检查。 |
| 处理页仍找首个 ROI，训练页仍按业务种类工作 | [助手上下文与回写](../../src/ui/AppControllerAssist.cpp)、[有界队列](../../src/ui/AssistPreview.cpp)、[现有批量输入事务](../../src/core/ProjectEditor.cpp)、[工具编辑器](../../src/ui/ToolEditorController.cpp)。扩展已有事务到 init 和普通输入，保留一次提交与过期拒绝。 |
| 加图会读显示图、可能先改 init，再写 PNG；应用先落盘再重载 | [两个训练入口](../../src/ui/AppControllerTraining.cpp)、[训练记录与写盘](../../src/ui/TrainingSession.cpp)、[结果展示与读回](../../src/ui/ResultPresentation.cpp)、[图像视口](../../src/ui/ImageViewport.cpp)。将原始帧快照与 QImage 展示分开。 |
| 当前项目只原子提交工程 JSON；运行按节点参数判断实例是否可复用 | [保存与重开](../../src/core/ProjectStorage.cpp)、[参数物化](../../src/core/NodeContracts.cpp)、[实例比较](../../src/runtime/NodeRuntime.cpp)、[调度](../../src/runtime/RuntimeEngine.cpp)。资产发布须跨过文件就绪与工程内存提交两个边界；QSaveFile 自身只保证单个文件。 |
| 宿主构造直边端点；颜色初始化仍重训 | [直边加载](https://github.com/QWERDF007/VisionTools/blob/c862304b32a0d31a4afb80425589094ea6047b44/src/recognition/AlignStraightBordersTool.cpp)、[直边拟合](https://github.com/QWERDF007/VisionTools/blob/c862304b32a0d31a4afb80425589094ea6047b44/src/recognition/StraightEdgeLocator.cpp)、[颜色统计](https://github.com/QWERDF007/VisionTools/blob/c862304b32a0d31a4afb80425589094ea6047b44/src/recognition/ColorModel.cpp)、[识色初始化](https://github.com/QWERDF007/VisionTools/blob/c862304b32a0d31a4afb80425589094ea6047b44/src/recognition/RecognizeColorTool.cpp)。建模和消费模型共用工具侧编解码。 |
| 随包线缆示例的输入与断言 | [线缆示例](../../examples/03_综合示例/02_识别线缆颜色/README.md)、[实际示例断言](../../tests/example_pipelines.cpp)。仓库有 11 个内置工程；线缆示例使用 `cable_11.png`，训练副本与其 SHA-256 同为 `06377bc16bfa83cf4656d81a3214290a2e6f01bdc7dcd8ce95b129924e047d01`。 |

## 验证入口

| 观察什么 | 用哪个现有入口扩展 |
| --- | --- |
| 用户真实的悬停、点击、Ctrl 追加、应用、保存、重开 | [工作台训练](../../tests/workbench_training_contract.cpp)、[处理助手](../../tests/workbench_assist_contract.cpp)、[助手释放](../../tests/workbench_assist_release_contract.cpp)。复用 Qt 事件、真实控制器/插件和隔离工程；不要新增颜色、尺寸等外观断言。 |
| 原图保活、SDK 独立安装与包级诊断 | [安装消费者](../../3rdparty/VisionRTContracts/tests/installed_consumer)、[声明契约](../../tests/definition_json_contract.cpp)、[包校验 CLI](../../tests/validator_cli_contract.py)、[Python 子视图寿命](../../tests/python_array_lifetime_contract.cpp)。 |
| 不完整草稿、失败恢复、持久化和只读示例 | [训练会话](../../tests/training_session_contract.cpp)、[工程存储](../../tests/project_storage_contract.cpp)、[工程编辑](../../tests/project_editor_contract.cpp)、[示例归属](../../tests/example_project_contract.cpp)。使用临时工程和受控失败点；同时采集创建/写入/删除事件，目录前后相同不能证明中间未写。把正常日志、显式保存工程文件与训练资产写入分开归因。 |
| 真实算法、工位数值、安装后的调用和 DLL 身份 | [工具算法契约](https://github.com/QWERDF007/VisionTools/blob/c862304b32a0d31a4afb80425589094ea6047b44/tests/test_recognition_contract.cpp)、[示例执行](../../tests/example_pipelines.cpp)、[安装执行](../../tests/installed_pipeline_contract.cpp)、[发布证据](../../tools/release_evidence.py)。正例需要真实像素、工具与返回值；截图只作视觉旁证。 |

宿主的目标、分组与环境以[测试编排](../../tests/CMakeLists.txt)为准。下面是**后续开发命令**示例，在配置好的 Release 构建树中按票选用，不代表本次文档工作已运行它们：

```powershell
tools/build.cmd cmake --build build --config Release --target workbench_training_contract workbench_assist_contract
ctest --test-dir build -C Release -R '^(workbench_training_contract|workbench_assist_contract)$' --output-on-failure --no-tests=error
```

发布时用与本次 gitlink 同源的 SDK、宿主和工具包，按[打包和校验入口](../../tools/README.md)验证。按构建配置区分可立即运行的例子、需要所选 Python/NumPy 或 GPU/权重的例子、预期失败的诊断例子。证据包含安装身份、输入摘要、一次应用的文件事件与执行修订、实际进程加载 DLL 和随包示例的运行结果。

## 第三方复用依据

- 声明 JSON：Contracts 内部使用固定提交版本的 [RapidJSON Reader/Writer](https://github.com/Tencent/rapidjson/blob/master/doc/sax.md)，以 RawNumber 在类型入口调用 SDK 的精确整数转换，不把第三方类型暴露给消费者。只对工具声明换读写边界；工程 JSON 和 Qt 编辑界面不因此重写。
- 来源图：复用 [OpenCV FileStorage](https://docs.opencv.org/4.x/da/d56/classcv_1_1FileStorage.html) 的内存、JSON、Base64 模式保存原 Mat；颜色顺序是独立元数据。用非 8 位和多通道样本验证逐像素往返，颜色训练首版对不支持的输入明确拒绝。
- 工程文件：保留 [Qt QSaveFile](https://doc.qt.io/qt-6/qsavefile.html) 禁用直接覆写回退的行为；不可变资产目录先准备好，再提交引用。无需新增数据库或通用文件事务框架。
