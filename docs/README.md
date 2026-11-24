# VisionRT 工程文档索引

## 验证与进度

- [任务账本](../WORKLOG.md)：实际进度、运行证据与后续入口。
- [工作台训练](../tests/workbench_training_contract.cpp)、[训练发布](../tests/workbench_training_publish_contract.cpp)、[点选交互](../tests/workbench_assist_contract.cpp)、[随包示例](../tests/example_pipelines.cpp)。
- [动态节点与事务](../tests/logic_node_execution_test.cpp)、[表达式工作台](../tests/workbench_expression_contract.cpp)。
- [工程保存与重开](../tests/project_storage_contract.cpp)、[工程文件对话框](../tests/workbench_startup.cpp)、[示例另存](../tests/example_project_contract.cpp)、[随包示例检查](../tests/example_metadata_contract.cpp)、[发行示例检查](../tests/test_package_app.py)。

## 契约与核心模块

| 主题 | 唯一实现入口 |
| --- | --- |
| SDK 类型、状态与模块 ABI | [Contracts 文档](../3rdparty/VisionRTContracts/docs/README.md) |
| 工具声明、JSON 读写及名称 | [声明接口](../3rdparty/VisionRTContracts/include/visionrt/contracts/Declaration.h)、[编解码](../3rdparty/VisionRTContracts/include/visionrt/contracts/DeclarationJson.h)、[语义名称](../3rdparty/VisionRTContracts/include/visionrt/contracts/DeclarationNames.def) |
| 公共枚举与 QML/Python 导出 | [枚举源](../3rdparty/VisionRTContracts/include/visionrt/contracts/ProtocolEnums.h)、[生成器](../3rdparty/VisionRTContracts/tools/export_enums.py)、[Qt 注册](../src/ui/UiTypes.cpp) |
| 工具发现及工作台适配 | [工具包](../src/core/ToolPackage.cpp)、[注册表](../src/core/ToolRegistry.cpp)、[集成登记](../src/core/include/core/ToolIntegrations.h) |
| 工程配置与持久化 | [工程模型](../src/core/include/core/Project.h)、[编辑事务](../src/core/ProjectEditor.cpp)、[存储](../src/core/ProjectStorage.cpp)、[动态契约](../src/core/NodeContracts.cpp) |
| 执行、快照与读回 | [运行时](../src/runtime/include/runtime/RuntimeEngine.h)、[快照](../src/runtime/include/runtime/Snapshot.h)、[结果展示](../src/ui/ResultPresentation.h) |
| 原生与 Python 工具 | [插件加载](../src/plugins/PluginLoader.cpp)、[环境状态](../src/python/include/python/PythonEnvironment.h)、[探测](../src/python/PythonProbe.cpp)、[后端](../src/python/PythonBackend.cpp)、[值桥](../src/python_bridge/PythonInterop.cpp) |
| 训练草稿与发布 | [实例接口](../3rdparty/VisionRTContracts/include/visionrt/contracts/Tool.h)、[统一参数声明](../3rdparty/VisionRTContracts/include/visionrt/contracts/Declaration.h)、[会话](../src/ui/include/ui/TrainingSession.h)、[资产所有权](../src/ui/include/ui/TrainingAssets.h)、[工作台接线](../src/ui/AppControllerTraining.cpp) |
| 日志与诊断 | [日志服务](../src/log/include/log/LogService.h)、[宿主错误](../src/core/include/core/Errors.def)、[诊断投影](../src/ui/Diagnostics.cpp) |

## 工作台

