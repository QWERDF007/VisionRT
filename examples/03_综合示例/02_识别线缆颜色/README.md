# 识别线缆颜色

使用自带的参考图和模型，先定位连接器的两条直边，再按五个工位识别线芯颜色。颜色工具读取原图，对齐矩阵只映射检测区域。

## 运行

打开同目录工程并运行。图像来自 `images/`，已发布训练资产位于 `识别线缆颜色.assets/training/`；节点用相对 `training_asset` 引用完整修订。复制工程时一同复制资产目录。

## 训练

训练配置与搜索区域见 [工程](识别线缆颜色.vrt.json)，参考图与模型见 [资产目录](识别线缆颜色.assets/training)。交互入口见 [参数页](../../../src/ui/qml/ParameterPanel.qml)、[训练记录](../../../src/ui/qml/ResultsTrainingRecords.qml)、[EasyTouch](../../../src/ui/AppControllerAssist.cpp)。

训练、模型保存和发布见 [工作台接线](../../../src/ui/AppControllerTraining.cpp)、[资产准备](../../../src/ui/TrainingAssets.cpp)；真实操作验收见 [训练发布测试](../../../tests/workbench_training_publish_contract.cpp)。

## 预期结果

参考图的对齐应接近单位变换；五个工位自上而下对应 purple、red、blue、yellow、purple。颜色、分数、比例与中心按相同索引对应。姿态界限和逐工位数值验收以 [真实示例测试](../../../tests/example_pipelines.cpp) 为入口。

可替换 `images/` 中的图片观察姿态与工位坐标变化，也可修改训练区域、标签或检测阈值。修改检测阈值只影响识别判定；修改训练来源后需要显式应用。
