# VisionTools补充Specs

## S31-VT-01 纯算法测试独立于推理模块

增加始终可构建的test_yolo_postprocess，直接编译YoloPostprocess.cpp和纯测试，不链接InferRT、CUDA、工具DLL或Qt。当前三个后处理测试迁到此目标，不复制保留两份；test_infer_contract只保留模块注册、工厂和初始化契约，真实GPU验证另层执行。

保留网络坐标候选trace；先排除逆映射后面积为0的候选，再NMS和max_detections。增加全padding、部分越界有效、同分数稳定顺序、不同类别、非有限输入等正反例。不要在没有改变契约的情况下把NMS坐标改为裁剪后的源图坐标。

## S31-VT-02 七工具原生实现

完整端口及算法见[端口契约](七工具目标规范/12_端口契约.md)及对应Specs。当前read工具作为扩展起点；已有gray/gaussian/threshold不是旋转或亮度，已有projection不是圆/行间距测量。

使用OpenCV变换、转换、采样、SVD及标准C++的卡尺/配对/鲁棒拟合。CircleFit与EdgeProfile供正式工具和helper共同使用。推荐目录src/common单一几何组件、src/metrology测量模块；工厂注册手写，声明随包，不引入HALCON或构建生成器。

圆的最终内点决定完整度和角覆盖；行间距保留所有完整配对，accepted只标记宽度是否合格，不先筛掉异常物体。两类工具透传原Image/Owner作为叠加底图，ROI输出关联image_port。

## S31-VT-03 表达式与Python交付

使用已有内建Python加载通道；新增共享受限AST引擎、ExpressionTool、BranchTool和声明。simpleeval锁定实际验证版本、离线随包，不依赖运行时联网安装。解释器与工具包中的模块路径必须可追溯到安装目录，不回退开发源码。

initialize编译，execute更新输入并计算；严格Bool条件，异常两路Missing。输入输出为既有Values容器，工具不解析自己的声明JSON。C++/Python镜像类型遵守现有Contracts，不添加readonly/COW或任意混合元组。

## S31-VT-04 数值与交互验收

合成图带解析真值，固定旋转方向、非整数中心/边缘、遮挡和噪声；标准无噪声夹具以≤0.25px为目标，必须注明图像采样与适用参数。真实样本没有提供时标未验，不得声称工业测量准确率达标。

helper仅返回候选/提案，不推进正式实例或目录游标。最大搜索范围、样本预算和latest-only队列用于保证交互有界。hover无候选正常返回candidate_valid=false；learn无效不得用默认值覆盖工程。
