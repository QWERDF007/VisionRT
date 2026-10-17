# VisionRT 与 VisionTools 算子库桥接方案设计

## 一、 背景与设计目标

### 1.1 业务背景
在工业视觉低代码平台体系中，主框架（`VisionRT`）与算法工具库（`VisionTools`）应当遵循深度解耦、面向长期维护的架构原则。
本方案基于对商业视觉系统 `TimesAI2.exe` 与其工具库 `timesai_tools` 的深度逆向工程分析，吸取其插件化与多语言协作的优点，全面革除其技术债务（如 STL 跨 DLL 边界导致的 ABI 脆弱性、魔术数字强耦合、巨量小动态库碎片化等），为 `VisionRT` 与 `VisionTools` 建立一套规范、强类型、高性能且跨平台一致的现代化桥接体系。

### 1.2 核心目标
1. **架构对称性**：`VisionTools` 仓库目录架构完全对齐 `VisionRT` 标准工业级工程布局（`3rdparty`、`cmake`、`docs`、`src`、`tests`、`tools`）；
2. **单一真相源（SSOT）**：算子元数据清单作为唯一契约源，UI 控件自适应呈现、DAG 连线拓扑校验、算子参数映射皆由元数据自动驱动；
3. **ABI 强稳定性**：DLL 边界绝不穿透复杂的 C++ STL 容器（严禁跨动态库传递裸 `std::any`、`std::string`、`std::map`、`std::vector`），采用纯虚接口（VTable C++ ABI）与上下文深封装；
4. **零拷贝高性能流转**：C++ 与 Python 算子均原生支持与 `VisionRT` 共享图像（OpenCV `cv::Mat`）与数值张量（NumPy `ndarray`）缓冲区，杜绝内存深拷贝与冗余格式转换；
5. **领域级模块化**：按算法领域划分子工程与动态库（如 `visiontools_preprocess.dll`、`visiontools_measure.dll`），避免 200+ 碎片 DLL 的系统调用灾难，兼顾增量编译效率与包管理整洁度。

---

## 二、 TimesAI 桥接机制剖析与技术反思

### 2.1 逆向发现与实现机制
- **元数据层**：位于 `visionCode/json/basis/<分类>/<算子名>.json`，明文或 AES 加密，声明算子中英文名、输入输出参数列表及 UI 控件类型；
- **C++ 算子层**：细粒度编译为数百个形如 `libtools/tools/tool_<name>.dll` 的独立动态库。导出统一的 `extern "C" int item_register(...)`，通过标准函数签名 `int (*fp)(std::map<int, std::any>&, std::map<int, std::any>&)` 提供算法逻辑，通过全局宏 `REGISTER_FUNCTION` 注入平台注册表；
- **Python 算子层**：通过 `TimesAICore_PyBridge.dll` 提供统一隔离，在运行时调用 Python 算子类的 `ModelObj.process(cpp_tuple)`，自动获取/释放 GIL，以字典格式回填 C++ 输出参数；
- **主平台调度层**：`TimesAI2.exe` 通过 `TimesAIApplicationCore.dll` 与 `ToolRegister.dll` 扫描 JSON 并加载 DLL，按工作流 DAG 进行数据传递与调度。

### 2.2 核心缺陷与技术债务
1. **ABI 极度脆弱**：`std::any`、`std::map<int, std::any>` 和 `std::string` 穿透 DLL 边界，只要主程序与工具库的编译器小版本、MSVC CRT 运行库或优化级别出现微小差异，就会发生严重的内存对齐崩溃或析构非法访问（导致其平台必须强制锁定 VS2017 与统一运行时环境）；
2. **缺乏编译期与静态契约校验**：算子内部全部依赖下标硬编码读取（如 `inputs.at(0)`）和盲目转型（`any_cast`），JSON 字段与 C++ 代码脱节时无法在编译期暴露，运行时极易触发越界或崩溃；
3. **Python 切片传参脆弱**：Python 端使用硬编码元组切片（`cpp_tuple[5: 5 + input_count]`），参数变更极难维护；
4. **DLL 碎片灾难**：200+ 个独立 DLL 导致部署包文件数达到数千个，启动时产生高昂的动态链接定位开销。

---

## 三、 VisionTools 仓库目录架构设计

`VisionTools` 与 `VisionRT` 保持完全统一的工程组织规范：

