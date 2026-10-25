# 统一数据、JSON 与所有权规范

## S-DATA-01 类型与容器

| 类型 | C++ 工具视角 | Python 工具视角 | UI |
|---|---|---|---|
| Int | `std::int64_t` | `int`，排除 bool | 整数 |
| Float | `double` | `float` | 浮点数 |
| Bool | `bool` | `bool` | 布尔值 |
| String | UTF-8 `std::string` | `str` | 字符串 |
| Image | `std::vector<cv::Mat>`，由 DataRef/Owner 承载 | `list[np.ndarray]` | 图像 |
| Array rank 1 | `std::vector<T>` | `list[T]` | 一维…数组 |
| Array rank 2 | `std::vector<std::vector<T>>` | `list[list[T]]` | 二维…数组 |
| Array rank 3 / 4 | 按 rank 嵌套 vector | 按 rank 嵌套 list | 三维 / 四维…数组 |
| ROI | `std::vector<std::vector<std::vector<double>>>` | `list[list[list[float]]]` | ROI |

Python 没有内置 `double` 类型，这里的 Python float 对应双精度语义。Array 当前 rank 上限为 4；保持当前 Contracts 支持范围，不宣称任意 rank 已实现。dtype 使用 `uint8 / int32 / int64 / float32 / float64 / bool / string`；Python 数字需要做对应范围检查。数组可以不等长，空内层合法，不补齐、不扁平化。

Image 外层始终是图像列表，单图也是长度 1。禁止引入“图像批次”类型、Mask 类型、Region 类型、矩阵句柄或 Any 类型。mask 使用 Image，元素为 CV_8UC1；变换矩阵使用 Array(float64, rank=3)。图像像素支持范围由具体工具规定，可以小于桥接层支持范围，不能用“桥接能传”推导“算法支持”。

系统输出的内部 ID 保留 `__status_code` / `__status_message`，UI 显示“状态码：整数”“状态消息：字符串”。名称、内部 ID 和类型是三个字段，不能把 `__status_message String` 当类型标签。

## S-DATA-02 ROI、坐标和映射

统一 `ROI[image_index][roi_index][encoded_value]`，每个 ROI 记录的第一个值是整数语义的 kind（以 double 存储）。复用现有 Contracts 编码：

| kind | 记录 |
|---|---|
| 0 点 | `[0, x, y]` |
| 1 线段 | `[1, x1, y1, x2, y2]` |
| 2 旋转矩形 | `[2, x, y, width, height, angle_deg]` |
| 3 圆 | `[3, cx, cy, radius]` |
| 4 多边形 | `[4, x1, y1, x2, y2, x3, y3, ...]` |
| 5 点集 | `[5, x1, y1, ...]` |

矩形 x/y 是旋转前左上角，旋转中心为 `(x+w/2, y+h/2)`。图像坐标 x 向右、y 向下，正角度顺时针，单位度。像素中心坐标是整数；图像物理边缘为 `-0.5` 与 `W-0.5`、`H-0.5`。UI 复用 ImageViewport 的 imageToItem / itemToImage，不独立推导一套缩放平移公式。

ROI 非空时外层长度必须等于 N 张图像，不隐式把第一张 ROI 广播到所有图。`[]` 的语义由端口约定：亮度表示全图；测量表示未配置并报参数错误。`[[], ...]` 表示某图没有选区，亮度保留该图像素不变；测量对应零个测量请求。全组均空的测量输入视为未配置。

`alignment` 为 `[]`（单位矩阵）、长度 1（显式约定广播）或 N 个 3×3 矩阵；表示参考坐标到当前图像坐标的正向变换 `p_current = H * p_reference`。所有元素有限，最后一行为 `[0,0,1]`，仿射线性部分可逆；矩阵以行存储、列向量相乘。旋转输出 `H_out = A_rotation * H_in`。

圆、行间距首版仅接受保向相似变换：线性部分 `L^T L = s²I`、`det(L)>0`，使用相对容差，s>0。非均匀缩放/剪切使圆变椭圆，不能偷偷平均成圆。亮度区域可用一般仿射，圆区域经逆变换逐像素判定形成椭圆 mask，不伪造新 ROI kind。测量输出几何一律在当前图像像素坐标。

## S-DATA-03 空值与状态

`Missing` 是已有值状态，不是空数组、false、0 或空字符串。成功但未检出目标：工具状态成功，输出合法空集合及 valid/count/reason。配置非法、图像不支持、表达式异常：非零工具状态，业务输出按既有运行时规则为 Missing。

新增 RT 内部 `NodeDisposition = Pending / Running / Succeeded / Failed / Skipped / Blocked / Canceled`。工具不伪造成功结果表达“没有执行”。Skipped 是门控 false 或门控来源已跳过；Blocked 是门控失败、缺失，或所需上游数据不可用。Skipped / Blocked 发布本轮终态与 Missing 输出，未执行阶段 measured=false，耗时显示“—”。这不改变 Contracts ToolStatus ABI。

