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

# 核心通用库
add_library(visionrt_common
    src/common/Value.cpp
    src/common/Version.cpp)
target_include_directories(visionrt_common PUBLIC src ${OpenCV_INCLUDE_DIRS})
target_link_libraries(visionrt_common PUBLIC ${OpenCV_LIBS})
configure_version(visionrt_common VISIONRT visionrt/common ${PROJECT_VERSION})

# 核心业务逻辑库
add_library(visionrt_core
    src/core/Tool.cpp
    src/core/ValueYaml.cpp
    src/core/ToolRegistry.cpp
    src/core/ProjectEditor.cpp
    src/core/ProjectStorage.cpp)
target_link_libraries(visionrt_core PUBLIC visionrt_common Qt6::Core yaml-cpp::yaml-cpp)

# CPU 内置工具组件库
add_library(visionrt_tools src/tools/CpuTools.cpp)
target_link_libraries(visionrt_tools PUBLIC visionrt_core)

# Python 运行时及交互桥接库
add_library(visionrt_python src/python/PythonRuntime.cpp src/python/PythonBridge.cpp)
target_link_libraries(visionrt_python PUBLIC visionrt_core pybind11::embed)
target_compile_definitions(visionrt_python PUBLIC
    PYBIND11_DETAILED_ERROR_MESSAGES
    VISIONRT_PYTHON_DEFAULT_HOME="${VISIONRT_PYTHON_HOME}")

# DAG 执行调度引擎库
add_library(visionrt_runtime
    src/runtime/RuntimeEngine.cpp
    src/runtime/NodeRuntime.cpp
    src/runtime/Receiver.cpp)
target_link_libraries(visionrt_runtime PUBLIC visionrt_core visionrt_taskflow)

# InferRT GPU 推理插件模块 (可选)
option(VISIONRT_BUILD_INFER "Build the fixed InferRT GPU module" ON)
if(VISIONRT_BUILD_INFER)
    include(ConfigCUDA)
    include(ConfigInferRT)
    add_library(VisionRTInfer SHARED
        src/infer/InferModule.cpp
        src/infer/YoloModel.cpp
        src/infer/YoloPostprocess.cpp)
    target_compile_definitions(VisionRTInfer PRIVATE VISIONRT_INFER_EXPORTS)
    if(DEFINED InferRT_VERSION)
        target_compile_definitions(VisionRTInfer PRIVATE VISIONRT_INFERRT_VERSION="${InferRT_VERSION}")
    endif()
    target_link_libraries(VisionRTInfer PRIVATE visionrt_core InferRT::model CUDA::cudart)
    set_target_properties(VisionRTInfer PROPERTIES PREFIX "" OUTPUT_NAME VisionRTInfer)
endif()

