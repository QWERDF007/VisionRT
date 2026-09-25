cmake_minimum_required(VERSION 3.25)
project(VisionRT VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# 引入构建树与编译器配置，参考 InferRT/cmake，输出在 build/bin
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ConfigBuildTree)
include(ConfigCompiler)
include(ConfigVersion)

include(CTest)
find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc imgcodecs)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "Disable yaml-cpp tests" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "Disable yaml-cpp tools" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "Disable yaml-cpp contrib" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "Build static yaml-cpp" FORCE)
set(YAML_MSVC_SHARED_RT ON CACHE BOOL "MSVC shared runtime" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "Disable yaml-cpp install" FORCE)
add_subdirectory(3rdparty/yaml-cpp EXCLUDE_FROM_ALL)
if(NOT DEFINED Python3_ROOT_DIR AND NOT DEFINED ENV{PYTHON_ROOT})
    set(Python3_ROOT_DIR "E:/Softwares/Anaconda3/envs/py312")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Embed)
set(PYBIND11_FINDPYTHON ON)
add_subdirectory(3rdparty/pybind11 EXCLUDE_FROM_ALL)

add_library(visionrt_taskflow INTERFACE)
target_include_directories(visionrt_taskflow INTERFACE 3rdparty/taskflow)

add_library(visionrt_common
    src/common/Value.cpp
    src/common/Version.cpp)
target_include_directories(visionrt_common PUBLIC src ${OpenCV_INCLUDE_DIRS})
target_link_libraries(visionrt_common PUBLIC ${OpenCV_LIBS})
configure_version(visionrt_common VISIONRT visionrt/common ${PROJECT_VERSION})

add_library(visionrt_core
    src/core/Tool.cpp
    src/core/ValueYaml.cpp
    src/core/ToolRegistry.cpp
    src/core/ProjectEditor.cpp
    src/core/ProjectStorage.cpp)
target_link_libraries(visionrt_core PUBLIC visionrt_common Qt6::Core yaml-cpp::yaml-cpp)

add_library(visionrt_tools src/tools/CpuTools.cpp)
target_link_libraries(visionrt_tools PUBLIC visionrt_core)

add_library(visionrt_python src/python/PythonRuntime.cpp src/python/PythonBridge.cpp)
target_link_libraries(visionrt_python PUBLIC visionrt_core pybind11::embed)
get_filename_component(VISIONRT_PYTHON_HOME "${Python3_EXECUTABLE}" DIRECTORY)
target_compile_definitions(visionrt_python PUBLIC
    PYBIND11_DETAILED_ERROR_MESSAGES
    VISIONRT_PYTHON_DEFAULT_HOME="${VISIONRT_PYTHON_HOME}")

add_library(visionrt_runtime
    src/runtime/RuntimeEngine.cpp
    src/runtime/NodeRuntime.cpp
    src/runtime/Receiver.cpp)
target_link_libraries(visionrt_runtime PUBLIC visionrt_core visionrt_taskflow)

option(VISIONRT_BUILD_INFER "Build the fixed InferRT GPU module" OFF)
if(VISIONRT_BUILD_INFER)
    find_package(InferRT CONFIG REQUIRED COMPONENTS model)
    add_library(VisionRTInfer SHARED
        src/infer/InferModule.cpp src/infer/YoloModel.cpp src/infer/YoloPostprocess.cpp)
    target_compile_definitions(VisionRTInfer PRIVATE VISIONRT_INFER_EXPORTS)
    target_link_libraries(VisionRTInfer PRIVATE visionrt_core InferRT::model CUDA::cudart)
    set_target_properties(VisionRTInfer PROPERTIES PREFIX "" OUTPUT_NAME VisionRTInfer)
    file(REAL_PATH "${InferRT_DIR}/../../.." VISIONRT_INFERRT_ROOT)
endif()

option(VISIONRT_BUILD_APP "Build the desktop workbench" ON)
if(VISIONRT_BUILD_APP)
find_package(quickui CONFIG REQUIRED)
qt_standard_project_setup(REQUIRES 6.5)
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
get_filename_component(VISIONRT_PYTHON_HOME "${Python3_EXECUTABLE}" DIRECTORY)
target_compile_definitions(visionrt_ui PRIVATE VISIONRT_PYTHON_HOME="${VISIONRT_PYTHON_HOME}")

