cmake_minimum_required(VERSION 3.25)
project(VisionRTToolSDK VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT TARGET opencv_core)
    find_package(OpenCV REQUIRED COMPONENTS core)
endif()

add_library(VisionRTToolSDK STATIC
    src/Value.cpp
    src/Contract.cpp
)
add_library(VisionRT::ToolSDK ALIAS VisionRTToolSDK)
set_target_properties(VisionRTToolSDK PROPERTIES EXPORT_NAME ToolSDK)

target_include_directories(VisionRTToolSDK
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(VisionRTToolSDK PUBLIC opencv_core)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS VisionRTToolSDK
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
