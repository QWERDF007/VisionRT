# VisionRT 平台架构规范与工程索引

## 1. 项目定位与核心原则

VisionRT 是基于 C++20 与 Qt 6.6 Quick 构建的现代化工业视觉低代码运行平台。平台遵循以下工程设计原则：
- **单一真相源（SSOT）**：算子元数据 YAML 与统一 SDK 是系统行为与 UI 呈现的唯一契约源，写入口唯一，其他皆派生、复用。
- **深度解耦与面向维护**：宿主平台核心不嵌入特定算法厂商的深度学习推理后端（如 TensorRT/CUDA 私有实现），所有图像处理与深度学习算法均由独立的算子库（`VisionTools`）或标准动态插件提供。
- **受控 C++ ABI 与数据模型**：模块间边界严格通过 `visionrt::ModuleApi` C-ABI 抽象，算子实现继承 `IToolInstance` 纯虚接口；图像（OpenCV `cv::Mat`）与多维数值张量在 DAG 数据流按边共享指针，实现只读零拷贝；Python 桥接在输入端以只读 NumPy 视图包裹底层内存（零拷贝），在输出端于叶子节点执行单次内存规整拷贝。
- **严谨的错误诊断与四态规范**：未运行、运行中、成功、错误四态与选中态彻底解耦，错误路径保留已发生阶段耗时与原始状态码。

---

## 2. 核心模块与架构布局

```
VisionRT/
├── src/
│   ├── sdk/                 # 统一算子开发 SDK（单一真相源，导出为 VisionRT::ToolSDK）
│   │   ├── include/visionrt/sdk/Tool.h      # 核心值类型与 IToolInstance 纯虚接口
│   │   ├── include/visionrt/sdk/Module.h    # 动态库导出协议 ModuleApi 与 visiontoolsModuleApi
│   │   └── include/visionrt/sdk/Schema.h    # 统一元数据校验规则与 CLI 验证器
│   ├── core/                # 平台核心对象（ToolRegistry, ProjectDefinition, ProjectStorage）
│   ├── runtime/             # 基于 Taskflow 的拓扑 DAG 并行调度与快照发布引擎
│   ├── plugins/             # 基于 QLibrary 的动态插件加载与生命周期管理（PluginLoader）
│   ├── python/              # pybind11 单解释器嵌入、GIL 作用域与 NumPy 零拷贝桥接
│   ├── tools/               # 宿主内置基础工具（图像读取、ROI 投影、裁剪等）
│   └── ui/                  # Qt Quick / QML 工作台交互、四态画布、视口与参数表
├── examples/                # 可复现示例工程（临时副本运行，禁止源码树自我改写）
├── tests/                   # 15 个独立自动化契约测试套件
└── tools/                   # 构建、打包与运维工具（build.cmd, package_app.py）
```

---

## 3. 插件与算子扩展规范

### 3.1 C++ 动态插件扩展
外部工具库（如 `VisionTools`）输出独立的领域动态库（如 `visiontools_preprocess.dll`），通过统一模块接口导出：

```cpp
#include <visionrt/sdk/Module.h>
#include <cstring>

class MyCustomTool final : public visionrt::IToolInstance {
public:
    visionrt::ToolStatus initialize(const visionrt::Values& params) override { return {}; }
    visionrt::ToolResult execute(const visionrt::Values& inputs) override {
        // 算法业务逻辑...
        return {{0, "ok"}, {}};
    }
    visionrt::ToolStatus release() override { return {}; }
};

static visionrt::IToolInstance* VT_CALL pluginCreate(const char* registry_name,
                                                   const visionrt::ToolServices* services,
                                                   visionrt::ToolStatus* status) noexcept {
    if (std::strcmp(registry_name, "my.custom.tool") == 0) {
        if (status) *status = {};
        return new MyCustomTool();
    }
    if (status) *status = {-3, "Unknown registry name"};
    return nullptr;
}

static void VT_CALL pluginDestroy(visionrt::IToolInstance* instance) noexcept {
    delete instance;
}

static const visionrt::ModuleApi g_api{
    visionrt::MODULE_ABI_MAJOR,
    sizeof(visionrt::ModuleApi),
    pluginCreate,
    pluginDestroy
};

extern "C" VT_EXPORT const visionrt::ModuleApi* VT_CALL visiontoolsModuleApi() noexcept {
    return &g_api;
}
```

插件通过 `module_index.yaml` 索引进行注册，由宿主 `PluginLoader` 在启动或工具包安装时动态挂载，无需重新编译宿主。

### 3.2 Python 算子扩展
Python 脚本放置于工具包对应目录，实现 `ToolInstance` 类，包含 `initialize(params)` 与 `execute(inputs)` 方法。输入图像以只读 NumPy 视图传入（零拷贝），返回字典中的结果张量在叶子输出时完成单次格式规整。

---

## 4. 构建与测试说明

项目采用单一构建入口脚本 `tools/build.cmd`，自动初始化 MSVC x64 环境：

- **编译工程**：
  ```cmd
  tools\build.cmd --build
  ```
- **执行全量自动化契约测试（15 个测试套件，100% 通过）**：
  ```cmd
  tools\build.cmd --test
  ```
- **生成应用发布包**：
  ```cmd
  tools\build.cmd --package dist
  ```
- **执行发布包与工具包合并测试**：
  ```cmd
  python tools/package_app.py --test-verify
  ```

---

## 5. 文档与规范索引

- **SDK 接口合同与数据契约**：[`src/sdk/include/visionrt/sdk/Tool.h`](../src/sdk/include/visionrt/sdk/Tool.h)
- **节点错误诊断与四态视觉记录**：[`RT-05_节点错误诊断记录.md`](RT-05_节点错误诊断记录.md)
- **VisionRT 与 VisionTools 桥接架构**：[`visiontools-bridge-architecture.md`](visiontools-bridge-architecture.md)
- **重构交付与验收矩阵报告**：[`RT-15_重构交付与验收矩阵.md`](RT-15_重构交付与验收矩阵.md)