状态消息保持简短。例如“文件路径输入为空”“圆 ROI 未配置”“条件结果必须是布尔值”。正常 UI 不加“节点0 visionrt…执行”；诊断详情可以带节点与阶段定位。主消息是工具原文，不能把框架 prefix 伪装成工具消息。

## S-DATA-04 可写零拷贝与生命周期

复用当前 `PythonImages.cpp` 的 Storage / capsule / cv::UMatData / BufferLease，而不是再增加公共 `shared_ptr<cv::Mat>` 类型。零拷贝承诺针对可表示布局的像素，不针对 Python list、dict、数值数组容器或执行结果对象。

- C++→Python：ndarray shape/strides 指向 Mat 像素，base capsule 保持 Storage；Storage 保持 Mat 或外部 Owner。允许可表示的行间隔，不要求整张 C-contiguous。
- Python→C++：保留 ndarray/其底层 exporter 的生命周期，并让这个持有者跟随 Mat 的 UMatData。单独拷走 `cv::Mat` 头、做 ROI、删除原 ndarray 和外层 DataRef 后，图像仍有效。
- 返回 ndarray 不等于把所有权“转交”给 C++；这里是共享生命周期。OpenCV 不释放 NumPy 的物理内存，最终 Python 引用释放才由其所有者释放。
- 不支持的负步长、列跳步、字节序、dtype 等返回明确错误；工具可显式 `np.ascontiguousarray` 或 `np.array(copy=True)`。宿主不隐式整理、转换或复制像素。
- 对底层实际不可写的 ndarray 做合法性检查，不给 Contracts 数据加 readonly/readwrite 属性，也不擅自把不可写存储标成可写。
- 回环复用的是底层 Storage，不是无条件还原原始 Mat 头。Python 返回裁剪视图时必须尊重当前 data offset、shape、strides 和范围，避免把裁剪图错误还原成整图。
- 地址 `% itemsize == 0` 只验证标量对齐，不代表 AVX 对齐；不能据此使用要求更高对齐的指令。
- C++ 最后一个持有者可能在后台线程释放；Python 对象操作遵循现有 GIL/存储释放协议。关闭先停止调度，排空 RT 内部读者、正式与辅助工具、结果和预览 Owner，再 prepareShutdown，等外部持有者释放，最后销毁解释器。

DataRef/Owner 保证存活，不保证像素不变。VisionTools 决定原地、复制、新输出及共享写协调。RT 必须协调自己的 UI、文本、保存、预览读者，不能一边读同一可写像素一边启动新轮次。外部组件自行保留和修改数据的并发责任仍在该组件。

## S-JSON-01 目标格式

目标声明 schema 为 3，工程 format 为 4；本次配套工具包一次升级声明，不维护 YAML 或构建生成器，也不实现长期双版本解析分支。旧工程自动迁移不在本包承诺范围内，若必须保留已有工程，单列一次性迁移交付，不在正式加载路径猜测旧语义。

声明在既有字段上新增以下宿主元数据：

| 字段 | 语义 |
|---|---|
| `node_model` | `fixed`（默认）、`expression`、`condition`、`select`；只描述宿主编辑/物化方式 |
| `visibility` | `normal`（默认）或 `assist`；辅助工具不进入普通添加菜单 |
| `interaction` | 可选对象，声明 helper_tool_id、输入映射、proposal 映射、允许 ROI kind |
| ROI输入的 `alignment_input` | 同节点 Array(float64,3) 输入的ID；供ROI编辑/叠加进行参考坐标与当前图像坐标转换 |

保留 `kind=cpp/python_class/python_function/host`、现有 `entry` 和内建/自定义来源目录规则。cpp 不带 entry，按 tool_id 手写注册工厂。Python entry 的 package/file/symbol 均相对于该声明的来源根解析；不能让 custom fallback 到 builtin 同名脚本。未知字段和非法映射在扫描时诊断，缺 helper 时禁用该交互并说明原因，不拖垮正常正式测量。

工程节点新增可选 `expression_config`、`execution_guard`、`select_type`。它们是配置事实源；派生端口、初始化表、Taskflow 边不重复持久化成另一套权威数据。节点/版本号仍沿用现有无损字符串序列编码。类型和值编码复用 ValueJson：尤其不能把 int64 先转 QJson double 导致精度损失；严格 JSON 禁止 NaN/Infinity。

工程保存、参数编辑、输出文本导出均使用 JSON。图像输出文本按现有摘要/资源描述导出，不把全部像素塞入 JSON；保存图像沿用明确的图像导出操作。表达式的符号、端口名、公式都是普通字符串，不能为了编辑方便增加 YAML 内嵌字符串。
