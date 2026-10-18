cmake_minimum_required(VERSION 3.25)
project(SdkConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(VisionRTToolSDK CONFIG REQUIRED)

add_executable(sdk_consumer main.cpp)
target_link_libraries(sdk_consumer PRIVATE VisionRT::ToolSDK)