# 桌面 Workbench 应用程序与发布配置
option(VISIONRT_BUILD_APP "Build the desktop workbench" ON)
if(VISIONRT_BUILD_APP)
    add_library(visionrt_ui
        src/ui/AppController.cpp
        src/ui/AppControllerDisplay.cpp
        src/ui/AppControllerMetadata.cpp
        src/ui/AppController.h
        src/ui/ToolTreeModel.cpp
        src/ui/ToolTreeModel.h
        src/ui/OutputModel.cpp
        src/ui/OutputModel.h
        src/ui/ImageViewport.cpp
        src/ui/ImageViewport.h
        src/ui/DisplayConverter.cpp
        src/ui/DisplayConverter.h
        src/infer/InferLoader.cpp)
    target_link_libraries(visionrt_ui PUBLIC
        visionrt_runtime visionrt_python visionrt_tools
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 quickui::quickui)
    target_compile_definitions(visionrt_ui PRIVATE VISIONRT_PYTHON_HOME="${VISIONRT_PYTHON_HOME}")

    qt_add_executable(VisionRT src/main.cpp)
    target_link_libraries(VisionRT PRIVATE visionrt_ui)
    if(EXISTS "${CMAKE_SOURCE_DIR}/docs/VisionRT_V3/examples/tools")
        add_custom_command(TARGET VisionRT POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
                "${CMAKE_SOURCE_DIR}/docs/VisionRT_V3/examples/tools" "$<TARGET_FILE_DIR:VisionRT>/tools")
    endif()

    file(GLOB visionrt_qml CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/ui/qml/*.qml")
    foreach(qml_file IN LISTS visionrt_qml)
        get_filename_component(qml_name "${qml_file}" NAME)
        set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
    endforeach()
    qt_add_qml_module(VisionRT URI VisionRT VERSION 1.0 QML_FILES ${visionrt_qml})

    configure_file(cmake/Deploy.cmake.in "${CMAKE_BINARY_DIR}/Deploy.cmake" @ONLY)
    add_custom_target(deploy
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_BINARY_DIR}/Deploy.cmake"
        DEPENDS VisionRT
        USES_TERMINAL)
    if(VISIONRT_BUILD_INFER)
        add_dependencies(deploy VisionRTInfer)
    endif()
endif()

# 开发与本地调试动态库拷贝目标
set(_visionrt_runtime_dlls ${OPENCV_DLLS} ${PYTHON_DLLS})
if(VISIONRT_BUILD_INFER AND INFERRT_DLLS)
    list(APPEND _visionrt_runtime_dlls ${INFERRT_DLLS})
endif()
if(_visionrt_runtime_dlls)
    add_custom_target(deploy_runtime_dlls ALL
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${_visionrt_runtime_dlls} "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMENT "Deploying runtime DLLs to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    )
endif()

# 测试契约与自动化测试用例
if(BUILD_TESTING)
    include(CTest)
    include(AddTest)

    add_executable(version_contract tests/version_contract.cpp)
    target_link_libraries(version_contract PRIVATE visionrt_common)
    add_test(NAME version_contract COMMAND version_contract)
    visionrt_apply_test_environment(version_contract)

    add_executable(value_contract tests/value_contract.cpp)
    target_link_libraries(value_contract PRIVATE visionrt_common)
    add_test(NAME value_contract COMMAND value_contract)
    visionrt_apply_test_environment(value_contract)

    add_executable(path_contract tests/path_contract.cpp)
    target_link_libraries(path_contract PRIVATE visionrt_core)
    add_test(NAME path_contract COMMAND path_contract)
    visionrt_apply_test_environment(path_contract)

    visionrt_contract(tool_registry_contract visionrt_core "${CMAKE_SOURCE_DIR}/docs/VisionRT_V3/examples/tools")
    visionrt_contract(project_editor_contract visionrt_core)
    visionrt_contract(project_storage_contract visionrt_core "${CMAKE_SOURCE_DIR}/docs/VisionRT_V3/examples/tools")
    visionrt_contract(cpu_tools_contract visionrt_tools "${CMAKE_SOURCE_DIR}/docs/VisionRT_V3/examples/tools")
    visionrt_contract(python_contract visionrt_python "${CMAKE_SOURCE_DIR}/docs/VisionRT_V3/examples/tools")
    visionrt_contract(runtime_contract visionrt_runtime "${CMAKE_SOURCE_DIR}/docs/VisionRT_V3/examples/tools")

    add_executable(infer_contract tests/infer_contract.cpp src/infer/YoloPostprocess.cpp)
    target_link_libraries(infer_contract PRIVATE visionrt_common)
    add_test(NAME infer_contract COMMAND infer_contract)
    visionrt_apply_test_environment(infer_contract)
    if(MSVC)
        target_compile_options(infer_contract PRIVATE /UNDEBUG)
    endif()

    if(VISIONRT_BUILD_APP)
        visionrt_contract(viewport_contract visionrt_ui)
        visionrt_contract(workbench_contract visionrt_ui)
    endif()
endif()

# 打印整体配置看板
include(PrintConfig)
