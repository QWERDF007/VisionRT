include_guard(GLOBAL)

# yaml-cpp: 静态构建，共享 MSVC 运行时，关闭不必要的测试与工具
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "Disable yaml-cpp tests" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "Disable yaml-cpp tools" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "Disable yaml-cpp contrib" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "Build static yaml-cpp" FORCE)
set(YAML_MSVC_SHARED_RT ON CACHE BOOL "MSVC shared runtime" FORCE)
set(YAML_CPP_INSTALL ON CACHE BOOL "Enable yaml-cpp install" FORCE)
add_subdirectory(3rdparty/yaml-cpp)

# Taskflow: 现代 C++ 头文件并发库
add_library(visionrt_taskflow INTERFACE)
target_include_directories(visionrt_taskflow INTERFACE 3rdparty/taskflow)
