# 测量工具 Specs

全部使用 OpenCV 和标准 C++；不调用 HALCON、商业测量 SDK 或无法验证的专有算法。本方案的亚像素和精度是有验收样本约束的工程目标，不等同于任意真实图像上的保证。

## S-MET-01 共享边缘测量内核

首版图像支持 CV_8U 的灰度、BGR、BGRA，按 OpenCV 灰度转换处理，不随意取红色通道。其他深度明确报错；若后续需要 16 位，应独立定义梯度阈值量纲再扩展。图像转换缓存在单次执行内共享，不能用 data 指针作为跨轮缓存键。

两类正式测量工具增加 images 输出，直接透传输入 DataRef/Owner，不修改像素。测得圆、点集和线段通过既有 image_port 元数据关联该输出，确保单次运行时能使用现有显示链叠加结果，而不是只有数值没有底图。

`EdgeProfile` 接受采样中心线、法向搜索区间、切向平均宽度、Gaussian sigma、极性和最小梯度。使用双线性采样；只使用完整有效的采样窗口，不把越界像素补黑制造假边。先沿切向求均值获得一维强度序列，Gaussian 平滑后计算有符号一阶导数，提取局部极值。

亚像素定位对梯度峰及两邻点作抛物线插值：分母过小退回整点，偏移限定在 ±0.5；局部峰不完整则拒绝。平滑窗口半径为 ceil(3*sigma)，搜索窗口要预留此支持范围。梯度阈值是处理后灰度/像素量纲，参数名和 UI 一致。亮到暗对应负峰，暗到亮对应正峰。

同一射线上存在多个候选时，按距离名义边缘最近优先，再按梯度绝对值、位置稳定打破平局；不能每次由容器迭代顺序决定。诊断输出保留真实选中点和拟合残差；不把经验 quality 指数称为概率。

峰必须具有非零对比，min_gradient=0也不能把平坦区当作边缘。单ROI的原始双线性采样预算首版上限为8百万点，先用checked乘法估算后拒绝超限；默认参数远低于该上限。该预算是工具内部明确常量，不是数据读写属性，后续性能验证可调整并记录。

## S-CIRCLE-01 圆测量

输入每个名义 ROI 为 kind=3。沿 0..360° 均匀布置 num_calipers（默认64，至少12）；名义半径 r 周围 ±search_half_width 搜索，切向平均宽度 average_width；每个卡尺最多选一个符合 polarity 的边缘点。输入 ROI 与参数先全部合法性检查，r 必须大于搜索宽度，sigma/宽度/数量满足采样窗口要求。

对候选点做平移缩放归一化，使用 OpenCV solve(SVD) 求代数初值，再进行最多20次 Huber 加权几何最小二乘（cx,cy,r 三参数），收敛阈值在归一化空间定义。残差为 `norm(p-center)-r`；最终 outlier 阈值基于 MAD 和明确像素上限联合判断，重新拟合内点一次。退化点集、病态线性系统、负半径、发散都判该 ROI 无效，不循环尝试到偶然成功。

初值解 `x²+y²+D*x+E*y+F=0`，由D/E/F恢复圆心和半径；非正半径平方拒绝。Huber尺度使用 `max(0.05px, 1.4826*MAD)`，权重转折点为尺度的1.345倍；最终内点阈值为 `max(0.05px, min(3*max_rms, 2.5*1.4826*MAD))`。归一化参数增量范数≤1e-6即收敛；奇异值比显示条件数>1e12则退化。常量集中在CircleFit一处，以夹具验证后再调整，不能分散在UI与工具中各一份。

验收有效性共同要求：有效卡尺数至少 min_points、有效卡尺数/总卡尺数 ≥ min_completeness、有效角度覆盖 ≥ min_coverage_deg、r 在 radius_min/max 内、RMS ≤ max_rms。越界卡尺计入总数而不计有效数，防止半个圆出界仍宣称100%完整。覆盖角用有效角排序后的最大空缺计算，不能仅看点数。

