# Python 互操作开源方案索引

核实日期：2026-10-19；仅使用官方文档/源码。版本适用范围以各引用的 tag/文档版本为准；滚动文档与 master 不代表本地锁定版本验证。未做运行验证。
本地代码证据见 [实现导航](visiontools-bridge-architecture.md)，选型与迁移要求见 [规格](tool-package-delivery/spec.md)；本文件只记录上游事实。

| 候选 | 已核实能力 | 限制 | 取舍理由 | 官方来源 | 核实日期 |
| --- | --- | --- | --- | --- | --- |
| pybind11（已有） | 引用 RAII、NumPy/buffer、外部内存 base owner。 | object 析构不自动 acquire GIL；array_t 默认 forcecast；ptr 无 base 的数组构造会复制。 | 保留；约束现有桥接，不无证据迁移。 | [GIL][P1]、[引用][P2]、[NumPy][P3]、[数组源码][P4] | 2026-10-19 |
| nanobind | ndarray 支持 dtype/shape/layout/只读约束及 owner capsule。 | 转换可复制，需 noconvert 禁止；无 owner 的 reference 返回可被当作永久有效。 | 不选；无本项目迁移收益证据，不声称更快或整体线程安全。 | [ndarray][N1] | 2026-10-19 |
| NumPy / buffer protocol（协议层） | 描述格式、shape、字节 strides、readonly；正常获取的 Py_buffer 持 exporter 引用，Release 释放。 | 不保证连续；负步长/间接寻址不可当平坦内存；只读不冻结别名。 | 配合 pybind11 复用，不自建协议。 | [buffer][B1]、[flags][U1] | 2026-10-19 |

## 引用与限制
- `py::handle` 为不自动增减引用的 borrowed 包装；`py::object`/array 持引用。object 复制、赋值、析构会操作引用计数且不自动 acquire GIL；由此推导，容器 clear、异步闭包最后释放都要审查，不能发生在 gil_scoped_release 有效期间。error_already_set 也持 Python 状态，其内部 deleter 需 GIL且有 finalizing 风险。[P1][P2]
- capsule 应持完整分配的纯 C++ owner，而非临时地址；base 保活与清理回调见源码。由此推导，异步任务、capsule、自定义 deleter 全部释放前，还需保活回调所在 DLL；内存 owner 不自动保活代码模块。[P2][P4]
- 真正无数据复制要求类型/布局无需转换、共享同一存储且 owner 覆盖使用期；array_t 默认 forcecast、连续性要求或 np.asarray 的 dtype/order 都可能复制。noconvert 参数注解不自动约束接收 py::handle 的内部转换，此为其作用范围推导。[P3][P6][U2]
- readonly 是访问限制，既有可写 NumPy 视图仍可写；由此推导也不冻结 C++ 别名。必须另行约束生产者改写/重分配。非连续 buffer 的 len 不等于可顺序访问的物理跨度，不可任意整块 memcpy。[U1][B1]
- Python-owned 输出是延长 Python 引用及其解释器/GIL/原生回调代码的寿命约束，不是转成纯 C++ owner；这是所有权规则推导。默认保留 copy-out，且目标不能浅拷贝残留 Python 引用。[P1][P2][B1]

