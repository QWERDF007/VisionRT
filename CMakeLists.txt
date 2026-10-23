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
add_subdirectory(3rdparty/VisionRTContracts)

# 核心通用库
add_library(visionrt_common
    src/common/Version.cpp)
target_include_directories(visionrt_common PUBLIC src)
target_link_libraries(visionrt_common PUBLIC VisionRT::Contracts)
configure_version(visionrt_common VISIONRT visionrt/common ${PROJECT_VERSION})

# 核心业务逻辑库
add_library(visionrt_core
    src/core/ToolDefinition.cpp
    src/core/ToolValueValidation.cpp
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
    src/plugins/PluginLoader.cpp)
target_link_libraries(visionrt_core PUBLIC visionrt_common Qt6::Core)

add_executable(visionrt_validate_tool tools/visionrt_validate_tool.cpp)
target_link_libraries(visionrt_validate_tool PRIVATE visionrt_core)


# Python 运行时及交互桥接库
add_library(visionrt_python_values src/python/PythonInterop.cpp src/python/PythonImages.cpp src/python/PythonStorage.cpp)
target_link_libraries(visionrt_python_values PUBLIC visionrt_core pybind11::embed)
add_library(visionrt_python src/python/PythonRuntime.cpp)
target_link_libraries(visionrt_python PUBLIC visionrt_python_values)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/python/managed_packages.py")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/python/managed_packages.py" VISIONRT_PYTHON_LOADER)
configure_file(src/python/PythonLoader.h.in python/PythonLoader.h @ONLY)
target_include_directories(visionrt_python PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/python")
target_compile_definitions(visionrt_python PUBLIC
    PYBIND11_DETAILED_ERROR_MESSAGES
    VISIONRT_PYTHON_DEFAULT_HOME="${VISIONRT_PYTHON_HOME}")

# DAG 执行调度引擎库
add_library(visionrt_runtime
    src/runtime/RuntimeEngine.cpp
    src/runtime/NodeRuntime.cpp
    src/runtime/Receiver.cpp)
target_link_libraries(visionrt_runtime PUBLIC visionrt_core visionrt_taskflow)

# 桌面 Workbench 应用程序与发布配置
add_library(visionrt_presentation
    src/ui/DisplayConverter.cpp src/ui/DisplayConverter.h
    src/ui/OutputModel.cpp src/ui/OutputModel.h
    src/ui/Diagnostics.cpp src/ui/Diagnostics.h)
target_link_libraries(visionrt_presentation PUBLIC visionrt_core Qt6::Gui)

option(VISIONRT_BUILD_APP "Build the desktop workbench" ON)
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
        src/ui/AppControllerBindings.cpp
        src/ui/AppControllerDisplay.cpp
        src/ui/AppControllerReadback.cpp
        src/ui/AppControllerRoi.cpp
        src/ui/AppControllerMetadata.cpp
        src/ui/AppControllerParameters.cpp
        src/ui/AppControllerRouting.cpp
        src/ui/AppController.h
        src/ui/AppControllerDiagnostics.cpp
        src/ui/ToolTreeModel.cpp
        src/ui/ToolTreeModel.h)
    target_link_libraries(visionrt_ui PUBLIC
        visionrt_runtime visionrt_python visionrt_viewport
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 quickui::quickui)
    target_compile_definitions(visionrt_ui PRIVATE VISIONRT_PYTHON_HOME="${VISIONRT_PYTHON_HOME}")
    target_link_libraries(visionrt_ui PRIVATE visionrt_avoid)

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
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

    set(VISIONRT_TEST_TOOLS_PACKAGE "${CMAKE_BINARY_DIR}/tools-package" CACHE PATH "Installed tools package used by integration contracts")

    add_executable(version_contract tests/version_contract.cpp)
    target_link_libraries(version_contract PRIVATE visionrt_common)
    add_test(NAME version_contract COMMAND version_contract)
    visionrt_apply_test_environment(version_contract)

    add_executable(value_contract tests/value_contract.cpp)
    target_link_libraries(value_contract PRIVATE visionrt_core)
    add_test(NAME value_contract COMMAND value_contract)
    visionrt_apply_test_environment(value_contract)

    add_executable(path_contract tests/path_contract.cpp)
    target_link_libraries(path_contract PRIVATE visionrt_core)
    add_test(NAME path_contract COMMAND path_contract)
    visionrt_apply_test_environment(path_contract)

    visionrt_contract(tool_registry_contract visionrt_core)
    visionrt_contract(declaration_values_contract visionrt_core)
    visionrt_contract(definition_json_contract visionrt_core)
    visionrt_contract(declaration_value_contract visionrt_core)
    visionrt_contract(tools_package_contract visionrt_core)
    add_test(NAME validator_cli_contract COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tests/validator_cli_contract.py"
        --validator "$<TARGET_FILE:visionrt_validate_tool>" --opencv-bin "${VISIONRT_OPENCV_BIN}"
        --module "$<TARGET_FILE:test_registration_valid>" --bad-module "$<TARGET_FILE:test_plugin_legacy>")
    visionrt_apply_test_environment(validator_cli_contract)
    add_test(NAME package_app_contract COMMAND "${CMAKE_COMMAND}" -E env
        "VISIONTOOLS_VALIDATOR=$<TARGET_FILE:visionrt_validate_tool>"
        "${Python3_EXECUTABLE}" -S "${CMAKE_SOURCE_DIR}/tests/test_package_app.py")
    visionrt_apply_test_environment(package_app_contract)
    add_test(NAME dependency_manifest_contract COMMAND "${CMAKE_COMMAND}" -E env
        "CMAKE_COMMAND=${CMAKE_COMMAND}" "${Python3_EXECUTABLE}" -S
        "${CMAKE_SOURCE_DIR}/tests/dependency_manifest_contract.py")
    visionrt_apply_test_environment(dependency_manifest_contract)
    if(WIN32)
        add_test(NAME build_incremental_contract COMMAND "${CMAKE_COMMAND}" -E env
            "CMAKE_COMMAND=${CMAKE_COMMAND}" "CMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/build_incremental_contract.py")
    endif()
    visionrt_contract(value_json_contract visionrt_core)
    visionrt_contract(display_storage_contract visionrt_presentation)
    visionrt_contract(output_value_contract visionrt_presentation)
    visionrt_contract(python_value_contract visionrt_python_values)
    visionrt_contract(value_bridge_benchmark visionrt_presentation --output "${CMAKE_BINARY_DIR}/evidence/value_bridge-smoke.csv")
    target_link_libraries(value_bridge_benchmark PRIVATE visionrt_python_values)
    if(WIN32)
        target_link_libraries(value_bridge_benchmark PRIVATE psapi)
    endif()
    visionrt_contract(python_loading_contract visionrt_python)
    visionrt_contract(python_adapter_contract visionrt_python)
    visionrt_contract(python_shutdown_contract visionrt_python)
    visionrt_contract(tool_discovery_contract visionrt_core "$<TARGET_FILE:visionrt_validate_tool>")
    add_dependencies(tool_discovery_contract visionrt_validate_tool)
    visionrt_contract(project_editor_contract visionrt_core)
    visionrt_contract(project_storage_contract visionrt_core)
    visionrt_contract(project_value_contract visionrt_core)
    visionrt_contract(cpu_tools_contract visionrt_core "${VISIONRT_TEST_TOOLS_PACKAGE}")
    target_link_libraries(cpu_tools_contract PRIVATE ${OpenCV_LIBS})
    visionrt_contract(example_metadata_contract visionrt_core "${VISIONRT_TEST_TOOLS_PACKAGE}" "${CMAKE_SOURCE_DIR}/examples")
    add_executable(gpu_tools_contract tests/gpu_tools_contract.cpp)
    target_link_libraries(gpu_tools_contract PRIVATE visionrt_runtime)
    visionrt_contract(python_contract visionrt_python "${VISIONRT_TEST_TOOLS_PACKAGE}")
    visionrt_contract(installed_pipeline_contract visionrt_runtime "${VISIONRT_TEST_TOOLS_PACKAGE}")
    target_link_libraries(installed_pipeline_contract PRIVATE visionrt_python)
    visionrt_contract(runtime_contract visionrt_runtime "${VISIONRT_TEST_TOOLS_PACKAGE}")
    visionrt_contract(runtime_diagnostics_contract visionrt_runtime)
    visionrt_contract(runtime_value_contract visionrt_runtime)
    visionrt_contract(runtime_readback_contract visionrt_runtime)

    if(VISIONRT_BUILD_APP)
        add_executable(libavoid_contract tests/libavoid_contract.cpp)
        target_link_libraries(libavoid_contract PRIVATE visionrt_avoid)
        add_test(NAME libavoid_contract COMMAND libavoid_contract)
        visionrt_apply_test_environment(libavoid_contract)
        visionrt_contract(wire_router_contract visionrt_ui)
        visionrt_contract(wire_routing_benchmark visionrt_ui --output "${CMAKE_BINARY_DIR}/evidence/routing-smoke.csv")
        target_link_libraries(wire_routing_benchmark PRIVATE Qt6::Test)
        target_compile_definitions(wire_routing_benchmark PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
        visionrt_contract(viewport_contract visionrt_viewport)
        visionrt_contract(viewport_lifetime_contract visionrt_viewport)
        target_link_libraries(viewport_lifetime_contract PRIVATE Qt6::Test)
        visionrt_contract(ui_value_contract visionrt_ui)
        target_link_libraries(ui_value_contract PRIVATE Qt6::Test)
        visionrt_contract(ui_receiver_identity_contract visionrt_ui)
        target_link_libraries(ui_receiver_identity_contract PRIVATE Qt6::Test)
        target_compile_definitions(ui_receiver_identity_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
        visionrt_contract(ui_notifications_contract visionrt_ui)
        target_link_libraries(ui_notifications_contract PRIVATE Qt6::Test)
        target_compile_definitions(ui_notifications_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
        visionrt_contract(ui_project_paths_contract visionrt_ui)
        target_link_libraries(ui_project_paths_contract PRIVATE Qt6::Test)
        target_compile_definitions(ui_project_paths_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
        visionrt_contract(ui_unc_paths_contract visionrt_ui)
        target_link_libraries(ui_unc_paths_contract PRIVATE Qt6::Test)
        target_compile_definitions(ui_unc_paths_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
        set_tests_properties(ui_unc_paths_contract PROPERTIES SKIP_RETURN_CODE 77)
        visionrt_contract(ui_completion_contract visionrt_ui)
        target_link_libraries(ui_completion_contract PRIVATE Qt6::Test)
        target_compile_definitions(ui_completion_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
        visionrt_contract(ui_readback_contract visionrt_ui)
        target_link_libraries(ui_readback_contract PRIVATE Qt6::Test)
        target_compile_definitions(ui_readback_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
        visionrt_contract(runtime_hotpath_contract visionrt_ui)
        target_link_libraries(runtime_hotpath_contract PRIVATE Qt6::Test)
        target_compile_definitions(runtime_hotpath_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
        visionrt_contract(workbench_flows_contract visionrt_ui)
        target_link_libraries(workbench_flows_contract PRIVATE Qt6::Test)
        target_compile_definitions(workbench_flows_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
        visionrt_contract(workbench_clipboard_contract visionrt_ui)
        target_link_libraries(workbench_clipboard_contract PRIVATE Qt6::Test)
        target_compile_definitions(workbench_clipboard_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
        visionrt_contract(workbench_status_contract visionrt_ui)
        target_link_libraries(workbench_status_contract PRIVATE Qt6::Test)
        target_compile_definitions(workbench_status_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
        visionrt_contract(workbench_tooltree_contract visionrt_ui)
        target_link_libraries(workbench_tooltree_contract PRIVATE Qt6::Test)
        target_compile_definitions(workbench_tooltree_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
        visionrt_contract(workbench_publication_contract visionrt_ui)
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
        target_link_libraries(workbench_lifecycle_contract PRIVATE Qt6::Test)
        target_compile_definitions(workbench_lifecycle_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
            VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            VISIONRT_BINARY_DIR="${CMAKE_BINARY_DIR}")
        visionrt_contract(ui_result_isolation_contract visionrt_ui)
        target_link_libraries(ui_result_isolation_contract PRIVATE Qt6::Test)
        target_compile_definitions(ui_result_isolation_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}")
        visionrt_contract(ui_storage_contract visionrt_ui)
        target_link_libraries(ui_storage_contract PRIVATE Qt6::Test)
        if(WIN32)
            target_link_libraries(ui_storage_contract PRIVATE psapi)
        endif()
        visionrt_contract(workbench_contract visionrt_ui)
        target_sources(workbench_contract PRIVATE
            tests/workbench_parameters.cpp
            tests/workbench_parameter_states.cpp
            tests/workbench_canvas.cpp
            tests/workbench_cancellation.cpp
            tests/workbench_startup.cpp
            tests/workbench_paths.cpp
            tests/workbench_popups.cpp
            tests/workbench_roi.cpp)
        target_link_libraries(workbench_contract PRIVATE Qt6::Test)
        add_dependencies(workbench_contract test_plugin_module)
        set_tests_properties(workbench_contract PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        set_property(TEST workbench_contract APPEND PROPERTY ENVIRONMENT "QT_QUICK_CONTROLS_STYLE=Basic")
        foreach(_scenario IN ITEMS roi output parameters paths canvas startup catalog popups output-reuse cancellation)
            add_test(NAME workbench_${_scenario}_contract COMMAND workbench_contract --${_scenario}-child)
            visionrt_apply_test_environment(workbench_${_scenario}_contract)
            set_tests_properties(workbench_${_scenario}_contract PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
            set_property(TEST workbench_${_scenario}_contract APPEND PROPERTY ENVIRONMENT "QT_QUICK_CONTROLS_STYLE=Basic")
        endforeach()
        target_compile_definitions(workbench_contract PRIVATE
            VISIONRT_TEST_TOOLS_PACKAGE="${VISIONRT_TEST_TOOLS_PACKAGE}"
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

    add_library(test_plugin_legacy SHARED tests/fixtures/test_plugin_legacy.cpp)
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

    add_executable(plugin_contract tests/plugin_contract.cpp)
    if(VISIONRT_BUILD_APP)
        target_link_libraries(plugin_contract PRIVATE visionrt_ui)
        target_compile_definitions(plugin_contract PRIVATE VISIONRT_BUILD_APP=1)
    else()
        target_link_libraries(plugin_contract PRIVATE visionrt_core visionrt_runtime visionrt_python Qt6::Core Qt6::Gui)
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

    add_executable(examples_contract tests/examples_contract.cpp)
    target_link_libraries(examples_contract PRIVATE visionrt_core visionrt_runtime visionrt_python Qt6::Core)
    add_dependencies(examples_contract test_plugin_module test_plugin_a test_dep_a)
    target_compile_definitions(examples_contract PRIVATE VISIONRT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME examples_contract COMMAND examples_contract --tools-package "${VISIONRT_TEST_TOOLS_PACKAGE}" "${CMAKE_SOURCE_DIR}")
    visionrt_apply_test_environment(examples_contract)
    if(MSVC)
        target_compile_options(examples_contract PRIVATE /UNDEBUG)
    endif()

endif()

# 打印整体配置看板
include(PrintConfig)
