# 识别线缆颜色

使用自带的参考图和训练资源包，先定位连接器的两条直边，再按五个工位识别线芯颜色。颜色工具读取原图，对齐矩阵只映射检测区域。

## 运行

打开 [工程](识别线缆颜色.vrtapp) 并运行。原图见 [示例图像](../../images/识别线缆颜色/)，训练资源包（`edge-model.zip`、`color-model.zip`，内含模型与训练原图）位于 [assets](assets/)，相对路径均基于安装目录解析。

## 训练

训练元数据与搜索区域见 [工程](识别线缆颜色.vrtapp)，参考图与模型打包于 [assets](assets/)。交互入口见 [参数页](../../../src/ui/qml/ParameterPanel.qml)、[训练记录](../../../src/ui/qml/ResultsTrainingRecords.qml)、[EasyTouch](../../../src/ui/AppControllerAssist.cpp)。

训练与发布流程见 [工作台接线](../../../src/ui/AppControllerTraining.cpp) 与 [TrainingArchive](../../../3rdparty/VisionRTContracts/include/visionrt/contracts/TrainingArchive.h)；真实操作验收见 [训练发布测试](../../../tests/workbench_training_publish_contract.cpp)。

## 预期结果

参考图的对齐应接近单位变换；五个工位自上而下对应 purple、red、blue、yellow、purple。颜色、分数、比例与中心按相同索引对应。姿态界限和逐工位数值验收以 [真实示例测试](../../../tests/example_pipelines.cpp) 为入口。
