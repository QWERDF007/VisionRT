# 验证证据说明

test-results.json 保留本轮实际命令、输出、退出码和预期退出码。绝对目录是本次审核工作区，不是产品要求的安装路径。生产仓库未被修改，工具源码和编译出的可执行文件没有重复打入报告。

复现 YOLO 探针时，分别准备 VisionTools 的 b24307b7bcb687a3b8f9fe2269aff528b054de6f 与 aa92f9e578b7780b85fde71d4aee25cf88182f2d 源码。以当前目录保存的 yolo_regression.cpp 为驱动，编译链接对应版本的 src/infer/YoloPostprocess.cpp，并把 src/infer 加入头文件路径。例如在仓库根目录运行：

```bash
g++ -std=c++20 -Wall -Wextra -O2 -I src/infer /path/to/evidence/yolo_regression.cpp src/infer/YoloPostprocess.cpp -o /path/to/yolo_regression
/path/to/yolo_regression
```

最新版本预期退出 0；旧版本预期在 maximum=1 保留有效框的断言失败并退出 1。该负对照不属于最新代码的失败。驱动提取自最新 test_infer_contract.cpp 中的三个纯后处理测试函数。

Python 测试直接运行对应 VisionTools 仓库中的 tests/test_python_tools.py -v，使用安装有 NumPy 的解释器。DataRef 探针使用本次审核的 Contracts 依赖快照头文件，验证可写共享与保活，不代表独立验收 Contracts 最新仓库或完整 Python 图像桥接。

change_summary.json 列出两仓本次提交及变更路径；catalog_inventory.json 是扫描到的现有工具声明。文档归档变更也列在提交差异中，但产品验收判断来自源码、测试及实际执行证据。
