# 线缆历史实验与参考来源

这组图用于排查右边定位失败和参考姿态偏移，可在遇到同类问题时选用。发布时的功能验收见[开发规格](../VisionRT_EasyTouch与训练实施方案.md#验收规格)。

两份结果各有 11 条记录。原配置只有 03、05、11 完成对齐和识色，其余 8 张均在右边定位返回 105006。仅将右侧搜索带短边从 44 改为 88 px，11 张均返回了五个工位结果。另一方面，在参考图上将底边名义端点与搜索中心沿法线一起移 10 px，矩阵的 Y 平移从 +0.896 变为 −9.049 px。前一个实验说明需要设置搜索容差，后一个说明需要从图像中重新训练参考边。两项修复要分别证明。

**历史输出对照。** 下表直接来自包内两份 JSON，方便后续区分定位回归、真实换序与颜色拒识。88 px 列是旧 DLL 放宽搜索范围后的输出，不充当人工真值。

| 帧号 | 原 44 px 右侧搜索带 | 88 px 实验的五工位输出，按原顺序 |
| --- | --- | --- |
| 01 | 右边未找到 | 红、紫、蓝、黄、紫 |
| 02 | 右边未找到 | 紫、红、蓝、黄、紫 |
| 03 | 完成 | 红、紫、蓝、黄、紫 |
| 04 | 右边未找到 | 紫、红、蓝、黄、紫 |
| 05 | 完成 | 紫、红、蓝、黄、紫 |
| 06 | 右边未找到 | 红、紫、蓝、黄、unknown |
| 07 | 右边未找到 | 紫、红、蓝、黄、紫 |
| 08 | 右边未找到 | unknown、红、蓝、黄、紫 |
| 09 | 右边未找到 | unknown、紫、蓝、黄、红 |
| 10 | 右边未找到 | 紫、红、蓝、黄、紫 |
| 11 | 完成 | 紫、红、蓝、黄、紫 |

## 历史来源

以下源码链接固定到 2026-11-18 方案考察的历史提交；当前开发入口另见[源码索引](evidence.md)。压缩包中的 DLL 结果是历史测量，基线分析复核了 JSON、示例图与探针摘要，未在 Windows 安装版复跑 11 张图，也没有 MERLIC 实机结果。

- **SDK 与值桥。** [公开值类型](https://github.com/QWERDF007/VisionRTContracts/blob/76bc8b6eb4477a16addd18a56eda48180d610b7d/include/visionrt/contracts/Value.h) ｜ [模块 ABI](https://github.com/QWERDF007/VisionRTContracts/blob/76bc8b6eb4477a16addd18a56eda48180d610b7d/include/visionrt/contracts/Module.h#L15) ｜ [工程值复制](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/core/ProjectValues.cpp#L5) ｜ [Python 值桥](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/python_bridge/PythonInterop.cpp)
- **宿主声明与编辑。** [声明解析](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/core/DefinitionJson.cpp#L377) ｜ [助手上下文](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/ui/AppControllerAssist.cpp#L195) ｜ [训练入口](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/ui/AppControllerTraining.cpp) ｜ [从矩形生成端点](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/ui/TrainingSession.cpp#L396) ｜ [参数事务](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/core/ProjectEditor.cpp#L823)
- **运行与保存。** [上游结果有效性](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/runtime/RuntimeEngine.cpp#L79) ｜ [实例配置及比较](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/runtime/NodeRuntime.cpp#L93) ｜ [工程保存](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/core/ProjectStorage.cpp#L979) ｜ [安装示例路径规则](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/core/include/core/ExampleProject.h)
- **真实算法。** [点选直边](https://github.com/QWERDF007/VisionTools/blob/97b8fc87dec230f0af2694cf764b040195c4bfa0/src/recognition/StraightEdgeProbe.cpp#L105) ｜ [直边拟合与姿态](https://github.com/QWERDF007/VisionTools/blob/97b8fc87dec230f0af2694cf764b040195c4bfa0/src/recognition/StraightEdgeLocator.cpp#L60) ｜ [颜色训练与统计](https://github.com/QWERDF007/VisionTools/blob/97b8fc87dec230f0af2694cf764b040195c4bfa0/src/recognition/ColorModel.cpp#L75) ｜ [识色初始化](https://github.com/QWERDF007/VisionTools/blob/97b8fc87dec230f0af2694cf764b040195c4bfa0/src/recognition/RecognizeColorTool.cpp#L56)
- **可扩展的现有测试。** [工具算法契约](https://github.com/QWERDF007/VisionTools/blob/97b8fc87dec230f0af2694cf764b040195c4bfa0/tests/test_recognition_contract.cpp) ｜ [工作台训练](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/tests/workbench_training_contract.cpp) ｜ [安装示例运行](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/tests/example_pipelines.cpp)
- **压缩包。** 目标范围见《VisionRT_EasyTouch与训练统一开发方案.md》，业务对照见《MERLIC_线缆颜色方案对照.md》。历史实验在 `evidence/merlic-cables-runtime/` 的 `manifest.json`、`local-baseline-results.json`、`local-wide-results.json` 和 `local-probe.cpp`。MERLIC 的内部拟合及分类公式未获实证，本方案使用 VisionTools 自有算法。
- **现有界面入口。** [处理和训练页签](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/ui/qml/ResultsModeTabs.qml) ｜ [加入训练](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/ui/qml/ResultsPanel.qml#L16) ｜ [应用训练数据](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/ui/qml/ResultsTrainingRecords.qml#L123) ｜ [现有全局点选开关](https://github.com/QWERDF007/VisionRT/blob/8d1f6de7415becf010c936dbadf7b114cdd33505/src/ui/qml/ResultsImageToolbar.qml#L64)
- **厂商操作说明。** 直边点选后调整对比度、边宽和跃迁方向，最低完整度由用户设定。颜色通过命名取样区域并应用来训练。[MERLIC 26.10 直边对齐](https://www.mvtec.com/doc/merlic/26.10/manual/en-us/Content/Tool_reference/Alignment/determine_alignment_with_straight_border.html) ｜ [MERLIC 26.10 颜色训练](https://www.mvtec.com/doc/merlic/26.10/manual/en-us/Content/Tool_reference/Processing/Checking/recognize_color.html)
- **原始 Mat 存储选项。** [OpenCV 4.5 FileStorage 模式](https://docs.opencv.org/4.5.0/da/d56/classcv_1_1FileStorage.html)
