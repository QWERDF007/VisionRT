# VisionRT 可复现应用示例工程 (RT-06)

本目录提供开箱即用、可真实执行并稳定复现的工作流工程文件（`.vrt`）及样例资源。

## 目录结构

- `images/`: 测试图像资源目录
  - `sample_0.png`: 100×100 测试图，包含两处高亮矩形区域，用于验证投影定位与多区域裁剪。
  - `sample_1.png`: 100×100 测试图，包含单处高亮矩形区域。
- `pipeline_cpu_python.vrt`: **CPU 与 Python 完整汇合链路**
  - 分支 A（CPU 处理链）：`visionrt.image.read` → `visionrt.roi.projection` → `visionrt.image.crop` → `visionrt.image.preprocess`
  - 分支 B（Python 统计链）：`visionrt.image.read` → `custom.image.mean`（统计每张图像像素均值，返回 dense float64 数组）
  - 特性：读图扇出多路并发；CPU 裁剪根据投影 ROI 提取子图；Python 桥接安全传递只读 ndarray。
- `pipeline_gpu_yolo.vrt`: **GPU 推理与缺陷过滤全链工程**
  - 链路：`visionrt.image.read` → `visionrt.dl.yolo` → `visionrt.detection.filter`
  - 特性：GPU YOLOv8n 目标检测输出 ragged 数组（ROI、类别、置信度得分、类别名）；后接缺陷过滤器按置信度阈值与类别过滤。
- `pipeline_error_diagnostic.vrt`: **节点四态与错误诊断工程 (RT-05)**
  - 节点 1 (`visionrt.image.read`): 正常读图（绿色成功态）。
  - 节点 2 (`visionrt.image.crop`): 故意不连接 `rois` 端口，复现缺失必需输入时的契约错误（返回错误码 -2，UI 标记“执行错误”并显示状态码与缺失端口）。
  - 节点 3 (`visionrt.dl.yolo`): 指定不存在的模型权重，复现模型加载失败（返回错误码 -3，UI 标记“执行错误”并显示加载错误详情）。
  - 节点 4 (`unavailable.custom.plugin`): 算子定义不存在，复现定义缺失（UI 顶部徽章显示“缺定义”，不可执行）。

## 运行与验证

1. 启动 Workbench 并打开工程：
   ```cmd
   VisionRT.exe --project examples/pipeline_cpu_python.vrt
   ```
2. 保存、关闭并重开工程，确认拓扑、端口绑定与参数完全一致。
3. 执行“单次运行”或“循环运行”，查看各节点执行时间分项、输出数据及视图预览。
