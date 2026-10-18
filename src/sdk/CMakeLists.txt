cmake_minimum_required(VERSION 3.25)
project(VisionRTToolSDK VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT TARGET opencv_core)
    find_package(OpenCV REQUIRED COMPONENTS core)
endif()

if(NOT TARGET yaml-cpp::yaml-cpp AND NOT TARGET yaml-cpp)
    find_package(yaml-cpp QUIET)
    if(NOT TARGET yaml-cpp::yaml-cpp AND NOT TARGET yaml-cpp)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/yaml-cpp/CMakeLists.txt")
            set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "Disable yaml-cpp tests" FORCE)
            set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "Disable yaml-cpp tools" FORCE)
            set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "Disable yaml-cpp contrib" FORCE)
            set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "Build static yaml-cpp" FORCE)
            set(YAML_MSVC_SHARED_RT ON CACHE BOOL "MSVC shared runtime" FORCE)
            set(YAML_CPP_INSTALL ON CACHE BOOL "Enable yaml-cpp install" FORCE)
            add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/yaml-cpp" "${CMAKE_CURRENT_BINARY_DIR}/yaml-cpp")
        else()
            message(FATAL_ERROR "yaml-cpp not found and 3rdparty/yaml-cpp submodule missing")
        endif()
    endif()
endif()

add_library(VisionRTToolSDK STATIC
    src/Value.cpp
    src/Contract.cpp
    src/Schema.cpp
    src/ValueYaml.cpp
    src/SchemaYaml.cpp
)
add_library(VisionRT::ToolSDK ALIAS VisionRTToolSDK)
set_target_properties(VisionRTToolSDK PROPERTIES EXPORT_NAME ToolSDK)

target_include_directories(VisionRTToolSDK
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(VisionRTToolSDK PUBLIC opencv_core yaml-cpp::yaml-cpp)

add_executable(visionrt_validate_tool tools/visionrt_validate_tool.cpp)
target_link_libraries(visionrt_validate_tool PRIVATE VisionRTToolSDK)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS VisionRTToolSDK visionrt_validate_tool
    EXPORT VisionRTToolSDKTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT VisionRTToolSDKTargets
    FILE VisionRTToolSDKTargets.cmake
    NAMESPACE VisionRT::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VisionRTToolSDK
)

configure_package_config_file(
    cmake/VisionRTToolSDKConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/VisionRTToolSDKConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VisionRTToolSDK
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/VisionRTToolSDKConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/VisionRTToolSDKConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/VisionRTToolSDKConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VisionRTToolSDK
)
