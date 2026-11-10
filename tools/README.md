# 构建与发行工具

| 职责 | 唯一入口 |
| --- | --- |
| 构建环境与命令代理 | [build.cmd](build.cmd)、[CMake](../CMakeLists.txt) |
| 机器依赖清单 | [dependencies.json](dependencies.json) |
| CMake 依赖默认路径 | [ConfigDependencyDefaults](../cmake/ConfigDependencyDefaults.cmake) |
| 开发环境运行库链接 | [link_dependencies.py](link_dependencies.py) |
| 依赖路径与严格 JSON 读取 | [dependency_utils.py](dependency_utils.py) |
| 宿主发布、工具 ZIP 部署与校验 | [package_app.py](package_app.py) |
| 发布证据汇总（提交三元组、环境报告、安装包证据） | [release_evidence.py](release_evidence.py) |
| 工具声明与包语义校验 | [Qt 校验器](visionrt_validate_tool.cpp)、[ToolPackage](../src/core/ToolPackage.cpp) |
| 部署事务与资源反例 | [test_package_app.py](../tests/test_package_app.py) |
| 发布证据审计规则 | [test_release_evidence.py](../tests/test_release_evidence.py) |
| 六个真实 Git+CMake 元数据构建（`-L integration`） | [test_build_metadata_integration.py](../tests/test_build_metadata_integration.py) |
| 跨 CMake/Python 依赖清单验证 | [dependency_manifest_contract.py](../tests/dependency_manifest_contract.py) |

## 常用入口

```cmd
tools\build.cmd cmake --build build --config Release
python tools\link_dependencies.py --build-dir build
python tools\package_app.py --help
```

工具包校验入口：

```cmd
build\bin\visionrt_validate_tool.exe --package <工具包目录>
build\bin\visionrt_validate_tool.exe --package <工具包目录> --load
```

阶段和可用状态以 [ToolRegistry](../src/core/ToolRegistry.h)、[PluginLoader](../src/plugins/PluginLoader.h) 和 [CLI](visionrt_validate_tool.cpp) 为准；真实模块验证见 [注册契约](../tests/module_registration_contract.cpp)。

将工具包部署到既有安装目录：

```cmd
python tools\package_app.py --deploy-tools-only --install-dir F:\tmp\VisionRT --tools-package <VisionTools.zip> --tools-index <VisionTools.module_index.json> --validator build\bin\visionrt_validate_tool.exe
```

部署先在暂存包执行严格 JSON lint 和真实宿主校验，通过后才替换工具目录；不加载模型。依赖默认值只在清单维护，部署 Python 环境来自构建缓存的 `VISIONRT_PYTHON_HOME`。参数与目录保护规则以脚本为准。

本机交付部署目录约定为 `F:\tmp\VisionRT`，不使用用户目录或系统盘；用户工具、偏好与日志都在软件目录内，`--force-clean` 只替换程序文件并保留 `custom-tools`、`logs`、`ui-preferences.json`。

打包验证同时读文件树和 PE 导入表：目录里不能出现 `python3*.dll`、`python.exe`、`python/Lib`、`python/DLLs`，从 `VisionRT.exe` 出发沿随包模块走一遍导入链，链上任何模块静态导入 `python3*.dll` 都会拒绝（桥接被显式加载，不在启动链上，它的导入会被记录而不是报错）。验证通过后写出 `build/evidence/package-evidence.json`（可用 `--evidence` 指定），记录导入链、桥是否存在、每个产品二进制的导入表与外带运行时扫描结果，供发布归档。

工具包必须是 schema 6：旧构建目录里的 `tools-package` 可能仍是 schema 5/4，宿主校验器会拒绝；当前可用来源是 `F:\Projects\VisionTools\dist\visiontools-deploy.zip` 与其 `.module_index.json`。

发布证据汇总（提交三元组 + 构建事实 + 安装包证据 + 各环境报告）：

```cmd
VisionRT.exe --python-report F:\tmp\evidence\unconfigured.json
VisionRT.exe --python-report F:\tmp\evidence\ready.json
python tools\release_evidence.py --package F:\tmp\VisionRT --report unconfigured=F:\tmp\evidence\unconfigured.json --report ready=F:\tmp\evidence\ready.json
```

脚本要求两仓 `3rdparty/VisionRTContracts` 指向同一提交、`ready` 场景真实加载的 `python3*.dll` 与 NumPy 都位于所选环境的 prefix/base 内、未配置进程没有任何 Python 模块、安装包启动链不导入 Python。缺任一场景或出现矛盾时退出码为 2 并不标记 `complete`；工作区有未提交改动时需显式加 `--allow-dirty`，记录会被标注为仅本地核对。产物默认写到 `build/evidence/release-evidence.json`。审计规则可单独运行：`python -m unittest tests.test_release_evidence`。

定向验证：

```cmd
tools\build.cmd ctest --test-dir build --output-on-failure -R package_app_contract
tools\build.cmd ctest --test-dir build --output-on-failure -R dependency_manifest_contract
```

## 原生通知探针

Windows 安装 Node.js 与 Windows SDK Debugging Tools，并构建 `ui_notifications_contract`、`viewport_lifetime_contract` 及其匹配 PDB 后，运行 `node tools/verify_ui_notifications.cjs <build目录> <cdb.exe> <ctest.exe> [证据目录]`。测试环境与命令取自 CTest；断言及证据格式见[探针源码](verify_ui_notifications.cjs)，场景见[静默通知契约](../tests/ui_notifications_contract.cpp)与[视口析构契约](../tests/viewport_lifetime_contract.cpp)。
