include_guard(GLOBAL)

# Taskflow: 现代 C++ 头文件并发库
add_library(visionrt_taskflow INTERFACE)
target_include_directories(visionrt_taskflow INTERFACE 3rdparty/taskflow)