- [主窗口](../src/ui/qml/Main.qml)、[控制器接口](../src/ui/include/ui/AppController.h)、[流程画布](../src/ui/qml/FlowCanvas.qml)。
- [参数](../src/ui/qml/ParameterPanel.qml)、[结果](../src/ui/qml/ResultsPanel.qml)、[缩略图卡片](../src/ui/qml/ResultsThumbnailCard.qml)、[训练记录](../src/ui/qml/ResultsTrainingRecords.qml)、[ROI 编辑](../src/ui/RoiEditSession.cpp)。
- [工具编辑器](../src/ui/ToolEditorController.cpp)、[用户工具存储](../src/core/CustomToolStore.cpp)。
- [设置](../src/ui/UiPreferences.cpp)、[关于信息](../src/ui/AboutReport.cpp)、[页签](../src/ui/qml/WorkbenchTabButton.qml)、[尺寸属性](../src/ui/include/ui/Size.h)、[颜色属性](../src/ui/include/ui/Color.h)。
- [通用主题颜色](../3rdparty/QuickUI/src/Color.cpp)、[字体属性](../3rdparty/QuickUI/src/Font.cpp)、[工作台滚动条](../src/ui/qml/WorkbenchScrollBar.qml)、[滚动容器](../src/ui/qml/BoundedScrollView.qml)、[训练字段编辑器](../src/ui/qml/TrainingValueEditor.qml)。
- [下拉框属性](../3rdparty/QuickUI/src/controls/QuiComboBox.qml)、[分组下拉框](../3rdparty/QuickUI/src/controls/QuiGroupComboBox.qml)、[参数下拉框](../src/ui/qml/ParameterComboBox.qml)。
- [值的中文类型](../src/ui/DisplayTypes.cpp)、[输入输出数值显示](../src/ui/ValueDisplay.cpp)、[参数与分组投影](../src/ui/AppControllerMetadata.cpp)、[参数精度验证](../tests/ui_value_contract.cpp)、[完整输出数据](../src/ui/OutputModel.cpp)、[阶段输出校验](../src/core/ToolValueValidation.cpp)、[值编解码](../src/core/ValueJson.cpp)、[几何编辑验证](../tests/workbench_roi.cpp)、[输出数据验证](../tests/output_value_contract.cpp)。
- [合流类型选择](../src/ui/qml/SelectTypeEditor.qml)、[类型选择与表达式验证](../tests/workbench_expression_contract.cpp)。
- [ROI 手势](../src/ui/ImageViewportRoi.cpp)、[装饰绘制](../src/ui/RoiOverlay.cpp)、[几何与命中](../src/ui/RoiGeometry.h)、[参数编辑](../src/ui/qml/ParameterEditor.qml)、[交互验证](../tests/roi_interaction_contract.cpp)。
- [ROI 显示、参数编辑与焦点验证](../tests/workbench_roi_persistence_contract.cpp)、[中心悬浮标签与 ROI 绘制验证](../tests/viewport_contract.cpp)。
- [像素来源与显示转换](../src/ui/DisplayConverter.cpp)、[Alt 像素读取](../src/ui/ImageViewport.cpp)、[指针提示](../src/ui/qml/ResultsImageView.qml)、[像素验证](../tests/viewport_pixel_contract.cpp)、[实际窗口与截图验证](../tests/viewport_contract.cpp)。
- [节点悬浮提示](../src/ui/qml/FlowCanvas.qml)、[节点提示与状态验证](../tests/workbench_status_contract.cpp)、[像素提示与焦点验证](../tests/workbench_roi_persistence_contract.cpp)。

## 构建与发行

- [工具脚本索引](../tools/README.md)、[构建入口](../tools/build.cmd)、[默认构建预设](../CMakePresets.json)、[根 CMake](../CMakeLists.txt)、[依赖配置](../tools/dependencies.json)、[子模块来源](../.gitmodules)。
- [模块构建](../cmake/AddModule.cmake)、[构建身份](../cmake/WriteBuildInfo.cmake)、[部署](../cmake/Deploy.cmake.in)。
- [安装打包](../tools/package_app.py)、[依赖闭包](../tools/runtime_dependencies.py)、[发布证据](../tools/release_evidence.py)、[工具校验器](../tools/visionrt_validate_tool.cpp)。
- [测试注册与分组](../tests/CMakeLists.txt)、[分组机制](../cmake/AddTest.cmake)、[随包示例](../examples/README.md)。

## 仓库认知

- [工程规则](../AGENTS.md)、[AOCI Root](../aoci.txt)、[Meta](../aoci.meta.txt)、[Code](../aoci.code.txt)。
