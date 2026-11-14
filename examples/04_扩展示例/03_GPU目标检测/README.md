# GPU目标检测

- 前置条件：本示例**不是免配置示例**。需要支持 CUDA 的显卡、与安装包匹配的 GPU 运行环境，并自备 Ultralytics 的 `bus.jpg` 与 COCO YOLOv8n 权重 `yolov8n.wts`（由 InferRT 导出入口生成）。
- 资源放置：把 `bus.jpg` 放到本目录 `images/bus.jpg`，把 `yolov8n.wts` 放到本目录 `models/`；YOLO 节点的权重与引擎路径就是相对本工程的 `models/yolov8n.wts` 与 `models/yolov8n.engine`。
- 使用：文件 → 打开示例… → 本目录的 `GPU目标检测.vrt.json`。
- 流程：图像读取 → YOLO推理 → 结果过滤。
- 期望结果：首次运行先构建引擎（耗时较长），随后给出检测框、类别、置信度，过滤节点按类别与分数保留目标。
- 模型缺失时节点会明确报出路径不可用，不会被当作可运行示例。
