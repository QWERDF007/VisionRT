# 智能点选 Specs

## S-ASSIST-01 职责和实例

正式测量工具的 JSON 声明交互映射，VisionRT 只负责取稳定输入、坐标换算、任务调度、展示和参数事务。VisionTools 提供隐藏的 `visiontools.assist.circle` 与 `visiontools.assist.row_spacing`，复用相同 C++ 工厂接口和测量内核，不为 UI 另开私有 ABI。

辅助实例独立于正式实例，不能在悬停时调用正式节点 execute、修改其 initialize 配置或重新读取图像源。辅助工具接收长度1的 Image（Mat 头加 Owner）、cursor、phase 和 seed_rois，返回 preview_rois、proposal_rois 及少量固定类型推荐参数；不返回无法校验的任意 JSON 字典。

RT 知道源 image_index，但 helper 的 ROI 外层按长度1处理；提交时映射回原 ROI[image_index]，其他图像的 ROI 保留。辅助参数只在声明 interaction 映射中出现，正式工具不读声明。

光标位于当前图像坐标，种子 ROI 位于参考坐标。RT 同时选取该图的 alignment（广播矩阵也归一为长度1）传给 helper；几何转换由 VisionTools 处理。helper 的 preview_rois 在当前图像坐标，proposal_rois 经 alignment 逆变换回参考坐标，避免旋转后点击提交造成二次变换。对非相似矩阵按正式测量规则拒绝。推荐搜索宽度等标量依正式工具约定使用当前像素单位，不随逆变换重复缩放。

## S-ASSIST-02 鼠标交互

悬停更新临时候选轮廓；候选不写工程、不 dirty、不发布正式输出。普通点击替换当前图像中选中的 ROI；没有选择时替换该图 ROI 组为一个候选。Ctrl+点击追加；统一采用 Ctrl，不在不同工具间混用 Shift。Esc 取消；拖动超过 Qt startDragDistance 作为平移/编辑，不触发学习。

点击后的 learn 请求必须在同一输入 publication 上完成，返回有效 proposal 后一次事务提交 ROI 及推荐参数。无候选、参数非法、输入结果过期、ROI 端口当前来自上游时不修改工程，提示“未找到有效目标”“结果已更新，请重新选择”或“该参数由上游提供”。不能默默断开上游绑定。

Ctrl 追加时保持当前全局参数，候选不兼容现有极性/宽度等配置则拒绝并提示；不能为了新 ROI 改坏原有 ROI。普通替换可应用推荐参数。默认提交后把原正式结果标记过期，用户单次运行确认；不自动运行全流程、不自动推进文件序列。辅助 learn 可给当前候选精测预览，但不冒充正式节点结果。

## S-ASSIST-03 算法

### 圆

以光标为种子，在有界邻域提取梯度/轮廓，利用 OpenCV findContours 和初步圆形几何筛选产生候选；允许按图像金字塔扩大有效搜索范围。初选按光标距圆边界/内部关系、边缘支持和残差排序，选出名义圆，再用同一径向卡尺及 CircleFit 精化。圆弧不足或不稳定时返回无候选；不以 minEnclosingCircle 的包围圆冒充最终测量。

大圆不能靠固定小窗口可靠识别：采用有界多尺度搜索，达到最大范围仍缺足够证据就提示手动绘圆。learn 回原分辨率复测，推荐搜索宽度/极性来自实测边缘，不声称自动学习任意复杂背景。

### 行间距

光标邻域灰度梯度结构张量估计主法向，选择与光标邻近的候选边缘对；估计扫描方向、条纹宽度及切向可平均长度，组成旋转矩形。方向不明确或多方向交叉时返回无候选。使用 RowSpacing 内核得到配对与质量，learn 扩展到用户期望的局部行区域并输出 proposal ROI、极性及宽度范围。

种子只用于候选搜索，不需要神经网络或训练文件。两种辅助工具的搜索范围、金字塔层数和最大采样点数有明确上限；正式测量不继承点选临时缓存。

## S-ASSIST-04 并发和上下文

请求上下文包含 projectEpoch、generation、nodeId、executionRevision、inputPublication、imageIndex、requestSeq。只能在 Runtime Idle 且输入 publication 稳定时启动。DataRef 保活不保证不被写，因此 RT 内部读者租约必须阻止 Start 或同步修改越过正在执行的预览。

一个在途请求 + 一个最新待处理请求；新悬停替换 pending，结果回来后先比较完整上下文，再选择展示或丢弃。建议悬停30–50ms合并、局部预览 p95≤80ms 作为目标；不能使用无限队列，也不能为每次鼠标移动创建线程或工具实例。

协议：管理请求先作废 epoch、清空 pending、停止接纳读者；等待在途 helper 返回并释放 Owner；然后推进 runtime start/reset/open/close。GUI 事件循环始终可运行。helper 算法有界，线程不强杀；取消后允许算法完成但禁止旧结果落 UI。Loop 禁止启动辅助任务，不对上一轮冻结显示图进行当前数据计算。

关闭 reset 正式节点之外，还要 release 辅助实例，清除 candidate、selected image、mailbox 和所有临时 Owner；这些都是内部引用，必须在 AwaitOwners 前归还。只有外部保存的 DataRef/Mat 可以使系统停在 AwaitOwners。

## S-ASSIST-05 JSON 映射

interaction 的 input_map 把 images 映射为当前选中图像，rois 映射为对应种子组，alignment 映射为对应矩阵；cursor、phase 由 RT 填写。proposal_map 仅允许写本节点声明存在且当前为 Literal 的输入端口。映射类型必须与 helper 输出类型兼容。

正式圆工具映射 helper 的 proposal_rois→rois、suggested_search_half_width→search_half_width、suggested_polarity→polarity；行间距映射 proposal_rois→rois、suggested_polarity→object_polarity、suggested_width_min/max→width_min/max。preview_rois 只显示为瞬态 overlay，不进入工具业务输出。

不在本期为所有工具创建通用 AI 交互框架。旋转和亮度使用既有参数面板及单次运行预览；智能点选只开放给确有种子学习意义的圆和行间距。