```
F:\Projects\VisionTools\
├── 3rdparty/                 # 第三方开发依赖 (yaml-cpp, pybind11, Eigen, faiss 等)
├── cmake/                    # 模块化 CMake 构建逻辑 (与 VisionRT 统一)
│   ├── ConfigCompiler.cmake  # MSVC C++20 统一编译器配置 (/utf-8, /permissive-, /W4)
│   ├── ConfigVersion.cmake   # 语义化版本号管理
│   ├── ConfigOpenCV.cmake   # OpenCV 引入配置
│   ├── ConfigCUDA.cmake     # CUDA / cuDNN 配置
│   ├── ConfigInferRT.cmake  # GPU 深度学习推理后端
│   ├── ConfigPython.cmake   # Python 3.12 嵌入与 pybind11
│   ├── AddTest.cmake        # 契约测试自动化注册
│   └── PrintConfig.cmake    # 构建环境配置摘要
├── docs/                     # 架构文档、算子开发规范与开发手册
├── src/                      # 算子库核心源码
│   ├── sdk/                 # 算子 SDK 与跨 DLL 稳定 C-ABI 抽象层
│   │   ├── ToolContext.h    # 强类型数据输入输出上下文
│   │   ├── IToolInstance.h  # 算子生命周期纯虚接口
│   │   ├── ToolExport.h     # 动态库导出/导入宏
│   │   └── Types.h          # 跨模块通用几何基元与状态枚举
│   ├── preprocess/          # 预处理领域模块 (滤波、二值化、形态学、色彩空间)
│   ├── matching/            # 匹配领域模块 (ShapeMatch 形状匹配、灰度模板匹配)
│   ├── measure/             # 测量与几何 (亚像素找边/圆、拟合、几何计算、九点标定)
│   ├── barcode/             # 读码领域模块 (一维条码、二维码、DataMatrix)
│   ├── infer/               # 深度学习推理 (YOLO 检测/分割/分类/异常检测)
│   ├── flow/                # 流程控制算子 (跨流程数据接收/发送、变量缓存)
│   └── python/              # Python 算子共享基础设施与辅助桥接
├── tests/                    # TDD 契约测试集 (每个算子均有独立契约测试)
│   ├── test_preprocess_contract.cpp
│   ├── test_measure_contract.cpp
│   ├── test_matching_contract.cpp
│   └── test_python_contract.cpp
├── tools/                    # 工程运维与自动化脚本集 (与 VisionRT 统一)
│   ├── build.cmd            # 自动初始化 MSVC x64 环境并构建 (支持 --test/--clean)
│   ├── dependencies.yaml    # 依赖单一真相源 (Qt6, OpenCV, Python, CUDA)
│   ├── link_dependencies.py # 开发期快速软链接运行时依赖
│   ├── package_tools.py     # 算子库打包与导出至 VisionRT 自动化脚本
│   └── README.md            # 工具目录使用说明
├── assets/                   # 算子元数据定义 (单一真相源)
│   └── tools/
│       ├── 采集/
│       ├── 预处理/
│       ├── 处理/
│       ├── 深度学习/
│       └── 流程控制/
├── CMakeLists.txt            # 项目根构建文件
└── WORKLOG.md                # 任务唯一账本
```

---

## 四、 跨边界桥接与数据总线协议规范

### 4.1 领域级动态库（Domain DLL）组织
`VisionTools` 不采用单一大巨石 DLL，也不采用 200+ 碎片 DLL，而是按照业务领域输出动态库：
- `visiontools_preprocess.dll`
- `visiontools_matching.dll`
- `visiontools_measure.dll`
- `visiontools_barcode.dll`
- `visiontools_infer.dll`
- `visiontools_flow.dll`

### 4.2 C++ 算子 ABI 稳定接口契约（单一真相源 SSOT）
VisionRT 在 `src/sdk/include/visionrt/sdk/Tool.h` 提供统一定义并导出 `VisionRT::ToolSDK`，VisionTools 直接引用此 SDK，绝不维护第二套冲突接口：

```cpp
#include <visionrt/sdk/Tool.h>

namespace visionrt {

// 算子实例生命周期纯虚接口
class IToolInstance {
public:
    virtual ~IToolInstance() = default;
    virtual ToolStatus initialize(const Values& initParams) { return {}; }
    virtual ToolResult execute(const Values& inputs) = 0;
    virtual ToolStatus release() { return {}; }
};

// 动态库导出注册入口
extern "C" VISIONRT_TOOL_EXPORT int item_register(ToolRegisterContext* context);

} // namespace visionrt
```

### 4.3 算子实现示例
算子开发者面向强类型值系统编码，通过 `PlatformToolServices` 解析路径与读取资源：

