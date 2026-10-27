# VisionRT 工程样例

| 工程 | 用途 |
| --- | --- |
| [CPU / Python](pipeline_cpu_python.vrt.json) | 图像处理与 Python 统计 |
| [GPU YOLO](pipeline_gpu_yolo.vrt.json) | 检测与缺陷过滤 |
| [错误诊断](pipeline_error_diagnostic.vrt.json) | 缺参、模型加载失败与缺工具占位 |

节点、参数、资源路径和连线以工程 JSON 为准；图像资源位于 [images](images)。工程需要提供对应工具定义和实现的 VisionTools 包。

## 打开工程

```cmd
VisionRT.exe --project examples/pipeline_cpu_python.vrt.json
```

## 验证入口

- [存取契约](../tests/project_storage_contract.cpp)：工程候选、身份、占位与另存为。
- [样例执行契约](../tests/examples_contract.cpp)、[测试注册](../CMakeLists.txt)：真实工具包、Python、保存重开和错误路径。
- [元数据契约](../tests/example_metadata_contract.cpp)：样例初始化/输入选项、保存签名与工具声明的一致性。
- [GPU 契约及参数](../tests/gpu_tools_contract.cpp)：权重构建、引擎重载和独立推理参考。

## GPU 资源准备

资源目录需提供 Ultralytics 的 `bus.jpg` 和由 [InferRT 导出入口](F:/Projects/InferRT/samples/model/python/detection_gen_wts.py)生成的 COCO YOLOv8n `yolov8n.wts`。这些资源及生成的引擎不属于通用工具包，也不会自动下载。

```cmd
build\bin\gpu_tools_contract.exe <tools-package> examples\pipeline_gpu_yolo.vrt.json <fixture-directory>
```

使用已有同模型引擎的入口与完成期限以 GPU 契约为准。安装态验证需隔离开发机依赖；开发环境通过不等于部署通过。
