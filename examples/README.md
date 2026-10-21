# VisionRT 可复现应用示例工程 (RT-06)

本目录提供开箱即用、可真实执行并稳定复现的工作流工程文件（`.vrt`）及样例资源。

## 目录结构

- `images/`: 测试图像资源目录
  - `sample_0.png`: 100×100 测试图，包含两处高亮矩形区域。
  - `sample_1.png`: 100×100 测试图，包含单处高亮矩形区域。
- `pipeline_cpu_python.vrt`: **CPU 与 Python 完整汇合链路**
  - 分支 A（图像处理链）：`visionrt.image.read` → `visionrt.image.crop` → `visionrt.image.preprocess`；读取同时连接 `visionrt.roi.projection`，独立展示投影区域。
  - 分支 B（Python 统计链）：`visionrt.image.read` → `custom.image.mean`（统计每张图像像素均值，返回 dense float64 数组）
  - 特性：读图扇出多路并发；[工程声明](pipeline_cpu_python.vrt)定义一个 Rect 应用于整批图像；Python 桥接传递只读 ndarray。
- `pipeline_gpu_yolo.vrt`: **GPU 推理与缺陷过滤全链工程**
  - 链路：`visionrt.image.read` → `visionrt.dl.yolo` → `visionrt.detection.filter`
  - 特性：GPU YOLOv8n 目标检测输出 ragged 数组（ROI、类别、置信度得分、类别名）；后接缺陷过滤器按置信度阈值与类别过滤。
  - 模型准备复用 [InferRT 检测权重导出入口](F:/Projects/InferRT/samples/model/python/detection_gen_wts.py)；输入／引擎输出位置见[工程声明](pipeline_gpu_yolo.vrt)，加载模式及可选项以已安装工具元数据为准。模型与 GPU 引擎不是通用工具包的必备文件。
- `pipeline_error_diagnostic.vrt`: **节点四态与错误诊断工程 (RT-05)**
  - 节点 1 (`visionrt.image.read`): 正常读图（绿色成功态）。
  - 节点 2 (`visionrt.image.crop`): 故意保留 `rect` 为 Missing，复现缺失必需输入时的契约错误。
  - 节点 3 (`visionrt.dl.yolo`): 指定不存在的模型引擎，复现模型加载失败；UI 标记“执行错误”并显示初始化错误详情。
  - 节点 4 (`unavailable.custom.plugin`): 算子定义不存在，复现定义缺失（UI 顶部徽章显示“缺定义”，不可执行）。

## 运行与验证

1. 启动 Workbench 并打开工程：
   ```cmd
   VisionRT.exe --project examples/pipeline_cpu_python.vrt
   ```
2. 保存、关闭并重开工程，确认拓扑、端口绑定与参数完全一致。
3. 执行“单次运行”或“循环运行”，查看各节点执行时间分项、输出数据及视图预览。
4. 运行安装态联合集成契约测试：
   ```cmd
   build\bin\examples_contract.exe --tools-package <path/to/VisionTools_package>
   ```

## 本地 GPU 资源验证

`gpu_tools_contract` 使用真实包、`pipeline_gpu_yolo.vrt` 和本地模型资源执行读取、检测及下游过滤，不自动下载模型。资源目录放置上述导出入口生成的 COCO YOLOv8n `yolov8n.wts`，以及 Ultralytics 的 `bus.jpg`；预期类别、框与置信度在[契约入口](../tests/gpu_tools_contract.cpp)中以独立 PyTorch 推理结果固定。

```cmd
build\bin\gpu_tools_contract.exe <tools-package> examples\pipeline_gpu_yolo.vrt <fixture-directory>
```

默认验证权重构建和所生成引擎的重新加载；已有同一模型的 `yolov8n.engine` 时，可显式追加 `--engine-only` 只验证引擎加载与真实推理。该选项不把权重构建超时记为通过，也不改变工程的精度参数。安装态验证须使用隔离依赖环境，不能将开发环境运行通过当作部署通过；模型、引擎和验证资源不成为通用工具包必备内容。

该功能契约为权重构建和引擎加载／执行分别设置完成期限，具体值见[契约入口](../tests/gpu_tools_contract.cpp)；日志记录实际耗时，验收须说明是否使用已有缓存。该期限是测试判定，不是产品性能指标，也不保证可强制中断原生构建。构建与推理的不同职责见 NVIDIA TensorRT 的 `https://docs.nvidia.com/deeplearning/tensorrt/latest/inference-library/capabilities.html`；不为缩短验证时间改变精度或降低构建优化等级。
