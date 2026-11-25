# 畸变标尺校正测量

打开 [工程](畸变标尺校正测量.vrtapp)，在设置中选择含 NumPy 的 Python 环境，然后执行流程。原图位于 [集中图片目录](../../images/畸变标尺校正测量/)。

平面标定的强畸变标定板原图与标定模型一起打包于 `assets/calibration-model.zip`，标定板描述文件位于 [calplate_80mm.cpd](assets/calplate_80mm.cpd)。图像校正工具对畸变标尺图像执行去畸变并输出适配标定数据与像元尺度，行间距测量与表达式节点据此计算刻线平均中心距（mm）与样本标准差（μm）。

所有相对路径基于安装目录解析；算法与端到端验证见 [示例验证](../../../tests/merlic_examples_contract.cpp)。
