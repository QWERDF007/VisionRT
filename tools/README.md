# 构建与发行工具

| 职责 | 唯一入口 |
| --- | --- |
| 构建环境与命令代理 | [build.cmd](build.cmd)、[CMake](../CMakeLists.txt) |
| 机器依赖清单 | [dependencies.json](dependencies.json) |
| CMake 依赖默认路径 | [ConfigDependencyDefaults](../cmake/ConfigDependencyDefaults.cmake) |
| 开发环境运行库链接 | [link_dependencies.py](link_dependencies.py) |
| 依赖路径与严格 JSON 读取 | [dependency_utils.py](dependency_utils.py) |
| 宿主发布、工具 ZIP 部署与校验 | [package_app.py](package_app.py) |
| 工具声明与包语义校验 | [Qt 校验器](visionrt_validate_tool.cpp)、[ToolPackage](../src/core/ToolPackage.cpp) |
| 部署事务与资源反例 | [test_package_app.py](../tests/test_package_app.py) |
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
python tools\package_app.py --deploy-tools-only --install-dir install\VisionRT --tools-package <VisionTools.zip> --tools-index <VisionTools.module_index.json> --validator build\bin\visionrt_validate_tool.exe
```

部署先在暂存包执行严格 JSON lint 和真实宿主校验，通过后才替换工具目录；不加载模型。依赖默认值只在清单维护，部署 Python 环境来自构建缓存的 `VISIONRT_PYTHON_HOME`。参数与目录保护规则以脚本为准。

定向验证：

```cmd
tools\build.cmd ctest --test-dir build --output-on-failure -R package_app_contract
tools\build.cmd ctest --test-dir build --output-on-failure -R dependency_manifest_contract
```

## 原生通知探针

Windows 安装 Node.js 与 Windows SDK Debugging Tools，并构建 `ui_notifications_contract`、`viewport_lifetime_contract` 及其匹配 PDB 后，运行 `node tools/verify_ui_notifications.cjs <build目录> <cdb.exe> <ctest.exe> [证据目录]`。测试环境与命令取自 CTest；断言及证据格式见[探针源码](verify_ui_notifications.cjs)，场景见[静默通知契约](../tests/ui_notifications_contract.cpp)与[视口析构契约](../tests/viewport_lifetime_contract.cpp)。