```cpp
#include <visionrt/sdk/Tool.h>
#include <opencv2/imgproc.hpp>

class AdaptiveBinaryTool final : public visionrt::IToolInstance {
public:
    visionrt::ToolResult execute(const visionrt::Values& inputs) override {
        const auto& batch = std::get<visionrt::ImageBatch>(inputs.at("image"));
        if (batch.images().empty()) {
            return visionrt::ToolResult{visionrt::ToolStatus{1, "输入图像为空"}, {}};
        }
        const cv::Mat& src = batch.images()[0].readOnlyMat();
        int blockSize = static_cast<int>(std::get<int64_t>(inputs.at("block_size"))) | 1;
        double offset = std::get<double>(inputs.at("constant_offset"));

        cv::Mat dst;
        cv::adaptiveThreshold(src, dst, 255, cv::ADAPTIVE_THRESH_MEAN_C, 
                              cv::THRESH_BINARY, blockSize, offset);

        std::vector<visionrt::OwnedImage> outImages;
        outImages.push_back(visionrt::OwnedImage::copyOf(dst));
        return visionrt::ToolResult{
            visionrt::ToolStatus{},
            {{"binary_image", visionrt::ImageBatch(std::move(outImages))}}
        };
    }
};

// 领域库导出入口：通过 module_index.json 映射，启动时由 DomainLoader 统一加载
extern "C" VISIONRT_TOOL_EXPORT int item_register(visionrt::ToolRegisterContext* context) {
    if (!context) return -1;
    context->registerTool("visiontools.preprocess.adaptive_binary", [](const visionrt::ToolServices&) {
        return std::make_unique<AdaptiveBinaryTool>();
    });
    return 0;
}
```

### 4.4 Python 算子桥接规范
充分利用 `VisionRT` 已具备的高性能 `PythonBridge` 设施，算子直接接收与输出结构化字典，张量与图像通过只读 buffer 实现 0 拷贝：

```python
# assets/tools/深度学习/目标检测/yolo_detect.py
class ToolInstance:
    def __init__(self):
        self.model = None

    def initialize(self, params: dict):
        model_path = params.get("model_path", "")
        self.model = load_model(model_path)
        return {"__status_code": 0}

    def execute(self, inputs: dict):
        images = inputs.get("images", [])
        if not images:
            return {"__status_code": 1, "__status_message": "输入图像列表为空"}
        
        # images[0] 直接为共享内存的 ndarray 视图
        boxes, scores, classes = self.model.predict(images[0])
        return {
            "__status_code": 0,
            "boxes": boxes,       # 返回 numpy 数组，底层自动 0 拷贝转为 NumericArray
            "scores": scores,
            "classes": classes
        }
```

### 4.5 算子元数据契约（YAML SSOT）
存放于 `assets/tools/`，作为代码逻辑、UI 渲染与流程图拓扑连线的唯一事实源：

```yaml
schema_version: 1
tool_id: visiontools.preprocess.adaptive_binary
display_name: 自适应二值化
category: 预处理
subcategory: 色彩转换
version: 1.0.0
kind: cpp_class
entry:
  module: visiontools_preprocess
  symbol: VisionTools_Register

init_params: []

inputs:
  - id: image
    name: 输入图像
    type: ImageBatch
    editor: default
  - id: block_size
    name: 核大小(奇数)
    type: Int64
    editor: spinbox
    default: 3
    minimum: 3
    maximum: 999
  - id: constant_offset
    name: 偏移常量
    type: Float64
    editor: doublespinbox
    default: 5.0

outputs:
  - id: binary_image
    name: 二值图像
    type: ImageBatch
```

---

## 五、 工程协同与自动化生命周期

1. **VisionTools 本地独立构建**：
   - 算法开发者在 `VisionTools` 中编写算法与对应契约测试；
   - 运行 `tools\build.cmd` 完成 CMake 配置与 Ninja 编译；
   - 运行 `tools\build.cmd --test` 运行全量契约测试；
2. **一键同步与挂载到平台**：
   - 执行 `python tools\package_tools.py --target-dir F:\Projects\VisionRT`；
   - 脚本将编译产出的 `visiontools_*.dll` 部署到 `VisionRT/build/bin/`；
   - 将 `assets/tools/` 算子定义同步到 `VisionRT/assets/tools/`；
3. **VisionRT 运行时动态挂载**：
   - `VisionRT` 启动时，`ToolRegistry` 自动扫描 `tools` 目录；
   - 解析元数据，按需延迟加载领域动态库并绑定工厂；
   - 平台 UI（`ToolLibrary.qml` 与 `ParameterPanel.qml`）依据元数据自适应渲染工具树与参数编辑器；
   - 工作流拓扑调度引擎执行并实时呈现图像与状态。
