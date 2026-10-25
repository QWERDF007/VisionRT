# 图像工具 Specs

端口完整定义见 [12_端口契约.md](12_端口契约.md)。以下定义行为与算法。

## S-IMG-01 从文件获取图像

扩展 `visionrt.image.read`。`mode=file/directory/list` 决定路径来源；`read_mode=all/sequence` 决定一次返回全部筛选结果还是游标所指的一张。默认 all 保持该工具当前的多图语义，用户明确选择 sequence 后启用逐次取图。

路径统一经现有 PathResolver 处理。相对路径基于已保存工程目录，未保存工程使用相对路径时报简短错误；不自动换成示例目录。list 模式使用 paths_in，保留用户顺序和重复项；directory 按明确规则过滤、排序，可递归，但不跟随目录符号链接形成递归环。目录排序使用相对路径的确定性比较（Windows 比较键忽略大小写，再以原路径打破平局），不依赖文件系统枚举顺序。

实例状态只有 CatalogKey、解析后的路径列表、nextIndex 和必要配置。CatalogKey 比较 mode/path/paths_in/recursive/extensions/limit，配置值变化才重建并从首张开始；不计算文件内容哈希、不缓存所有解码图。默认筛选 png/jpg/jpeg/bmp/tif/tiff；实际能力以安装 OpenCV 编解码器为准，无法解码时报错，不承诺 HALCON 格式。多页 TIFF 首版只读第一帧，另行多页采集属于新需求。

read_mode变化同样重置游标；color、end_policy、max_batch_bytes变化不重建目录也不推进游标。file/list不应用目录后缀筛选；directory的后缀比较忽略大小写、接受有无前导点，extensions=[]表示不按后缀过滤，遇非图像文件解码时明确失败。无法访问的目标目录不静默忽略。相同配置下文件被替换，后续execute重新读取像素，目录列表本身保持到Reset。

sequence 首次取 index=0，整次执行成功后推进游标；解码、字节限制或取消失败不推进。末尾行为 `end_policy=wrap/hold/error`：wrap 下一次回到 0；hold 重读最后一张；error 下一次返回“图像序列已结束”。输出 current_index 表示本次实际索引，end_reached 表示本次已取到目录末项；不会把“下次游标”当“当前索引”。all 模式 current_index=0，end_reached=true。

limit=0 为不限制数量；非零先截取 catalog 再确定 total_count。max_batch_bytes 保留当前内部 ID（显示改为“本次图像字节上限”），0 为不限；每张解码后累计实际字节，超过时整次失败且不推进，错误不发布部分图像。此限制是输出预算，不能保证解码器绝不瞬时分配大图；UI 不应把它描述成硬进程内存上限。

目录内容变化不会在每次 Loop 中重新扫描。首版使用现有“重置运行时”释放 catalog 后重新扫描；切换目录/列表自动重建。悬停、点击文件名、显示上一帧、参数查看不调用 execute，因此绝不推进游标。没有可用文件、路径为空、文件损坏均明确失败。

## S-IMG-02 旋转图像

支持 CV_8U、CV_16U、CV_16S、CV_32F、CV_64F，通道 1/3/4。不支持的深度报错，不隐式转 8 位。对 N 张图逐张计算，任何输入非法时不发布部分输出。

angle_deg 采用 VisionRT 的顺时针正角度。OpenCV getRotationMatrix2D 的正方向是逆时针，因此传 `-angle_deg`。中心缺省为 `(W-1)/2,(H-1)/2`；用户可指定长度 2 的 center。角度归一化用于数值稳定，但输出所用矩阵必须与实际采样完全一致。

keep 模式输出尺寸与输入相同。expand 模式把像素外边界四角 `(-.5,-.5), (W-.5,-.5), (W-.5,H-.5), (-.5,H-.5)` 经旋转矩阵变换，取包围范围；对接近整数的跨度使用数值容差后向上取整，平移最小边界到 -.5。最终矩阵 A 包含扩展平移，alignment_out=A*alignment_in。不能只用 abs(sin/cos) 的宽高公式忽略偏心旋转与平移。

interpolation=nearest/linear/cubic；border_value 是与图像通道一致的标量或通道数组，范围按 dtype 校验。恒定填充值属于边界策略，不是插值类型。最近邻 90° 使用确定性图样验证方向、尺寸、像素坐标与矩阵，包含非方形、单像素和偏心中心。

算法默认新建输出。angle 为整周且画布/几何等同可由工具自行返回输入 DataRef，契约不强制复制；宿主不替工具作选择。不能在每轮把新结果写回仍被上一轮 UI 持有的输出缓存。

## S-IMG-03 调整亮度

核心 `dst = gain * src + offset`。gain、offset 有限，gain≥0；支持与旋转相同的深度和通道。整数输出按目标类型饱和，浮点保留浮点范围，不统一裁成 0–255。使用 `convertTo` 或等价 OpenCV 实现，不能使用带绝对值语义的 convertScaleAbs 替代。

默认创建新输出并复制未选区域。BGRA 的 alpha 保持原值，选区内只调整 BGR；如未来开放 alpha 调整，应增加明确业务参数，不改变默认。先校验所有图、mask、ROI 和 alignment，再开始写输出。

有效区域 = 每图 ROI 并集 ∩ mask 非零区域 ∩ 图像边界。ROI 的点/线默认不作为面积选择，端口只接受 Rectangle/Circle/Polygon；其他 kind 参数错误。ROI 全局 [] 表示全图，空的逐图组表示该图不处理。mask 缺省或空 Image 表示无额外限制；非空外层必须为 N，每张 CV_8UC1 且尺寸匹配，不隐式广播。

ROI 先由 alignment 从参考坐标映射到当前坐标。矩形/多边形按像素中心测试，圆的一般仿射区域用逆映射圆内测试；mask 已在当前图像坐标，不重复变换。像素中心位于几何边界时纳入，容差统一在 GeometryTransform 中定义。

`emit_mask=false` 时 applied_mask=Missing，不为显示方便每次创建全白大图；为 true 时返回实际应用的 CV_8UC1 区域，像素为 0/255。输出图与输入几何相同，JSON 使用 same_geometry_as_input。gain=1、offset=0 的恒等情况允许工具直接透传图像，mask 输出仍须遵守请求。
