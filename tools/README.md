# VisionRT 运行时与打包工具说明

本目录提供 VisionRT 的本地运行依赖链接与独立发布包打包工具。

## 依赖清单 (`dependencies.yaml`)

`dependencies.yaml` 是外部依赖在项目中的单一真相源（SSOT）。
定义了 Qt6、OpenCV、Python 3.12、CUDA 11.8、InferRT、TensorRT、Faiss、MKL 等运行时库的根目录和过滤模式。

## 1. 项目构建与 MSVC 环境脚本 (`build.cmd`)

用于自动检测并初始化 MSVC x64 开发编译环境，一键执行 CMake 配置、构建、测试与打包。

### 常用命令

默认 Release 构建：
```cmd
tools\build.cmd
```

构建并运行全量契约测试：
```cmd
tools\build.cmd --test
```

清理构建：
```cmd
tools\build.cmd --clean
```

构建并打包发布：
```cmd
tools\build.cmd --package F:\tmp\VisionRT
```

透传执行特定命令（环境代理模式）：
```cmd
tools\build.cmd cmake --build build --config Release
tools\build.cmd ctest --test-dir build --output-on-failure
tools\build.cmd python tools\package_app.py --install-dir F:\tmp\VisionRT
```

---

## 2. 独立发布包打包 (`package_app.py`)

用于生成独立、便携的 Release 发布包。
自动收集应用程序可执行文件、项目 DLL、QuickUI QML 模块及插件、Python 3.12 嵌入式环境、第三方依赖运行库、MSVC 运行库以及运行 `windeployqt` 部署 Qt 运行时。

### 常用命令

打包到指定输出目录（例如 `F:\tmp\VisionRT`）：
```powershell
python tools\package_app.py --install-dir F:\tmp\VisionRT
```

若指定 `--install-dir F:\tmp`，脚本会自动保护并打包到 `F:\tmp\VisionRT` 子目录，防止意外影响 `F:\tmp` 中的其它已有文件。

打包前重新编译：
```powershell
python tools\package_app.py --build --install-dir F:\tmp\VisionRT
```

常用参数说明：
| 参数 | 说明 |
|------|------|
| `--build-dir` | CMake 构建目录，默认 `build` |
| `--install-dir` | 发布包输出路径，默认 `install/VisionRT-<version>` |
| `--config` | 构建模式，默认 `release` |
| `--dependencies` | 依赖清单路径，默认 `tools/dependencies.yaml` |
| `--windeployqt` | 显式指定 `windeployqt.exe` 路径 |
| `--skip-windeployqt` | 跳过 Qt 部署 |
| `--skip-dependencies` | 跳过清单中的第三方依赖 |
| `--skip-system-libs` | 跳过 MSVC / Windows SDK 运行库 |
| `--include-pdb` | 包含 PDB 调试符号 |
| `--build` | 打包前执行 `cmake --build` |
| `--parallel` | `--build` 并行线程数，默认 4 |
| `--skip-verify` | 跳过发布包完整性校验 |
| `--no-clean` | 不清空输出目录，直接增量复用 |
| `--force-clean` | 即使输出目录缺少 `.visionrt_package` marker 也强制清理 |

---

## 3. 开发环境依赖链接 (`link_dependencies.py`)

解决“无法直接启动 `build/bin/VisionRT.exe`”的问题。
在开发调试阶段，将 Qt 基础 DLL、platform 插件、QuickUI 模块与第三方运行库直接链接到 `build/bin`，并生成 `qt.conf`，使开发者可以直接在终端或双击运行 `build/bin/VisionRT.exe`，无需设置全局 PATH 环境变量。

### 常用命令

```powershell
python tools\link_dependencies.py
```