这里的有效点、完整度、角覆盖和used_edges都依据最终内点集合，不能用剔除前的数量美化质量。有效角是对应卡尺的采样角；RMS以最终几何残差的像素单位计算。

输出结构详见端口表。valid/reasons 保留全部名义 ROI 顺序；成功几何压缩为 K 项，用 source_roi_indices 显式映射回原 ROI。circles、centers、radii、rms、completeness、used_edges 在各图的 K 维必须完全对齐。中心为当前图像像素坐标，used_edges 为 kind5 点集。无有效圆仍 status=0、valid_count=0，不能用 `"*"`、NaN 或混合类型占位。

单位：始终输出 radius_px；`units_per_pixel` 有限且>0，`unit` 为 px/mm 等显示文本，radius=radius_px*units_per_pixel。该比例描述当前图像像素尺寸，alignment 的缩放不再重复乘一次。未标定默认1 px；不把两个尺度不同的相机用一个比例冒充通用标定。

## S-ROWS-01 行间距测量

每个 ROI 使用 kind2 旋转矩形。以矩形中心为原点，u=(cosθ,sinθ) 为宽度/扫描方向，v=(-sinθ,cosθ) 为高度/平均方向。沿 u 生成一维投影，用共享 EdgeProfile 的滤波、梯度及亚像素定位；UI 画箭头指明扫描正向，避免角度反向后极性解释不一致。

object_polarity=bright 表示先正峰后负峰，dark 表示先负后正。按扫描顺序配对：出现多个同极性候选时在下一反极性前保留绝对梯度最大的候选，相等时选择靠前者；已配对边缘不复用。无首尾完整对的孤立边缘记为 unpaired_count，不制造零宽物体。

设完整配对 (a_i,b_i)，a_i<b_i；width_i=b_i-a_i；gap_i=a_(i+1)-b_i；pitch_i=((a_(i+1)+b_(i+1))-(a_i+b_i))/2。不得先删除宽度不合格的配对再计算间距，否则会吞掉弯针/连针或制造双倍节距。输出所有完整对的 width/gap/pitch，accepted 另表明宽度是否在范围；合格数量独立统计。

多个 ROI 的数据保留 `[image][roi][pair]`，二维数组返回每 ROI 状态/数量，pair_points 为 rank4 `[image][roi][pair][x1,y1,x2,y2]`，每对一行四个标量，rank 不超过现有4。overlay_segments 用 ROI kind1 编码每个被配对边缘的位置，可由参考高度构造可见短线段；它只是显示几何，不能代替数值分组。

零对：width/gap/pitch 空、count=0；一对：width一项、gap/pitch空。flat/no-edge 为 valid_roi=false、reason 明确，工具执行成功；非法 ROI/阈值/不支持图像为工具失败。ROI 几何越界导致支持窗口不足时将该 ROI 标为无有效测量，不裁成另一种测量区域。各值乘 units_per_pixel 输出物理长度，图像中的端点仍是像素坐标。

## S-MET-02 参数与精度验证

circle 默认参考值：64卡尺、search_half_width=8 px、average_width=3 px、sigma=1、min_gradient=8、min_points=12、min_completeness=0.6、min_coverage_deg=180、max_rms=1 px。这些是起始配置，不是所有工件通用值。

row 默认 sigma=1、min_gradient=8、object_polarity=bright、width_min=1、width_max=1000000 px。宽度范围用于 accepted，不用于掩盖原始配对；测量方向由 ROI 决定。所有数量/宽度有有限有效范围，输入异常在采样分配前被拒绝。

合成验收使用高分辨率生成后面积降采样的圆/条纹，覆盖非整数中心/半径、旋转角、噪声、遮挡、越界、双边缘和低对比。建议无噪声标准样本中心/半径误差≤0.25 px，条纹宽度/间距误差≤0.25 px；适用采样范围写入测试夹具，失效条件也必须断言。真实样本另记录偏差、重复性、漏检与误检，不只展示漂亮结果图。
