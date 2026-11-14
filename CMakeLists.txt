cmake_minimum_required(VERSION 3.25)
project(VisionRT VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 引入构建系统与编译器配置模块
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ConfigBuildTree)
include(ConfigCompiler)
include(ConfigVersion)

# 基础与公共依赖配置
include(ConfigDependencyDefaults)
include(ConfigOpenCV)
include(ConfigQT)
include(Config3rdparty)
include(ConfigPython)
include(ConfigQuickUI)
include(ConfigSpdlog)

# 测试开关与 enable_testing() 必须先于任何子目录：Contracts 在自己的目录里注册契约测试，
# 这里只负责把它们编入 sdk 组。
include(CTest)
set(VISIONRT_CONTRACTS_TESTS ${BUILD_TESTING} CACHE BOOL "构建 Contracts 自身的契约测试（归入 sdk 组）")
set(VISIONRT_CONTRACTS_INSTALL ON CACHE BOOL "Contracts 随宿主安装，供安装态消费者契约使用")
option(VISIONRT_RELEASE_BUILD "显式发布候选构建模式（要求干净工作区与明确 Git 提交）" OFF)

# 独立通用工具 SDK（Git 子模块）
add_subdirectory(3rdparty/VisionRTContracts)

# 核心通用库
add_library(visionrt_common
    src/common/Version.cpp
    src/core/Error.cpp)
target_include_directories(visionrt_common PUBLIC src)
target_link_libraries(visionrt_common PUBLIC VisionRT::Contracts)
configure_version(visionrt_common VISIONRT visionrt/common ${PROJECT_VERSION})

# 日志服务：异步 spdlog、轮转文件、有界 UI 信箱与 Qt 消息桥
set(VISIONRT_LOG_SOURCES
    src/log/LogService.cpp
    src/log/LogService.h
    src/log/LogLevel.cpp
    src/log/LogLevel.h
    src/log/LogModel.cpp
    src/log/LogModel.h)
add_library(visionrt_log ${VISIONRT_LOG_SOURCES})
target_link_libraries(visionrt_log PUBLIC visionrt_common Qt6::Core spdlog::spdlog)
target_include_directories(visionrt_log PUBLIC src)

# 核心业务逻辑库
add_library(visionrt_core
    src/core/ToolDefinition.cpp
    src/core/ToolValueValidation.cpp
    src/core/InputNormalizer.cpp
    src/core/DefinitionJson.cpp
    src/core/ModuleIndex.cpp
    src/core/ToolPackage.cpp
    src/core/ValueJson.cpp
    src/core/ValueJsonDocument.cpp
    src/core/NodeContracts.cpp
    src/core/ToolRegistry.cpp
    src/core/ProjectEditor.cpp
    src/core/ProjectValues.cpp
    src/core/ProjectStorage.cpp
    src/core/ExampleProject.cpp
    src/core/CustomToolStore.cpp
    src/plugins/PluginLoader.cpp)
target_link_libraries(visionrt_core PUBLIC visionrt_common Qt6::Core)

add_executable(visionrt_validate_tool tools/visionrt_validate_tool.cpp)
target_link_libraries(visionrt_validate_tool PRIVATE visionrt_core)


# Python 运行时：值桥接（静态）与解释器运行时的宿主侧
# 宿主只链接 visionrt_python_host；解释器实现被编进按需加载的桥接 DLL，主程序启动链不含
# pybind11::embed 或任何 CPython 导入。
add_library(visionrt_python_values src/python/PythonInterop.cpp src/python/PythonImages.cpp src/python/PythonStorage.cpp)
target_link_libraries(visionrt_python_values PUBLIC visionrt_core pybind11::embed)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/python/managed_packages.py")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/python/managed_packages.py" VISIONRT_PYTHON_LOADER)
configure_file(src/python/PythonLoader.h.in python/PythonLoader.h @ONLY)

