# VisionRT / VisionTools 交付验收

评估日期：2026-10-19。

## 结论与范围

**通用工具包交付链满足本次交付要求。** 验收覆盖普通构建、真实生产 ZIP、同源元数据、宿主部署与列表、领域 DLL / Python / 平台接收执行、所有权及离线完整包更换。

当前实际发行配置选择预处理领域、Python 工具和已有宿主接收能力。该选择不是 CPU/GPU 包协议，也不要求其他有效包具有相同工具组合。未选择的推理目标及缺少模型、硬件前提的推理执行不计为通过，见末节。

- [唯一规格](tool-package-delivery/spec.md)
- [工单](tool-package-delivery/README.md)
- [源码与测试入口](visiontools-bridge-architecture.md)
- [恢复工作记录](../WORKLOG.md)

## 交付物

| 交付物 | 位置及观察 |
| --- | --- |
| 工具 ZIP | `F:/tmp/VisionTools.zip`；正常构建/打包入口生成 |
| 外置索引 | `F:/tmp/module_index.yaml`；与 ZIP 内、宿主安装后的索引逐字节相同 |
| 当前发行配置 | `VisionTools-0.1.0`；6 个领域 C++ 工具共享一个 DLL，2 个 Python 类工具，14 个 host_binding 接收工具 |
| 缓存检查 | ZIP 不含 `__pycache__`、`.pyc`、`.pyo`；不复制未索引的开发样例目录 |
| 完整宿主发行目录 | `F:/tmp/VisionRT`；正常 EXE/DLL、Qt、构建所选 Python 及工具 ZIP 部署结果 |

构建目标和合同的唯一作者源在代码导航中；本表只记录当前验收产物，不是宿主工具名单。

## 缺口验收

| 项 | 当前可观察结果 | 证据入口 |
| --- | --- | --- |
| R1 通用协议 | 发行配置名不限制后端；模块来自实际选中目标及构建清单，纯 Python 包不强制携带原生模块或厂商 DLL | VisionTools `test_package`、`test_distribution` |
| R2 正常生产链 | 正常生成与打包纳入领域工具、Python 和平台声明；分类显式声明，必要非代码资源显式收集 | 生产 ZIP、生成/发行链测试 |
| R3 唯一发现路径 | 宿主和示例执行不再扫描工具源码目录；宿主重复基础算法测试实现已移除，领域算法测试加载真实 DLL | `tool_registry_contract`、`cpu_tools_contract`、`examples_contract` |
| R4 唯一作者入口 | 工作台不再暴露独立工具元数据写入口；安装声明只读，由工具库重新打包交付 | `workbench_contract` 对公开元对象的反向断言及实际 QML |
| R5 分类与来源 | 分类由 YAML 显式属性读取；移动声明不改变分类，安装声明属性不由 builtin/custom 目录名猜测 | SDK 包合同、注册与工作台测试 |
| R6 发行卫生 | 相邻代码与显式资源正确交付；缓存和无关日志不会随正常包进入 ZIP | 真 ZIP 检查、打包负例 |
| R7 SDK 独立消费 | OFF 核心安装消费不需要 Python；ON 安装包正确发现嵌入依赖，断言开启的外部消费者可转换图像及字符串数组，释放解释器后 C++ 结果仍有效 | SDK 两种配置的隔离安装测试 |

pybind11 ABI 相关编译定义由可选 SDK 目标统一传递；宿主部署的 Python 取自实际 CMake 构建结果，不再另选运行时。实现与边界以源码为准。

## 验证证据

| 验证 | 命令 / 环境 | 结果 |
| --- | --- | --- |
| 工具库真实交付入口 | VisionTools `tools/build.cmd --release --no-infer --package F:/tmp/VisionTools` | 构建与包运行验证成功；6 个领域工具实际执行 |
| 工具库回归 | VisionTools `tools/build.cmd ctest --test-dir build --output-on-failure` | 7/7 通过；本次设置 `VISIONRT_PYTHON_CONSUMER`、`VISIONRT_DEPLOY_SCRIPT` 启用宿主联调 |
| 纯 Python ZIP | `test_distribution` 调用正常索引生成函数、真实打包器、宿主 deploy_tools_package 和 python_contract 消费模式 | 删除临时作者源后仍能相对导入、读取显式数据并得到 42；modules 为空 |
| 宿主整体回归 | `ctest --test-dir build -C Release --output-on-failure` | 14/14 通过；工具输入来自最终 ZIP 部署结果 |
| SDK OFF | `ctest --test-dir build/sdk-delivery-off --output-on-failure` | 5/5 通过，包含真实安装后的纯 C++ 消费者 |
| SDK ON | `ctest --test-dir build/sdk-delivery-on --output-on-failure` | 5/5 通过，包含真实安装后的 PythonInterop 消费者 |
| SDK 缺依赖 | ON 配置显式禁用 pybind11 包发现 | 配置失败，不静默省略可选目标 |
| 部署负例与更换 | `python -B tools/package_app.py --test-verify` | 坏 ZIP、路径逃逸、缺文件、身份冲突、索引不一致及文件占用等检查通过 |
| 完整发行部署 | package_app.py 输入上述 ZIP 和外置索引；随后用 deploy-tools-only 完整更换同一发行目录的工具包 | 部署及 SDK 校验成功 |
| 隔离运行 | 中文/空格发行路径；PATH 仅 Windows 系统目录；Python 指向发行环境；测试程序只使用复制的工程/图片数据，无工具源码补偿 | 真实领域 DLL + Python、缺依赖失败/恢复、C++ + Python + 接收数值 25、保存重开与旧包文件清理全部通过 |
| 实际 EXE | 发行版 VisionRT.exe 在相同精简环境启动，退出码 0 | 工作台显示包内工具分类；stderr 为空，截图已检查 |

完整发行验证没有把测试 EXE/DLL 留在交付目录。现场截图和测试数据位于 `build/acceptance`；最终交付日志为 `build/tmp-producer-build-package.log`、`build/tmp-host-package.log`、`build/tmp-*-tests.log`、`build/tmp-installed-runtime.log` 与 `build/tmp-gui-*.log`。三处 SDK 工作区使用一致的交付源码。

## 未运行项

- 当前发行配置不包含 `visionrt.dl.yolo`，其模型文件负例明确记录 not_run；工程加载同时验证缺少该声明的诊断，没有伪造实现或从宿主源码补齐。
- InferRT / CUDA / TensorRT 的具体推理目标、真实模型与目标硬件执行未在本次配置验收。交付包含该目标的配置时须独立构建、打包并实测其运行前提，不能引用本次成功代替。
- 未进行长期现场压力和所有目标机器组合测试；这些不构成当前已验证通用包协议的后端限制。
