# VisionRT 示例工程

安装目录下的示例是可直接打开、可直接运行的工程：每套都有自己的 `.vrt.json`、`images/` 与说明，资源全部使用相对路径。

## 打开方式

- 软件内：文件 → 打开示例…，对话框直接落在本目录。
- 命令行：`VisionRT.exe --project examples/01_基础入门/01_图像读取/图像读取.vrt.json`

示例是只读的产品文件：修改后按 `Ctrl+S` 会要求另存到用户目录，另存时 `images/` 等素材随工程一起复制，源文件保持不变。

## 首批示例

| 示例 | 节点链 | 结果 |
| --- | --- | --- |
| [图像读取](01_基础入门/01_图像读取/README.md) | 图像读取 | 2 张图像的批次、索引与总数 |
| [ROI定位与裁剪](02_工具示例/01_ROI定位与裁剪/README.md) | 图像读取 → 投影ROI → 图像裁剪 | 1 个矩形 ROI、60×300 裁剪图 |
| [图像预处理](02_工具示例/02_图像预处理/README.md) | 图像读取 → 调整亮度 → 自适应二值化 | 提亮图、应用区域、二值图 |
| [圆测量](02_工具示例/03_圆测量/README.md) | 图像读取 → 测量圆 | 半径 60.0 px、圆心 (160,120) |
| [行间距测量](02_工具示例/04_行间距测量/README.md) | 图像读取 → 测量行间距 | 5 个亮条，宽 20.0 px、间隙 20.0 px |
| [多分支视觉检测](03_综合示例/01_多分支视觉检测/README.md) | 图像读取 → 投影ROI → 裁剪 → 二值化；图像读取 → 测量圆 | 两条分支各自的图像与半径 |

这六套只使用 CPU 原生工具，不依赖外部 Python、模型或相机。

## 研发型样例

目录根部的三个工程是研发期样例，不是首批交付：它们需要额外资源或预期失败，仅用于诊断与 GPU 验证。

| 工程 | 用途 |
| --- | --- |
| [CPU / Python](pipeline_cpu_python.vrt.json) | 读图后的两条并行分支：ROI 定位与裁剪二值化、Python 原图均值 |
| [GPU YOLO](pipeline_gpu_yolo.vrt.json) | 检测与缺陷过滤 |
| [错误诊断](pipeline_error_diagnostic.vrt.json) | 缺参、模型加载失败与缺工具占位 |

`pipeline_cpu_python.vrt.json` 的两条分支互不串联：节点 5 的上游是节点 1 的 `images`，取的是裁剪与二值化之前的原图；端口绑定以工程 JSON 为唯一来源，此处不重复连线细节。样例图像的原图均值（51.0、22.95）与裁剪二值化后的均值（95.625、191.25）不同，因此“均值取原图”是可被验证的语义。

## 验证入口

- [首批示例加载与执行](../tests/example_pipelines.cpp)：`testShippedExamples` 逐套加载、执行并校验上述数值，含中文与空格目录。
- [示例元数据契约](../tests/example_metadata_contract.cpp)：工程签名与工具声明一致、参数取值在声明选项内。
- [示例保存规则契约](../tests/example_project_contract.cpp)：示例识别、素材随行、冲突不覆盖、`Ctrl+S` 不写安装目录。

## GPU 资源准备

GPU 样例需要 Ultralytics 的 `bus.jpg` 与由 InferRT 导出入口生成的 COCO YOLOv8n `yolov8n.wts`。这些资源及生成的引擎不属于通用工具包，也不会自动下载。

```cmd
build\bin\gpu_tools_contract.exe <tools-package> examples\pipeline_gpu_yolo.vrt.json <fixture-directory>
```

安装态验证需隔离开发机依赖；开发环境通过不等于部署通过。
