include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/ConfigDependencyDefaults.cmake")

visionrt_resolve_path(_visionrt_opencv_home
    VARIABLES OpenCV_DIR OpenCV_HOME OpenCV_ROOT
    ENVIRONMENT_VARIABLES OpenCV_DIR OpenCV_HOME OpenCV_ROOT
    PATHS ${CMAKE_PREFIX_PATH}
    REQUIRED_FILE "lib/OpenCVConfig.cmake"
    DEFAULT "D:/Software/dev/opencv-4.8.0/build/x64/vc16"
)

if(_visionrt_opencv_home)
    foreach(_candidate IN ITEMS
            "${_visionrt_opencv_home}/lib"
            "${_visionrt_opencv_home}/lib/cmake/opencv4"
            "${_visionrt_opencv_home}")
        if(EXISTS "${_candidate}/OpenCVConfig.cmake")
            set(OpenCV_DIR "${_candidate}" CACHE PATH "OpenCV CMake package directory" FORCE)
            set(OpenCV_DIR "${_candidate}")
            break()
        endif()
    endforeach()
    list(PREPEND CMAKE_PREFIX_PATH "${_visionrt_opencv_home}")
endif()

find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc imgcodecs)

# 解析 OpenCV 运行时 DLL 目录
if(EXISTS "${OpenCV_DIR}/../bin")
    file(REAL_PATH "${OpenCV_DIR}/../bin" VISIONRT_OPENCV_BIN)
elseif(EXISTS "${OpenCV_DIR}/bin")
    file(REAL_PATH "${OpenCV_DIR}/bin" VISIONRT_OPENCV_BIN)
elseif(DEFINED _visionrt_opencv_home AND EXISTS "${_visionrt_opencv_home}/bin")
    file(REAL_PATH "${_visionrt_opencv_home}/bin" VISIONRT_OPENCV_BIN)
endif()

file(GLOB OPENCV_DLLS "${VISIONRT_OPENCV_BIN}/*.dll")
