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

# 独立通用工具 SDK（Git 子模块）
set(VISIONRT_BUILD_PYTHON_INTEROP ON CACHE BOOL "Build SDK Python interop" FORCE)
if(BUILD_TESTING)
    set(VISIONRT_ENABLE_TEST_PROBES ON CACHE BOOL "Enable test probes" FORCE)
    add_compile_definitions(VISIONRT_TEST_PROBES)
endif()
add_subdirectory(3rdparty/VisionRTSDK)

# 核心通用库
add_library(visionrt_common
    src/common/Version.cpp)
target_include_directories(visionrt_common PUBLIC src)
target_link_libraries(visionrt_common PUBLIC VisionRT::ToolSDK)
configure_version(visionrt_common VISIONRT visionrt/common ${PROJECT_VERSION})

# 核心业务逻辑库
add_library(visionrt_core
    src/core/ToolRegistry.cpp
    src/core/ProjectEditor.cpp
    src/core/ProjectStorage.cpp
    src/plugins/PluginLoader.cpp)
target_link_libraries(visionrt_core PUBLIC visionrt_common Qt6::Core yaml-cpp::yaml-cpp)


# Python 运行时及交互桥接库
add_library(visionrt_python src/python/PythonRuntime.cpp)
target_link_libraries(visionrt_python PUBLIC visionrt_core pybind11::embed VisionRT::PythonInterop)
target_compile_definitions(visionrt_python PUBLIC
    VISIONRT_PYTHON_DEFAULT_HOME="${VISIONRT_PYTHON_HOME}")

# DAG 执行调度引擎库
add_library(visionrt_runtime
    src/runtime/RuntimeEngine.cpp
    src/runtime/NodeRuntime.cpp
    src/runtime/Receiver.cpp)
target_link_libraries(visionrt_runtime PUBLIC visionrt_core visionrt_taskflow)

# 桌面 Workbench 应用程序与发布配置
option(VISIONRT_BUILD_APP "Build the desktop workbench" ON)
if(VISIONRT_BUILD_APP)
    add_library(visionrt_ui
        src/ui/Theme.cpp
        src/ui/Theme.h
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
        src/ui/DisplayConverter.h)
    target_link_libraries(visionrt_ui PUBLIC
        visionrt_runtime visionrt_python
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 quickui::quickui)
    target_compile_definitions(visionrt_ui PRIVATE VISIONRT_PYTHON_HOME="${VISIONRT_PYTHON_HOME}")

    qt_add_executable(VisionRT src/main.cpp)
    target_link_libraries(VisionRT PRIVATE visionrt_ui quickui::quickui)

    file(GLOB visionrt_qml CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/ui/qml/*.qml")
    foreach(qml_file IN LISTS visionrt_qml)
        get_filename_component(qml_name "${qml_file}" NAME)
        set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
    endforeach()
    qt_add_qml_module(VisionRT URI VisionRT VERSION 1.0 DEPENDENCIES quickui QML_FILES ${visionrt_qml})

    configure_file(cmake/Deploy.cmake.in "${CMAKE_BINARY_DIR}/Deploy.cmake" @ONLY)
    add_custom_target(deploy
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_BINARY_DIR}/Deploy.cmake"
        DEPENDS VisionRT
        USES_TERMINAL)
endif()

# 开发与本地调试动态库拷贝目标
set(_visionrt_runtime_dlls ${OPENCV_DLLS} ${PYTHON_DLLS})
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

    set(VISIONRT_TEST_TOOLS_PACKAGE "${CMAKE_BINARY_DIR}/tools-package" CACHE PATH "Installed tools package used by integration contracts")

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

    visionrt_contract(tool_registry_contract visionrt_core "${VISIONRT_TEST_TOOLS_PACKAGE}")
    visionrt_contract(project_editor_contract visionrt_core)
    visionrt_contract(project_storage_contract visionrt_core)
    visionrt_contract(cpu_tools_contract visionrt_core "${VISIONRT_TEST_TOOLS_PACKAGE}")
    target_link_libraries(cpu_tools_contract PRIVATE ${OpenCV_LIBS})
    visionrt_contract(python_contract visionrt_python "${VISIONRT_TEST_TOOLS_PACKAGE}")
    visionrt_contract(runtime_contract visionrt_runtime "${VISIONRT_TEST_TOOLS_PACKAGE}")

    if(VISIONRT_BUILD_APP)
        visionrt_contract(viewport_contract visionrt_ui)
        visionrt_contract(workbench_contract visionrt_ui)
        target_compile_definitions(workbench_contract PRIVATE
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")

        add_executable(performance_contract tests/performance_contract.cpp)
        target_link_libraries(performance_contract PRIVATE visionrt_ui visionrt_runtime visionrt_python opencv_core Qt6::Quick)
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
    target_link_libraries(test_plugin_module PRIVATE VisionRT::ToolSDK)
    target_compile_definitions(test_plugin_module PRIVATE VISIONRT_TOOL_SDK_EXPORTS)
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
    target_link_libraries(test_plugin_a PRIVATE VisionRT::ToolSDK test_dep_a)
    target_compile_definitions(test_plugin_a PRIVATE VISIONRT_TOOL_SDK_EXPORTS)
    set_target_properties(test_plugin_a PROPERTIES PREFIX "" OUTPUT_NAME test_plugin_a
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fixtures"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/fixtures"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/fixtures")

    add_library(test_plugin_b SHARED tests/fixtures/test_plugin_b.cpp)
    target_link_libraries(test_plugin_b PRIVATE VisionRT::ToolSDK test_dep_b)
    target_compile_definitions(test_plugin_b PRIVATE VISIONRT_TOOL_SDK_EXPORTS)
    set_target_properties(test_plugin_b PROPERTIES PREFIX "" OUTPUT_NAME test_plugin_b
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fixtures"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/fixtures"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/fixtures")

    add_executable(plugin_contract tests/plugin_contract.cpp)
    if(VISIONRT_BUILD_APP)
        target_link_libraries(plugin_contract PRIVATE visionrt_ui)
        target_compile_definitions(plugin_contract PRIVATE VISIONRT_BUILD_APP=1)
    else()
        target_link_libraries(plugin_contract PRIVATE visionrt_core visionrt_runtime visionrt_python Qt6::Core Qt6::Gui)
    endif()
    add_dependencies(plugin_contract test_plugin_module test_plugin_a test_plugin_b test_dep_a test_dep_b)
    add_test(NAME plugin_contract COMMAND plugin_contract
        "$<TARGET_FILE:test_plugin_module>"
        "$<TARGET_FILE:test_plugin_a>"
        "$<TARGET_FILE:test_dep_a>"
        "$<TARGET_FILE:test_plugin_b>"
        "$<TARGET_FILE:test_dep_b>")
    visionrt_apply_test_environment(plugin_contract)
    if(MSVC)
        target_compile_options(plugin_contract PRIVATE /UNDEBUG)
    endif()

    add_executable(examples_contract tests/examples_contract.cpp)
    target_link_libraries(examples_contract PRIVATE visionrt_core visionrt_runtime visionrt_python Qt6::Core)
    add_dependencies(examples_contract test_plugin_module)
    target_compile_definitions(examples_contract PRIVATE VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME examples_contract COMMAND examples_contract --tools-package "${VISIONRT_TEST_TOOLS_PACKAGE}" "${CMAKE_SOURCE_DIR}")
    visionrt_apply_test_environment(examples_contract)
    if(MSVC)
        target_compile_options(examples_contract PRIVATE /UNDEBUG)
    endif()

endif()

# 打印整体配置看板
include(PrintConfig)