add_library(visionrt_python_runtime STATIC src/python/PythonRuntime.cpp src/python/PythonRuntime.h)
target_link_libraries(visionrt_python_runtime PUBLIC visionrt_python_values)
target_include_directories(visionrt_python_runtime PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/python")
target_compile_definitions(visionrt_python_runtime PUBLIC PYBIND11_DETAILED_ERROR_MESSAGES)

# 桥接模块：只由宿主在运行期按路径加载，绝不被主程序链接
add_library(visionrt_python SHARED src/python/PythonBridge.cpp src/python/PythonBridgeApi.h)
target_link_libraries(visionrt_python PRIVATE visionrt_python_runtime)
target_compile_definitions(visionrt_python PRIVATE VISIONRT_PYTHON_BRIDGE_BUILD=1)
set_target_properties(visionrt_python PROPERTIES OUTPUT_NAME visionrt_python_bridge)


# 宿主侧环境探测与桥接调度：只依赖 Qt Core，不认识 pybind11
add_library(visionrt_python_host STATIC
    src/python/PythonBackend.cpp src/python/PythonBackend.h
    src/python/PythonProbe.cpp src/python/PythonProbe.h
    src/python/PythonEnvironment.h)
target_link_libraries(visionrt_python_host PUBLIC visionrt_core Qt6::Core)
target_compile_definitions(visionrt_python_host PRIVATE
    VISIONRT_PYTHON_BUILD_MAJOR=${Python3_VERSION_MAJOR}
    VISIONRT_PYTHON_BUILD_MINOR=${Python3_VERSION_MINOR})

# 测试用组合：直接编入解释器实现，并用构建解释器做一次真实探测作为显式测试配置。
add_library(visionrt_python_testing STATIC tests/TestPythonEnvironment.cpp tests/TestPythonEnvironment.h)
target_link_libraries(visionrt_python_testing PUBLIC visionrt_python_runtime visionrt_python_host)
target_include_directories(visionrt_python_testing PUBLIC "${CMAKE_SOURCE_DIR}/tests")
target_compile_definitions(visionrt_python_testing PRIVATE
    VISIONRT_TEST_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}")
# The interpreter path is also the test-side configuration a controller receives before it exists;
# consumers get the same value so the seam needs no probe of its own.
target_compile_definitions(visionrt_python_testing INTERFACE
    VISIONRT_TEST_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}")

# DAG 执行调度引擎库
add_library(visionrt_runtime
    src/runtime/RuntimeEngine.cpp
    src/runtime/NodeRuntime.cpp
    src/runtime/SelectTool.cpp
    src/runtime/Receiver.cpp)
target_link_libraries(visionrt_runtime PUBLIC visionrt_core visionrt_taskflow)

# 桌面 Workbench 应用程序与发布配置
add_library(visionrt_presentation
    src/ui/DisplayConverter.cpp src/ui/DisplayConverter.h
    src/ui/DisplayTypes.cpp src/ui/DisplayTypes.h
    src/ui/OutputModel.cpp src/ui/OutputModel.h
    src/ui/Diagnostics.cpp src/ui/Diagnostics.h)
target_link_libraries(visionrt_presentation PUBLIC visionrt_core Qt6::Gui)

option(VISIONRT_BUILD_APP "Build the desktop workbench" ON)
# DPI/外观截图只在人工视觉验收时运行：常规回归不需要为每个缩放级别启动窗口并落图。
option(VISIONRT_TEST_VISUAL "注册 DPI 与外观截图场景" OFF)
if(VISIONRT_BUILD_APP)
    include(ConfigLibavoid)
    add_library(visionrt_viewport src/ui/ImageViewport.cpp src/ui/ImageViewport.h)
    target_link_libraries(visionrt_viewport PUBLIC visionrt_presentation Qt6::Quick)
    add_library(visionrt_ui
        src/ui/Theme.cpp
        src/ui/Theme.h
        src/ui/DurationText.cpp
        src/ui/DurationText.h
        src/ui/WireRouter.cpp
        src/ui/WireRouter.h
        src/ui/AppController.cpp
        src/ui/AppControllerAssist.cpp
        src/ui/AboutReport.cpp
        src/ui/AboutReport.h
        src/ui/UiPreferences.cpp
        src/ui/UiPreferences.h
        src/ui/RecentProjects.cpp
        src/ui/RecentProjects.h
        src/ui/AppControllerBindings.cpp
        src/ui/AppControllerDisplay.cpp
        src/ui/AppControllerReadback.cpp
        src/ui/AppControllerRoi.cpp
        src/ui/NodeListModel.cpp
        src/ui/NodeListModel.h
        src/ui/ResultPresentation.cpp
        src/ui/RoiEditSession.cpp
        src/ui/RoiEditSession.h
        src/ui/ProjectOperations.cpp
        src/ui/ProjectOperations.h
        src/ui/AppControllerMetadata.cpp
        src/ui/AppControllerExpression.cpp
        src/ui/AppControllerParameters.cpp
        src/ui/AppControllerRouting.cpp
        src/ui/AssistPreview.cpp
        src/ui/AssistPreview.h
        src/ui/AppController.h
        src/ui/AppControllerDiagnostics.cpp
        src/ui/ToolTreeModel.cpp
        src/ui/ToolTreeModel.h
        src/ui/ToolPortListModel.cpp
        src/ui/ToolPortListModel.h
        src/ui/ToolEditorController.cpp
        src/ui/ToolEditorController.h)
    target_link_libraries(visionrt_ui PUBLIC
        visionrt_runtime visionrt_python_host visionrt_viewport visionrt_log
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 quickui::quickui)
    target_link_libraries(visionrt_ui PRIVATE visionrt_avoid)

    # 窗口程序，不是控制台程序：双击启动时不应额外打开命令行终端。命令行走文件与退出码，
    # 不依赖标准输出，所以 GUI 子系统不损失任何既有入口。
    qt_add_executable(VisionRT WIN32 src/main.cpp)
    target_link_libraries(VisionRT PRIVATE visionrt_ui quickui::quickui)
    # 桥接模块不参与链接，但必须随程序构建到同一目录，供运行期按名称加载。
    add_dependencies(VisionRT visionrt_python)

    file(GLOB visionrt_qml CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/ui/qml/*.qml")
    foreach(qml_file IN LISTS visionrt_qml)
        get_filename_component(qml_name "${qml_file}" NAME)
        set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
    endforeach()
    qt_add_qml_module(VisionRT URI VisionRT VERSION 1.0 DEPENDENCIES quickui QML_FILES ${visionrt_qml})

    # 构建元数据必须来自“构建完成”而不是“配置开始”，所以两份组件信息都在链接结束后生成：
    # VisionRT 用当前时刻（刚刚完成链接），Contracts 用它自己产物的最后修改时间，未重新链接的
    # 静态库因此保留上一次真实构建时刻。关于弹窗只读取这两个文件，不读编译期配置时间。
    add_custom_command(TARGET VisionRT POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DCOMPONENT=VisionRT
            -DVERSION=${PROJECT_VERSION}${PROJECT_VERSION_SUFFIX}
            -DCOMMIT=${REPO_COMMIT}
            -DBRANCH=${REPO_BRANCH}
            -DREPO_DIR=${CMAKE_SOURCE_DIR}
            -DSOURCE_FILE=$<TARGET_FILE:VisionRT>
            -DOUTPUT=$<TARGET_FILE_DIR:VisionRT>/build_info.json
            -DRELEASE_BUILD=${VISIONRT_RELEASE_BUILD}
            -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
            -P "${CMAKE_SOURCE_DIR}/cmake/WriteBuildInfo.cmake"
        COMMAND ${CMAKE_COMMAND}
            -DCOMPONENT=VisionRTContracts
            -DVERSION=${VISIONRT_CONTRACTS_VERSION}
            -DCOMMIT=${VISIONRT_CONTRACTS_COMMIT}
            -DBRANCH=${VISIONRT_CONTRACTS_BRANCH}
            -DREPO_DIR=${CMAKE_SOURCE_DIR}/3rdparty/VisionRTContracts
            -DSOURCE_FILE=$<TARGET_FILE:VisionRTContracts>
            -DOUTPUT=$<TARGET_FILE_DIR:VisionRT>/contracts-build-info.json
            -DRELEASE_BUILD=${VISIONRT_RELEASE_BUILD}
            -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
            -P "${CMAKE_SOURCE_DIR}/cmake/WriteBuildInfo.cmake"
        COMMENT "记录 VisionRT 与 VisionRTContracts 的构建完成时刻"
        VERBATIM)

    configure_file(cmake/Deploy.cmake.in "${CMAKE_BINARY_DIR}/Deploy.cmake" @ONLY)
    add_custom_target(deploy
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_BINARY_DIR}/Deploy.cmake"
        DEPENDS VisionRT
        USES_TERMINAL)

    # 开发运行目录与安装态同构：示例工程就在可执行文件旁边，菜单在 build/bin 下同样可用。
    # 先清空再复制，改名或删除的示例不会残留在运行目录里冒充产品文件。
    add_custom_target(examples_runtime ALL
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "$<TARGET_FILE_DIR:VisionRT>/examples"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/examples" "$<TARGET_FILE_DIR:VisionRT>/examples"
        COMMENT "同步示例工程到运行目录")
    add_dependencies(examples_runtime VisionRT)
endif()

# 开发与本地调试动态库拷贝目标：OpenCV 取自本次配置实际链接的导入目标，Python 取自宿主解释器
set(_visionrt_runtime_dlls ${VISIONRT_OPENCV_RUNTIME_DLLS} ${PYTHON_DLLS})
if(_visionrt_runtime_dlls)
    add_custom_target(deploy_runtime_dlls ALL
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${_visionrt_runtime_dlls} "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMENT "Deploying runtime DLLs to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )
endif()

# 测试契约与自动化测试用例
if(BUILD_TESTING)
    include(AddTest)
    visionrt_define_test_groups()
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

    set(VISIONRT_TEST_TOOLS_PACKAGE "${CMAKE_BINARY_DIR}/tools-package" CACHE PATH "Installed tools package used by integration contracts")
    set(VISIONRT_GPU_FIXTURES "${CMAKE_BINARY_DIR}/gpu-fixtures" CACHE PATH "YOLO fixtures (bus.jpg, yolov8n.wts) for the GPU contract")

    visionrt_contract(version_contract visionrt_common)
    visionrt_contract(value_contract visionrt_core)
    visionrt_contract(path_contract visionrt_core)

    visionrt_contract(tool_registry_contract visionrt_core)
    visionrt_contract(port_value_validation_test visionrt_core)
    visionrt_contract(definition_json_contract visionrt_core)
    visionrt_contract(tool_definition_validation_test visionrt_core)
    visionrt_contract(tools_package_contract visionrt_core)
    visionrt_contract(custom_tool_store_contract visionrt_core)
    add_test(NAME validator_cli_contract COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tests/validator_cli_contract.py"
        --validator "$<TARGET_FILE:visionrt_validate_tool>" --opencv-bin "${VISIONRT_OPENCV_BIN}"
        --module "$<TARGET_FILE:test_registration_valid>" --bad-module "$<TARGET_FILE:test_plugin_legacy>")
    visionrt_apply_test_environment(validator_cli_contract)
    add_test(NAME package_app_contract COMMAND "${CMAKE_COMMAND}" -E env
        "VISIONTOOLS_VALIDATOR=$<TARGET_FILE:visionrt_validate_tool>"
        "CMAKE_COMMAND=${CMAKE_COMMAND}"
        "CMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
        "CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "VISIONRT_BUILD_ENTRY=${CMAKE_SOURCE_DIR}/tools/build.cmd"
        "${Python3_EXECUTABLE}" -S "${CMAKE_SOURCE_DIR}/tests/test_package_app.py")
    visionrt_apply_test_environment(package_app_contract)
    # The release record is assembled from facts another process produced; its audit rules are pure
    # Python and need neither a build nor an installation, so they run as their own quick test.
    add_test(NAME release_evidence_contract COMMAND "${CMAKE_COMMAND}" -E env
        "${Python3_EXECUTABLE}" -S "${CMAKE_SOURCE_DIR}/tests/test_release_evidence.py")
    # The six real nested Git+CMake builds are a separate entry: the quick package suite must not
    # recurse into a compiler, and a candidate selects them explicitly with -L integration.
    add_test(NAME build_metadata_integration_contract COMMAND "${CMAKE_COMMAND}" -E env
        "VISIONTOOLS_VALIDATOR=$<TARGET_FILE:visionrt_validate_tool>"
        "CMAKE_COMMAND=${CMAKE_COMMAND}"
        "CMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
        "CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "VISIONRT_BUILD_ENTRY=${CMAKE_SOURCE_DIR}/tools/build.cmd"
        "${Python3_EXECUTABLE}" -S "${CMAKE_SOURCE_DIR}/tests/test_build_metadata_integration.py")
    visionrt_apply_test_environment(build_metadata_integration_contract)
    set_tests_properties(build_metadata_integration_contract PROPERTIES LABELS "package;integration")
    add_test(NAME dependency_manifest_contract COMMAND "${CMAKE_COMMAND}" -E env
        "CMAKE_COMMAND=${CMAKE_COMMAND}" "${Python3_EXECUTABLE}" -S
        "${CMAKE_SOURCE_DIR}/tests/dependency_manifest_contract.py")
    visionrt_apply_test_environment(dependency_manifest_contract)
    if(WIN32)
        # The nested configure and build reuse the toolchain this project already resolved; the
        # MSVC environment itself comes from build.cmd, so no second discovery chain is needed here.
        add_test(NAME build_incremental_contract COMMAND "${CMAKE_COMMAND}" -E env
            "CMAKE_COMMAND=${CMAKE_COMMAND}" "CMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            "CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}" "CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/build_incremental_contract.py")
    endif()
    visionrt_contract(value_json_contract visionrt_core)
    add_library(visionrt_log_testing ${VISIONRT_LOG_SOURCES})
    target_link_libraries(visionrt_log_testing PUBLIC visionrt_common Qt6::Core spdlog::spdlog)
    target_include_directories(visionrt_log_testing PUBLIC src)
    target_compile_definitions(visionrt_log_testing PRIVATE VISIONRT_LOG_TEST_HOOKS=1)
    # The log contract is split by responsibility: text/allocation, aggregation and lifecycle.
    # All three share the production logging library and the same test hooks.
    foreach(log_test log_text_contract log_aggregation_contract log_lifecycle_contract log_level_contract)
        visionrt_contract(${log_test} visionrt_log_testing LABELS backend)
        target_compile_definitions(${log_test} PRIVATE VISIONRT_LOG_TEST_HOOKS=1)
        target_link_libraries(${log_test} PRIVATE Qt6::Test)
        set_tests_properties(${log_test} PROPERTIES TIMEOUT 60)
    endforeach()
    visionrt_contract(display_storage_contract visionrt_presentation)
    visionrt_contract(display_conversion_benchmark visionrt_presentation
        --output "${CMAKE_BINARY_DIR}/evidence/display-conversion-smoke.csv" LABELS perf)
    visionrt_contract(roi_view_benchmark VisionRT::Contracts
        --output "${CMAKE_BINARY_DIR}/evidence/roi-view-smoke.csv" LABELS perf)
    visionrt_contract(output_value_contract visionrt_presentation)
    visionrt_contract(python_value_contract visionrt_python_values)
    # The probe is the only source of Python facts; its positive and negative samples run without a
    # window so a rejected environment is a test result rather than a manual exercise.
    visionrt_contract(python_probe_contract visionrt_python_host LABELS bridge)
    target_compile_definitions(python_probe_contract PRIVATE
        VISIONRT_TEST_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}")
    visionrt_contract(value_bridge_benchmark visionrt_presentation --output "${CMAKE_BINARY_DIR}/evidence/value_bridge-smoke.csv" LABELS perf)
    target_link_libraries(value_bridge_benchmark PRIVATE visionrt_python_values)
    if(WIN32)
        target_link_libraries(value_bridge_benchmark PRIVATE psapi)
    endif()
    visionrt_contract(python_loading_contract visionrt_python_testing)
    visionrt_contract(python_adapter_contract visionrt_python_testing)
    visionrt_contract(python_shutdown_contract visionrt_python_testing)
    visionrt_contract(tool_discovery_contract visionrt_core "$<TARGET_FILE:visionrt_validate_tool>")
    add_dependencies(tool_discovery_contract visionrt_validate_tool)
    visionrt_contract(project_editor_contract visionrt_core)
    visionrt_contract(logic_node_execution_test visionrt_runtime)
    visionrt_contract(edge_hierarchy_contract visionrt_core)
    target_link_libraries(edge_hierarchy_contract PRIVATE visionrt_runtime)
    visionrt_contract(project_storage_contract visionrt_core)
    visionrt_contract(project_value_contract visionrt_core)
    visionrt_contract(cpu_tools_contract visionrt_core "${VISIONRT_TEST_TOOLS_PACKAGE}")
    target_link_libraries(cpu_tools_contract PRIVATE ${OpenCV_LIBS})
    visionrt_contract(example_metadata_contract visionrt_core "${VISIONRT_TEST_TOOLS_PACKAGE}" "${CMAKE_SOURCE_DIR}/examples")
    add_executable(gpu_tools_contract tests/gpu_tools_contract.cpp)
    target_link_libraries(gpu_tools_contract PRIVATE visionrt_runtime)
    visionrt_contract(python_tools_execution_test visionrt_python_testing "${VISIONRT_TEST_TOOLS_PACKAGE}")
    visionrt_contract(installed_pipeline_contract visionrt_runtime "${VISIONRT_TEST_TOOLS_PACKAGE}" LABELS installed)
    target_link_libraries(installed_pipeline_contract PRIVATE visionrt_python_testing)
    visionrt_contract(installed_python_author_contract visionrt_runtime "${VISIONRT_TEST_TOOLS_PACKAGE}" LABELS installed)
    target_link_libraries(installed_python_author_contract PRIVATE visionrt_python_testing ${OpenCV_LIBS})
    visionrt_contract(runtime_contract visionrt_runtime "${VISIONRT_TEST_TOOLS_PACKAGE}")
    visionrt_contract(runtime_node_activity_contract visionrt_runtime)
    visionrt_contract(node_activity_mailbox_test visionrt_runtime)
    visionrt_contract(runtime_python_lazy_contract visionrt_runtime)
    target_link_libraries(runtime_python_lazy_contract PRIVATE visionrt_python_testing)
    visionrt_contract(runtime_diagnostics_contract visionrt_runtime)
    visionrt_contract(runtime_value_contract visionrt_runtime)
    visionrt_contract(result_readback_barrier_test visionrt_runtime)

    if(VISIONRT_BUILD_APP)
        add_executable(libavoid_contract tests/libavoid_contract.cpp)
        target_link_libraries(libavoid_contract PRIVATE visionrt_avoid)
        add_test(NAME libavoid_contract COMMAND libavoid_contract)
        visionrt_apply_test_environment(libavoid_contract)
        visionrt_contract(wire_router_contract visionrt_ui)
        visionrt_contract(wire_routing_benchmark visionrt_ui --output "${CMAKE_BINARY_DIR}/evidence/routing-smoke.csv")
        # R50: these scenarios execute Python, so the test states the build interpreter.
        target_link_libraries(wire_routing_benchmark PRIVATE visionrt_python_testing)
        target_link_libraries(wire_routing_benchmark PRIVATE Qt6::Test)
        target_compile_definitions(wire_routing_benchmark PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
        visionrt_contract(viewport_contract visionrt_viewport)
        visionrt_contract(viewport_lifetime_contract visionrt_viewport)
        target_link_libraries(viewport_lifetime_contract PRIVATE Qt6::Test)
        visionrt_contract(ui_value_contract visionrt_ui)
        # R50: this scenario executes Python, so the test states the build interpreter.
        target_link_libraries(ui_value_contract PRIVATE visionrt_python_testing)
        target_link_libraries(ui_value_contract PRIVATE Qt6::Test)
        visionrt_contract(ui_receiver_identity_contract visionrt_ui)
        # R50: these scenarios execute Python, so the test states the build interpreter.
        target_link_libraries(ui_receiver_identity_contract PRIVATE visionrt_python_testing)
        target_link_libraries(ui_receiver_identity_contract PRIVATE Qt6::Test)
        target_compile_definitions(ui_receiver_identity_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
        visionrt_contract(ui_notifications_contract visionrt_ui)
        # R50: these scenarios execute Python, so the test states the build interpreter.
        target_link_libraries(ui_notifications_contract PRIVATE visionrt_python_testing)
        target_link_libraries(ui_notifications_contract PRIVATE Qt6::Test)
        target_compile_definitions(ui_notifications_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
        # The node list model has no window: it must not take the desktop lock or join the ui group.
        visionrt_contract(node_list_model_test visionrt_ui LABELS backend)
        target_link_libraries(node_list_model_test PRIVATE Qt6::Test)
        visionrt_contract(tool_port_list_model_test visionrt_ui LABELS backend)
        target_link_libraries(tool_port_list_model_test PRIVATE Qt6::Test)
        # What the result view may expose is decided without a window, so the visibility rule is
        # checked here instead of through a rendered frame.
        visionrt_contract(presentation_visibility_contract visionrt_ui LABELS backend)
        # The environment verdict is host-owned: one status, one reason per failure, and a check
        # that never blocks the caller. The controller is the only entry point, so this walks the
        # real settings path without opening a window.
        visionrt_contract(python_environment_contract visionrt_ui LABELS backend)
        target_link_libraries(python_environment_contract PRIVATE Qt6::Test)
        target_compile_definitions(python_environment_contract PRIVATE
            VISIONRT_TEST_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}"
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
        visionrt_contract(tool_editor_controller_test visionrt_ui LABELS backend)
        target_link_libraries(tool_editor_controller_test PRIVATE Qt6::Test)
        # One complete authoring flow through the real dialog: open, add a tool and a parameter,
        # click Save, and read the declaration the window wrote.
        visionrt_contract(tool_editor_dialog_test visionrt_ui)
        target_link_libraries(tool_editor_dialog_test PRIVATE Qt6::Test)
        target_compile_definitions(tool_editor_dialog_test PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
        visionrt_contract(ui_project_paths_contract visionrt_ui)
        # R50: this scenario executes Python, so the test states the build interpreter.
        target_link_libraries(ui_project_paths_contract PRIVATE visionrt_python_testing)
        target_link_libraries(ui_project_paths_contract PRIVATE Qt6::Test)
        target_compile_definitions(ui_project_paths_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
        visionrt_contract(ui_unc_paths_contract visionrt_ui)
        # R50: these scenarios execute Python, so the test states the build interpreter.
        target_link_libraries(ui_unc_paths_contract PRIVATE visionrt_python_testing)
        target_link_libraries(ui_unc_paths_contract PRIVATE Qt6::Test)
        target_compile_definitions(ui_unc_paths_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
        set_tests_properties(ui_unc_paths_contract PROPERTIES SKIP_RETURN_CODE 77)
        visionrt_contract(ui_completion_contract visionrt_ui)
    target_link_libraries(ui_completion_contract PRIVATE visionrt_python_testing)
        target_link_libraries(ui_completion_contract PRIVATE Qt6::Test)
        target_compile_definitions(ui_completion_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
        visionrt_contract(output_text_readback_test visionrt_ui)
        # R50: these scenarios execute Python, so the test states the build interpreter.
        target_link_libraries(output_text_readback_test PRIVATE visionrt_python_testing)
        target_link_libraries(output_text_readback_test PRIVATE Qt6::Test)
        target_compile_definitions(output_text_readback_test PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
        visionrt_contract(display_reader_drain_test visionrt_ui)
        target_link_libraries(display_reader_drain_test PRIVATE Qt6::Test)
        visionrt_contract(assist_viewport_contract visionrt_ui)
        target_link_libraries(assist_viewport_contract PRIVATE Qt6::Test)
        visionrt_contract(workbench_expression_contract visionrt_ui)
        # R50: these scenarios execute Python, so the test states the build interpreter.
        target_link_libraries(workbench_expression_contract PRIVATE visionrt_python_testing)
        target_link_libraries(workbench_expression_contract PRIVATE Qt6::Test)
        target_compile_definitions(workbench_expression_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
        # Reuse of the Python instance is a correctness property, not a measurement: keeping it out
        # of the perf group leaves that group as benchmarks only, which is what the docs promise.
        visionrt_contract(runtime_hotpath_contract visionrt_ui LABELS bridge)
        target_link_libraries(runtime_hotpath_contract PRIVATE Qt6::Test visionrt_python_testing)
        target_compile_definitions(runtime_hotpath_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
        visionrt_contract(workbench_flows_contract visionrt_ui)
        target_link_libraries(workbench_flows_contract PRIVATE Qt6::Test)
        target_compile_definitions(workbench_flows_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
        visionrt_contract(workbench_clipboard_contract visionrt_ui)
        # R50: these scenarios execute Python, so the test states the build interpreter.
        target_link_libraries(workbench_clipboard_contract PRIVATE visionrt_python_testing)
        target_link_libraries(workbench_clipboard_contract PRIVATE Qt6::Test)
        target_compile_definitions(workbench_clipboard_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
        visionrt_contract(workbench_status_contract visionrt_ui)
        # R50: these scenarios execute Python, so the test states the build interpreter.
        target_link_libraries(workbench_status_contract PRIVATE visionrt_python_testing)
        target_link_libraries(workbench_status_contract PRIVATE Qt6::Test)
        target_compile_definitions(workbench_status_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
        visionrt_contract(workbench_interaction_contract visionrt_ui)
        # R50: these scenarios execute Python, so the test states the build interpreter.
        target_link_libraries(workbench_interaction_contract PRIVATE visionrt_python_testing)
        target_link_libraries(workbench_interaction_contract PRIVATE Qt6::Test)
        target_compile_definitions(workbench_interaction_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
        set_tests_properties(workbench_interaction_contract PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        set_property(TEST workbench_interaction_contract APPEND PROPERTY ENVIRONMENT "QT_QUICK_CONTROLS_STYLE=Basic")
        visionrt_contract(workbench_assist_release_contract visionrt_ui)
        target_link_libraries(workbench_assist_release_contract PRIVATE Qt6::Test)
        visionrt_contract(workbench_assist_contract visionrt_ui)
        # R50: these scenarios execute Python, so the test states the build interpreter.
        target_link_libraries(workbench_assist_contract PRIVATE visionrt_python_testing)
        target_link_libraries(workbench_assist_contract PRIVATE Qt6::Test)
        target_compile_definitions(workbench_assist_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
        visionrt_contract(workbench_tooltree_contract visionrt_ui)
        # R50: these scenarios execute Python, so the test states the build interpreter.
        target_link_libraries(workbench_tooltree_contract PRIVATE visionrt_python_testing)
        target_link_libraries(workbench_tooltree_contract PRIVATE Qt6::Test)
        target_compile_definitions(workbench_tooltree_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
        visionrt_contract(workbench_unavailable_tool_contract visionrt_ui)
        # R50: these scenarios execute Python, so the test states the build interpreter.
        target_link_libraries(workbench_unavailable_tool_contract PRIVATE visionrt_python_testing)
        target_link_libraries(workbench_unavailable_tool_contract PRIVATE Qt6::Test)
        target_compile_definitions(workbench_unavailable_tool_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
        set_property(TEST workbench_unavailable_tool_contract APPEND PROPERTY
            ENVIRONMENT "QT_QUICK_CONTROLS_STYLE=Basic")
        visionrt_contract(workbench_publication_contract visionrt_ui)
        # R50: these scenarios execute Python, so the test states the build interpreter.
        target_link_libraries(workbench_publication_contract PRIVATE visionrt_python_testing)
        target_link_libraries(workbench_publication_contract PRIVATE Qt6::Test)
        target_compile_definitions(workbench_publication_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
        visionrt_contract(workbench_layout_contract visionrt_ui)
        target_link_libraries(workbench_layout_contract PRIVATE Qt6::Test)
        target_compile_definitions(workbench_layout_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
        visionrt_contract(workbench_lifecycle_contract visionrt_ui)
        # R50: these scenarios execute Python, so the test states the build interpreter.
        target_link_libraries(workbench_lifecycle_contract PRIVATE visionrt_python_testing)
        target_link_libraries(workbench_lifecycle_contract PRIVATE Qt6::Test)
        target_compile_definitions(workbench_lifecycle_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
        visionrt_contract(ui_result_isolation_contract visionrt_ui)
        target_link_libraries(ui_result_isolation_contract PRIVATE Qt6::Test)
        target_compile_definitions(ui_result_isolation_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
        visionrt_contract(recent_projects_contract visionrt_ui)
        visionrt_contract(ui_preferences_contract visionrt_ui)
        # Example recognition, resource export and the installed-example save rule, at controller
        # level: the shipped assets are checked by example_metadata_contract instead.
        visionrt_contract(example_project_contract visionrt_ui "${VISIONRT_TEST_TOOLS_PACKAGE}")
        target_link_libraries(example_project_contract PRIVATE Qt6::Test)
        visionrt_contract(ui_storage_contract visionrt_ui)
        # R50: this scenario executes Python, so the test states the build interpreter.
        target_link_libraries(ui_storage_contract PRIVATE visionrt_python_testing)
        target_link_libraries(ui_storage_contract PRIVATE Qt6::Test)
        if(WIN32)
            target_link_libraries(ui_storage_contract PRIVATE psapi)
        endif()
        # One runner, one CTest entry per scenario: the aggregate entry used to run the same
        # scenarios again after the focused entries had already covered them. The source list is
        # explicit so a scenario file can be named for its responsibility, not for the target.
        visionrt_contract(workbench_contract visionrt_ui NO_TEST SOURCES
            tests/workbench_contract.cpp
            tests/workbench_parameters.cpp
            tests/workbench_parameter_states.cpp
            tests/flow_canvas_scenarios.cpp
            tests/workbench_cancellation.cpp
            tests/workbench_startup.cpp
            tests/workbench_paths.cpp
            tests/workbench_popups.cpp
            tests/workbench_roi.cpp)
        target_link_libraries(workbench_contract PRIVATE Qt6::Test visionrt_python_testing)
        add_dependencies(workbench_contract test_plugin_module)
        set(VISIONRT_WORKBENCH_SCENARIOS core roi output parameters paths canvas startup catalog popups
            output-reuse cancellation short-window)
        foreach(_scenario IN LISTS VISIONRT_WORKBENCH_SCENARIOS)
            add_test(NAME workbench_${_scenario}_contract COMMAND workbench_contract --${_scenario}-child)
            visionrt_apply_test_environment(workbench_${_scenario}_contract)
            set_tests_properties(workbench_${_scenario}_contract PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
            set_property(TEST workbench_${_scenario}_contract APPEND PROPERTY ENVIRONMENT "QT_QUICK_CONTROLS_STYLE=Basic")
        endforeach()
        if(VISIONRT_TEST_VISUAL)
            foreach(_dpi IN ITEMS 1.0 1.5 2.0)
                string(REPLACE "." "_" _dpi_name "${_dpi}")
                add_test(NAME workbench_dpi_${_dpi_name}_contract COMMAND workbench_contract --dpi-child ${_dpi})
                visionrt_apply_test_environment(workbench_dpi_${_dpi_name}_contract)
                set_tests_properties(workbench_dpi_${_dpi_name}_contract
                    PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
                set_property(TEST workbench_dpi_${_dpi_name}_contract APPEND PROPERTY
                    ENVIRONMENT "QT_QUICK_CONTROLS_STYLE=Basic")
            endforeach()
        endif()
        target_compile_definitions(workbench_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")

        add_executable(performance_contract tests/performance_contract.cpp)
        target_link_libraries(performance_contract PRIVATE visionrt_ui visionrt_runtime visionrt_python_testing opencv_core Qt6::Quick)
        target_compile_definitions(performance_contract PRIVATE
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
        add_test(NAME performance_contract COMMAND performance_contract --evidence-dir "${CMAKE_BINARY_DIR}/evidence")
        visionrt_apply_test_environment(performance_contract)
        if(MSVC)
            target_compile_options(performance_contract PRIVATE /UNDEBUG)
        endif()
    endif()

    # RT-09: 通用插件模块加载测试动态库与契约测试
    add_library(test_plugin_module SHARED tests/test_plugin_module.cpp)
    target_link_libraries(test_plugin_module PRIVATE VisionRT::Contracts)
    target_compile_definitions(test_plugin_module PRIVATE VISIONRT_CONTRACTS_EXPORTS)
    set_target_properties(test_plugin_module PROPERTIES PREFIX "" OUTPUT_NAME test_plugin_module)

    # VR5-02: 真实传递依赖测试动态库
    add_library(test_dep_a SHARED tests/fixtures/test_dep_a.cpp)
    set_target_properties(test_dep_a PROPERTIES PREFIX "" OUTPUT_NAME test_dep_a
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fixtures"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/fixtures"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/fixtures")

    add_library(test_dep_b SHARED tests/fixtures/test_dep_b.cpp)
    set_target_properties(test_dep_b PROPERTIES PREFIX "" OUTPUT_NAME test_dep_b
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fixtures"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/fixtures"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/fixtures")

    add_library(test_plugin_a SHARED tests/fixtures/test_plugin_a.cpp)
    target_link_libraries(test_plugin_a PRIVATE VisionRT::Contracts test_dep_a)
    target_compile_definitions(test_plugin_a PRIVATE VISIONRT_CONTRACTS_EXPORTS)
    set_target_properties(test_plugin_a PROPERTIES PREFIX "" OUTPUT_NAME test_plugin_a
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fixtures"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/fixtures"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/fixtures")

    add_library(test_plugin_b SHARED tests/fixtures/test_plugin_b.cpp)
    target_link_libraries(test_plugin_b PRIVATE VisionRT::Contracts test_dep_b)
    target_compile_definitions(test_plugin_b PRIVATE VISIONRT_CONTRACTS_EXPORTS)
    set_target_properties(test_plugin_b PROPERTIES PREFIX "" OUTPUT_NAME test_plugin_b
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fixtures"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/fixtures"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/fixtures")

    # Negative fixture, not dead code: it publishes an older ABI so the loader's refusal is exercised.
    add_library(test_plugin_legacy SHARED tests/fixtures/incompatible_abi_plugin.cpp)
    target_link_libraries(test_plugin_legacy PRIVATE VisionRT::Contracts)
    target_compile_definitions(test_plugin_legacy PRIVATE VISIONRT_CONTRACTS_EXPORTS)
    set_target_properties(test_plugin_legacy PROPERTIES PREFIX "" OUTPUT_NAME test_plugin_legacy)

    foreach(_registration_fixture IN ITEMS valid duplicate short null collision)
        add_library(test_registration_${_registration_fixture} SHARED tests/fixtures/test_plugin_registration.cpp)
        target_link_libraries(test_registration_${_registration_fixture} PRIVATE VisionRT::Contracts)
        target_compile_definitions(test_registration_${_registration_fixture} PRIVATE VISIONRT_CONTRACTS_EXPORTS)
    endforeach()
    target_compile_definitions(test_registration_duplicate PRIVATE VISIONRT_DUPLICATE_REGISTRATION)
    target_compile_definitions(test_registration_short PRIVATE VISIONRT_SHORT_MODULE)
    target_compile_definitions(test_registration_null PRIVATE VISIONRT_NULL_REGISTRATION)
    visionrt_contract(module_registration_contract visionrt_core
        "$<TARGET_FILE:test_registration_valid>" "$<TARGET_FILE:test_registration_duplicate>"
        "$<TARGET_FILE:test_registration_short>" "$<TARGET_FILE:test_registration_null>"
        "$<TARGET_FILE:test_registration_collision>")
    add_dependencies(module_registration_contract test_registration_valid test_registration_duplicate
        test_registration_short test_registration_null test_registration_collision)

    add_library(test_plugin_value_lifetime SHARED tests/fixtures/test_plugin_value_lifetime.cpp)
    target_link_libraries(test_plugin_value_lifetime PRIVATE VisionRT::Contracts)
    target_compile_definitions(test_plugin_value_lifetime PRIVATE VISIONRT_CONTRACTS_EXPORTS)
    visionrt_contract(native_value_lifetime_contract visionrt_core "$<TARGET_FILE:test_plugin_value_lifetime>")
    add_dependencies(native_value_lifetime_contract test_plugin_value_lifetime)
    visionrt_contract(python_array_lifetime_contract visionrt_python_values "$<TARGET_FILE:test_plugin_value_lifetime>")
    add_dependencies(python_array_lifetime_contract test_plugin_value_lifetime)

    add_executable(plugin_contract tests/plugin_contract.cpp)
    if(VISIONRT_BUILD_APP)
        target_link_libraries(plugin_contract PRIVATE visionrt_ui)
        target_compile_definitions(plugin_contract PRIVATE VISIONRT_BUILD_APP=1)
    else()
        target_link_libraries(plugin_contract PRIVATE visionrt_core visionrt_runtime visionrt_python_testing Qt6::Core Qt6::Gui)
    endif()
    add_dependencies(plugin_contract test_plugin_module test_plugin_a test_plugin_b test_dep_a test_dep_b test_plugin_legacy)
    add_test(NAME plugin_contract COMMAND plugin_contract
        "$<TARGET_FILE:test_plugin_module>"
        "$<TARGET_FILE:test_plugin_a>"
        "$<TARGET_FILE:test_dep_a>"
        "$<TARGET_FILE:test_plugin_b>"
        "$<TARGET_FILE:test_dep_b>")
    visionrt_apply_test_environment(plugin_contract)
    add_test(NAME plugin_abi_contract COMMAND plugin_contract --abi-child "$<TARGET_FILE:test_plugin_legacy>")
    visionrt_apply_test_environment(plugin_abi_contract)
    if(WIN32)
        add_test(NAME plugin_late_runtime_contract COMMAND plugin_contract --late-runtime-child
            "$<TARGET_FILE:test_plugin_a>" "$<TARGET_FILE:test_dep_a>" "$<TARGET_FILE:test_dep_b>")
        visionrt_apply_test_environment(plugin_late_runtime_contract)
    endif()
    if(MSVC)
        target_compile_options(plugin_contract PRIVATE /UNDEBUG)
    endif()

    add_executable(example_pipelines_contract tests/example_pipelines.cpp)
    target_link_libraries(example_pipelines_contract PRIVATE visionrt_core visionrt_runtime visionrt_python_testing Qt6::Core)
    add_dependencies(example_pipelines_contract test_plugin_module test_plugin_a test_dep_a)
    target_compile_definitions(example_pipelines_contract PRIVATE VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME example_pipelines_contract COMMAND example_pipelines_contract --tools-package "${VISIONRT_TEST_TOOLS_PACKAGE}" "${CMAKE_SOURCE_DIR}")
    visionrt_apply_test_environment(example_pipelines_contract)
    if(MSVC)
        target_compile_options(example_pipelines_contract PRIVATE /UNDEBUG)
    endif()

    # Package replacement is a deployment transaction, not an example pipeline: it is registered
    # with the package group and builds its own temporary tools package.
    visionrt_contract(package_replacement_test visionrt_runtime
        SOURCES tests/package_replacement_test.cpp LABELS package)
    target_link_libraries(package_replacement_test PRIVATE visionrt_python_testing Qt6::Core)
    add_dependencies(package_replacement_test test_plugin_module)

    # Entries registered outside visionrt_contract go through the same table: one owning group per
    # test, the build prerequisites ctest needs, and no property writes for tests that a
    # configuration does not create (BUILD_APP=OFF has no workbench entries at all).
    visionrt_group_test(plugin_contract LABELS backend REQUIRES plugin_contract)
    visionrt_group_test(plugin_abi_contract LABELS backend REQUIRES plugin_contract)
    if(TEST plugin_late_runtime_contract)
        visionrt_group_test(plugin_late_runtime_contract LABELS backend REQUIRES plugin_contract)
    endif()
    visionrt_group_test(example_pipelines_contract LABELS backend REQUIRES example_pipelines_contract)

    if(VISIONRT_BUILD_APP)
        foreach(_scenario IN LISTS VISIONRT_WORKBENCH_SCENARIOS)
            visionrt_group_test(workbench_${_scenario}_contract LABELS ui REQUIRES workbench_contract)
        endforeach()
        if(VISIONRT_TEST_VISUAL)
            foreach(_dpi_name IN ITEMS 1_0 1_5 2_0)
                visionrt_group_test(workbench_dpi_${_dpi_name}_contract LABELS ui REQUIRES workbench_contract)
            endforeach()
        endif()
    endif()
    if(TEST libavoid_contract)
        visionrt_group_test(libavoid_contract LABELS ui REQUIRES libavoid_contract)
    endif()
    if(TEST performance_contract)
        visionrt_group_test(performance_contract LABELS perf REQUIRES performance_contract)
    endif()
    # The package group runs the real validator, so the validator and the two fixture modules it
    # loads are build prerequisites of the group, not of the individual invocation.
    visionrt_group_test(validator_cli_contract LABELS package
        REQUIRES visionrt_validate_tool test_registration_valid test_plugin_legacy)
    visionrt_group_test(package_app_contract LABELS package)
    visionrt_group_test(release_evidence_contract LABELS package)
    visionrt_group_test(build_metadata_integration_contract LABELS package integration)
    visionrt_group_test(dependency_manifest_contract LABELS package)
    if(TEST build_incremental_contract)
        visionrt_group_test(build_incremental_contract LABELS package)
    endif()

    # SDK group: the Contracts subproject registers its own suite (VISIONRT_CONTRACTS_TESTS), labels
    # it `sdk` and publishes its members; the host only adds the build entry and the group audit
    # sees the real membership.
    get_property(_contract_tests GLOBAL PROPERTY VISIONRT_CONTRACTS_REGISTERED_TESTS)
    if(NOT _contract_tests)
        # An explicit opt-out is reported, not silently swallowed: the group audit only accepts a
        # missing group when the configuration said so.
        list(APPEND VISIONRT_DISABLED_TEST_GROUPS sdk)
    endif()
    foreach(_sdk_test IN LISTS _contract_tests)
        set_property(GLOBAL APPEND PROPERTY VISIONRT_GROUP_MEMBERS_sdk ${_sdk_test})
        if(TARGET ${_sdk_test})
            set_target_properties(${_sdk_test} PROPERTIES EXCLUDE_FROM_ALL TRUE)
            add_dependencies(tests-sdk ${_sdk_test})
        endif()
    endforeach()

    if(TARGET gpu_tools_contract)
        add_test(NAME gpu_tools_contract COMMAND gpu_tools_contract
            "${VISIONRT_TEST_TOOLS_PACKAGE}" "${CMAKE_SOURCE_DIR}/examples/04_扩展示例/03_GPU目标检测/GPU目标检测.vrt.json"
            "${VISIONRT_GPU_FIXTURES}")
        visionrt_apply_test_environment(gpu_tools_contract)
        # Without a CUDA device or the external YOLO fixtures the case is reported as not run; it
        # must never look like a pass. A real device failure still fails the test.
        set_tests_properties(gpu_tools_contract PROPERTIES LABELS "gpu" SKIP_RETURN_CODE 77)
        set_property(GLOBAL APPEND PROPERTY VISIONRT_GROUP_MEMBERS_gpu gpu_tools_contract)
        add_dependencies(tests-gpu gpu_tools_contract)

        # Converting weights rebuilds a TensorRT engine and can take tens of minutes; checking that
        # an existing engine still infers correctly is a separate, cheap capability. It shares the
        # binary but not the failure domain, and reports NOT RUN when no engine was ever built.
        add_test(NAME gpu_inference_contract COMMAND gpu_tools_contract
            "${VISIONRT_TEST_TOOLS_PACKAGE}" "${CMAKE_SOURCE_DIR}/examples/04_扩展示例/03_GPU目标检测/GPU目标检测.vrt.json"
            "${VISIONRT_GPU_FIXTURES}" --engine-only)
        visionrt_apply_test_environment(gpu_inference_contract)
        set_tests_properties(gpu_inference_contract PROPERTIES LABELS "gpu" SKIP_RETURN_CODE 77)
        set_property(GLOBAL APPEND PROPERTY VISIONRT_GROUP_MEMBERS_gpu gpu_inference_contract)
        add_dependencies(tests-gpu gpu_tools_contract)
    endif()

    foreach(_executable IN ITEMS plugin_contract example_pipelines_contract libavoid_contract performance_contract
            gpu_tools_contract)
        if(TARGET ${_executable})
            set_target_properties(${_executable} PROPERTIES EXCLUDE_FROM_ALL TRUE)
            add_dependencies(tests-all ${_executable})
        endif()
    endforeach()

    visionrt_audit_test_groups()

endif()

# 打印整体配置看板
include(PrintConfig)
