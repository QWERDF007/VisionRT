# 变距尺子标定测量

打开 [工程](变距尺子标定测量.vrtapp)，在设置中选择含 NumPy 的 Python 环境，然后执行流程。原图位于 [集中图片目录](../../images/变距尺子标定测量/)。

平面标定的训练原图与标定模型一起打包于 `assets/calibration-model.zip`，标定板描述文件位于 [calplate_80mm.cpd](assets/calplate_80mm.cpd)，可在训练视图直接查看或重新应用标定。检测图中的参考线长度为 55 mm，用于确定当前测量平面的尺度。中心距统计输出平均值（mm）和样本标准差（μm）。

所有相对路径基于安装目录解析；算法与实际数值验证见 [工具验证](../../../tests/calibration_example_contract.cpp)。
