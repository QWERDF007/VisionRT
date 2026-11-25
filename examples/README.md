# VisionRT 示例工程

通过软件的“文件 → 打开示例…”选择 `.vrtapp` 工程，修改后另存到用户目录。

| 综合示例 | 使用说明 |
| --- | --- |
| [多分支视觉检测](综合示例/多分支视觉检测/多分支视觉检测.vrtapp) | [说明](综合示例/多分支视觉检测/README.md) |
| [识别线缆颜色](综合示例/识别线缆颜色/识别线缆颜色.vrtapp) | [说明](综合示例/识别线缆颜色/README.md) |
| [变距尺子标定测量](综合示例/变距尺子标定测量/变距尺子标定测量.vrtapp) | [说明](综合示例/变距尺子标定测量/README.md)，需要含 NumPy 的 Python 环境 |

原图统一存放于 [images](images/)，各工程的训练资源包（`.zip`，含模型与训练原图）及描述文件存放于同目录的 `assets`，相对路径均基于安装目录解析。ZIP 容器与训练资源包实现见 [ProjectArchive](../src/core/include/core/ProjectArchive.h) 与 [TrainingArchive](../3rdparty/VisionRTContracts/include/visionrt/contracts/TrainingArchive.h)。

验证入口：[工程加载和运行](../tests/example_pipelines.cpp)、[标定训练与测量](../tests/calibration_example_contract.cpp)、[元数据](../tests/example_metadata_contract.cpp)、[另存](../tests/example_project_contract.cpp)、[打包](../tools/package_app.py)。
