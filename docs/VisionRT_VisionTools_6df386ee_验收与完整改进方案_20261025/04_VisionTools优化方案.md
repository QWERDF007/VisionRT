# VisionTools 改进方案

## 1. 工具总体结构保留

工具继承 IToolInstance，initialize/execute/release 明确；ToolFactory 模板统一创建和 provider 销毁；模块手写注册；InputPorts 提供小型取参和范围校验。该结构已满足抽象、继承、多态和模板复用，不需要再加 OperatorBase→ImageOperator→BatchOperator 层级，也不需要 JSON 生成器。

工具 JSON 与实现通过现有宿主校验器、契约测试校对端口；无需在执行时读取声明来“反射”输入输出。

## 2. YOLO：先纠正 padding 边界

最小修复在 postprocess 内筛掉映射原图后面积为零的候选，再参与 selected/NMS/maximum。保留原来 raw candidates trace、类别隔离、分数排序与 candidateIndex 平分规则。普通有效候选继续用网络坐标 IoU，避免顺便改动全部边界框抑制结果。

新回归覆盖：padding 名额占用、padding 抑制有效框、部分超出边界但仍有面积、全部无效、maximum=1/较大值、不同类别、相同分数确定性、非方形原图。两例复现代码已附，建议纳入现有纯后处理测试入口，CPU 即可跑，不要求每次拉起 CUDA。

## 3. YOLO 性能：已有与可选

已实现的 State 资源复用包括 model、session、stream、hostInput、deviceInput、hostOutputs、deviceOutputs、planes、views。初始化不应每轮重跑，release 等待 stream 完成再释放资源，这些应保留。

当前单张顺序：颜色转换 → InferRT preprocessWithGeometry → split 到 pinned 输入 → H2D → executeSession → D2H → stream 同步 → CPU 后处理。每张同步保证共享输入/输出缓冲可复用，并保证函数返回前结果有效。直接删除同步会破坏所有权与执行次序。

优先测量 CPU 预处理、H2D/推理/D2H、后处理的真实占比。GPU 段需要事件/完成点准确测量，CPU enqueue 时间不能称推理耗时。工具内部指标未正式接入接口时保持宿主预/后处理显示 —。

可选优化：

1. 当前每类别每网格都做 sigmoid。数学上 sigmoid 单调，可先找最大 logit 再算一次；但 float 饱和/舍入会改变原实现的平分选类行为，因此必须明确 tie 规则、构建极端 logits 用例并与参考结果比对，不能直接宣称逐位等价。
2. selected/candidates/kept 分配只在测得突出时采用工具私有 scratch；executeWithTrace 的数据需要独立归属，不得把旧 trace 指向会复写的 scratch。
3. 真正需要多图吞吐时再做双缓冲和重叠流水线；必须独立 in-flight stream/buffer/event 生命周期，并在返回前完成。当前没有性能证据要求新增此复杂度。
4. GPU 预处理/NMS 或推理批量化是后续算法工程，受实际模型 batch=1 约束；不能仅把 Image 列表长度当成可直接增大的 TensorRT batch。

## 4. CPU 工具的具体候选

| 位置 | 当前成本/语义 | 可选改法 | 必须保留 |
|---|---|---|---|
| ReadImageTool | 收集候选、排序、复制到 files；逐文件压缩字节 buffer | 排序后 resize/move 复用同一列表；本次调用内复用解码输入容量 | UTF-8 路径、排序、数量/字节预算、失败 Missing；不缓存过期文件内容 |
| ImagePreprocess / RoiProjection | 重复 U8/通道检查和转灰度分支 | 一个内部图像检查/转灰度小 helper，区分 view 与 clone 的调用意图 | identity 透传、gray 单通道显式 clone 的现有存储策略 |
| ImageCrop | 旋转/分数 ROI 对每像素做多边形判断；已有本轮 mask 复用 | 先测 mask 开销；考虑扫描线或更完整的轴对齐快路径 | 像素中心包含规则、小数/负坐标、角度边界；cv::fillPoly 不保证与现规则等价 |
| RoiProjection | 每像素检查阈值和横纵分支 | 把方向分支移到外层；比较向量化/行计数与阈值图+reduce | 原阈值比较、gap 合并、坐标/ROI 顺序；额外二值图占用需计入 |
| DetectionFilter | 对每个检测线性 find allowed_classes | 小集合继续线性，大集合在本次执行内排序去重后二分或哈希 | 输出顺序和类型、scores/classes/ROI 对齐、空集合表示全允许 |
| AdaptiveBinary | 空 Image 列表提前成功，已有测试明确 | 仅补文档说明；若未来统一策略必须单独修改契约 | 本轮不要把约定行为当错误修改 |

矩形 90/180 度是否走无掩码视图路径必须先证明像素中心覆盖等价。裁剪浅视图保留原 owner；旋转掩码产生新输出是工具主动行为，不属于违反零拷贝桥接。

## 5. Python 作者示例

image_add 示例当前逐张校验并立即修改；若第一张 uint8 已改、第二张 float32 触发错误，则前者仍被改。核心协议未承诺事务回滚，因此不作为宿主缺陷；作为作者示例建议先校验所有元素，再执行修改，减少失败时的意外部分写入。不要要求宿主自动备份来保证回滚。

`np.minimum(image,254)+1` 在 inplace 模式也会产生临时像素数组。“原地输出”不等于“算法内部零临时分配”。可在工具内尝试两次 ufunc `out=image` 实现饱和加一，验证 0/254/255、非连续可表示视图、别名和错误行为；只有实测收益明显再改，不能把该微优化变成 Contracts 的通用写策略。

image_mean、image_sizes、ROI 示例维持原生 list/ndarray 和 JSON entry。进一步抽取共享 helper 只在多个工具确实重复且发布路径清晰时进行，不为几个示例加大型 Python 框架。

## 6. 打包和配置

保持包声明校验、builtin/custom 根隔离、显式资源清单和当前暂存提交机制。不能用 --allow-missing-dependencies 或 --skip-verify 的开发便捷模式替代正式验收。

两仓库 CPU/infer presets 当前共用 build 目录。可改为不同 binaryDir，并同步 build.cmd、打包解析路径和文档；这减少切配置时缓存/残留文件困扰，但现有包清单和校验仍是必要防线。不能仅凭共用 build 就声称现有安装包已经污染。

不新增哈希数据库、插件热更新管理器或跨运行共享输出池。输出池若复写仍被下游/显示持有的旧缓冲，会破坏可写共享数据的时间一致性；只可复用工具本次调用内 scratch，或在工具自己能够证明无持有者和同步安全时扩展。