qt_add_executable(VisionRT src/main.cpp)
target_link_libraries(VisionRT PRIVATE visionrt_ui)
add_custom_command(TARGET VisionRT POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
        "${CMAKE_SOURCE_DIR}/docs/VisionRT_V3/examples/tools" "$<TARGET_FILE_DIR:VisionRT>/tools")
file(GLOB visionrt_qml CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/ui/qml/*.qml")
foreach(qml_file IN LISTS visionrt_qml)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()
qt_add_qml_module(VisionRT URI VisionRT VERSION 1.0 QML_FILES ${visionrt_qml})

get_target_property(VISIONRT_QUICKUI_DLL quickui::quickui IMPORTED_LOCATION_RELEASE)
get_filename_component(VISIONRT_QUICKUI_MODULE "${VISIONRT_QUICKUI_DLL}" DIRECTORY)
get_filename_component(VISIONRT_QUICKUI_QML_ROOT "${VISIONRT_QUICKUI_MODULE}" DIRECTORY)
get_target_property(VISIONRT_QT_CORE Qt6::Core IMPORTED_LOCATION_RELEASE)
get_filename_component(VISIONRT_QT_BIN "${VISIONRT_QT_CORE}" DIRECTORY)
find_program(VISIONRT_WINDEPLOYQT windeployqt HINTS "${VISIONRT_QT_BIN}" REQUIRED)
file(REAL_PATH "${OpenCV_DIR}/../bin" VISIONRT_OPENCV_BIN)
configure_file(cmake/Deploy.cmake.in "${CMAKE_BINARY_DIR}/Deploy.cmake" @ONLY)
add_custom_target(deploy
    COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_BINARY_DIR}/Deploy.cmake"
    DEPENDS VisionRT
    USES_TERMINAL)
endif()
if(VISIONRT_BUILD_APP AND VISIONRT_BUILD_INFER)
    add_dependencies(deploy VisionRTInfer)
endif()

file(REAL_PATH "${OpenCV_DIR}/../bin" VISIONRT_OPENCV_BIN)
file(GLOB OPENCV_DLLS "${VISIONRT_OPENCV_BIN}/*.dll")
get_filename_component(VISIONRT_PYTHON_BIN "${Python3_EXECUTABLE}" DIRECTORY)
file(GLOB PYTHON_DLLS "${VISIONRT_PYTHON_BIN}/python3*.dll")

add_custom_target(deploy_runtime_dlls ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${OPENCV_DLLS} "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${PYTHON_DLLS} "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    COMMENT "Deploying runtime DLLs to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

get_target_property(VISIONRT_QT_CORE Qt6::Core IMPORTED_LOCATION_RELEASE)
if(NOT VISIONRT_QT_CORE)
    get_target_property(VISIONRT_QT_CORE Qt6::Core LOCATION)
endif()
if(VISIONRT_QT_CORE)
    get_filename_component(VISIONRT_QT_BIN "${VISIONRT_QT_CORE}" DIRECTORY)
endif()

function(visionrt_apply_test_environment TEST_NAME)
    if(WIN32)
        file(TO_NATIVE_PATH "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" _p0)
        file(TO_NATIVE_PATH "${VISIONRT_QT_BIN}" _p1)
        file(TO_NATIVE_PATH "${VISIONRT_OPENCV_BIN}" _p2)
        file(TO_NATIVE_PATH "${VISIONRT_PYTHON_BIN}" _p3)
        file(TO_NATIVE_PATH "${VISIONRT_PYTHON_BIN}/Library/bin" _p4)
        file(TO_NATIVE_PATH "E:/Softwares/Anaconda3/Library/bin" _p5)
        file(TO_NATIVE_PATH "${VISIONRT_PYTHON_BIN}" _pyhome)
        set_tests_properties(${TEST_NAME} PROPERTIES
            ENVIRONMENT "PATH=${_p0}\;${_p1}\;${_p2}\;${_p3}\;${_p4}\;${_p5}\;$ENV{PATH};PYTHONHOME=${_pyhome}"
        )
    endif()
endfunction()

function(visionrt_contract name library)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE ${library})
    if(MSVC)
        target_compile_options(${name} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${name} PRIVATE -UNDEBUG)
    endif()
    add_test(NAME ${name} COMMAND ${name} ${ARGN})
    visionrt_apply_test_environment(${name})
endfunction()

if(BUILD_TESTING)
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