## CPython 与未选协议（同上核实日期）
| 候选 | 已核实能力与限制 | 取舍理由 | 官方来源 |
| --- | --- | --- | --- |
| CPython 单解释器 | 常规 GIL 构建调用 C API 要附着线程状态并持 GIL；PyGILState_* 默认对应主解释器。finalize 须在初始化线程、主解释器激活时调用。 | 保持现状；停止接入并排空任务，解释器存活且持 GIL 时清理 Python 引用，再由宿主 finalize；等待需要 GIL 的 worker 时不能占着 GIL。此为约束推导，不新增运行时。 | [线程][C1]、[生命周期][C2] |
| subinterpreter | CPython 3.12 起可独立 GIL；PyGILState_* 不支持混用多解释器，扩展的多阶段初始化/隔离也有要求。 | 不选；不能仅靠绑定库声明推定全部 NumPy/算法扩展兼容。 | [C1]、[扩展加载][C3] |
| free-threading | 无 GIL 不免除线程状态附着；扩展需声明支持，否则导入可能重启 GIL；borrowed 引用、共享可变对象需另行同步。 | 不选；移除 GIL 不等于现有桥接线程安全，需全依赖链验证。 | [扩展规范][C4] |
| DLPack | 数值张量交换支持设备、布局、版本/stream 协商；copy=False 不可无复制导出时须失败。capsule 消费一次，consumer 最终恰一次调用 deleter；GPU 要同步。 | 不入主链；不能代替任意参数/ROI 契约，也不移除 owner/解释器或 DLL 寿命问题。 | [Python 规范][D1]、[C 头文件][D2] |
| Array API | 数组操作规范及 __dlpack__ 接口，不是无条件零拷贝。规范允许不支持 readonly 的 consumer 忽略 READ_ONLY 标志。 | 不选作只读边界；不能用 DLPack 普遍保证输入不可修改，也不管理 Python 对象生命周期。 | [接口规范][A1] |

- **终止不是兜底清理**：CPython 3.14 终止期间其他线程附着会永久阻塞；3.12 文档对应行为是终止调用线程。Py_IsInitialized/Py_IsFinalizing 检查不是任务与 shutdown 的同步屏障（推导）。Py_FinalizeEx 还存在销毁顺序、扩展重复初始化及不卸载扩展动态库等限制，不作为工具重载机制。[C1][C2][C5]
- **QuickTools 外部语义核实**：官方 pybind11 v3.0.1 与 master 的 array(buffer_info, base) 转发到底层构造，ptr 存在而无 base 时走 PyArray_NewCopy_；不能因传入 img.data/strides 就称零拷贝。object.release() 交出引用、清空包装对象，不 DECREF；若引用既未转交 owner 又未减引用，会泄露（推导）。本地代码证据见主设计文档。[P4][Q1][Q2][Q3]
- **验证边界**：仅核实官方契约/源码，无本地版本构建、性能、线程安全或真实环境验证；不声称更快或整体线程安全。nanobind、DLPack、subinterpreter、共享输出 allocator 均不列入待实现。

[P1]: https://pybind11.readthedocs.io/en/stable/advanced/misc.html
[P2]: https://github.com/pybind/pybind11/blob/master/include/pybind11/pytypes.h
[P3]: https://pybind11.readthedocs.io/en/stable/advanced/pycpp/numpy.html
[P4]: https://github.com/pybind/pybind11/blob/master/include/pybind11/numpy.h
[P6]: https://pybind11.readthedocs.io/en/stable/advanced/functions.html#non-converting-arguments
[N1]: https://nanobind.readthedocs.io/en/latest/ndarray.html
[B1]: https://docs.python.org/3.14/c-api/buffer.html
[U1]: https://numpy.org/doc/stable/reference/generated/numpy.ndarray.flags.html
[U2]: https://numpy.org/doc/stable/reference/generated/numpy.asarray.html
[C1]: https://docs.python.org/3.14/c-api/init.html#thread-state-and-the-global-interpreter-lock
[C2]: https://docs.python.org/3.14/c-api/interp-lifecycle.html#c.Py_FinalizeEx
[C3]: https://docs.python.org/3.14/library/importlib.html#importlib.machinery.ExtensionFileLoader
[C4]: https://docs.python.org/3.14/howto/free-threading-extensions.html
[C5]: https://docs.python.org/3.12/c-api/init.html#c.PyGILState_Ensure
[D1]: https://dmlc.github.io/dlpack/latest/python_spec.html
[D2]: https://github.com/dmlc/dlpack/blob/main/include/dlpack/dlpack.h
[A1]: https://data-apis.org/array-api/2024.12/API_specification/generated/array_api.array.__dlpack__.html
[Q1]: https://github.com/pybind/pybind11/blob/v3.0.1/include/pybind11/numpy.h#L1002
[Q2]: https://github.com/pybind/pybind11/blob/v3.0.1/include/pybind11/pytypes.h#L371
[Q3]: https://pybind11.readthedocs.io/en/stable/reference.html#_CPPv4N8pybind116object7releaseEv
