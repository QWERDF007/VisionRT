include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/ConfigDependencyDefaults.cmake")

visionrt_resolve_path(_visionrt_opencv_home
    VARIABLES OpenCV_DIR OpenCV_HOME OpenCV_ROOT
    ENVIRONMENT_VARIABLES OpenCV_DIR OpenCV_HOME OpenCV_ROOT
    PATHS ${CMAKE_PREFIX_PATH}
    REQUIRED_FILE "lib/OpenCVConfig.cmake"
    DEPENDENCY opencv
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

# Only the runtime libraries the binary actually links: a world build ships its single world DLL,
# a component build ships exactly the imported modules. Copying every DLL in the vendor folder used
# to drag debug builds and unused modules into the release.
set(OPENCV_DLLS "")
set(_visionrt_opencv_modules "")
if(TARGET opencv_world)
    set(_visionrt_opencv_modules opencv_world)
else()
    foreach(_visionrt_module IN ITEMS core imgproc imgcodecs)
        if(TARGET opencv_${_visionrt_module})
            list(APPEND _visionrt_opencv_modules opencv_${_visionrt_module})
        endif()
    endforeach()
endif()
foreach(_visionrt_target IN LISTS _visionrt_opencv_modules)
    set(_visionrt_implib "")
    foreach(_visionrt_config IN ITEMS RELEASE RELWITHDEBINFO MINSIZEREL NOCONFIG "")
        if(_visionrt_implib)
            continue()
        endif()
        if(_visionrt_config)
            get_target_property(_visionrt_candidate ${_visionrt_target} IMPORTED_IMPLIB_${_visionrt_config})
            if(NOT _visionrt_candidate)
                get_target_property(_visionrt_candidate ${_visionrt_target} IMPORTED_LOCATION_${_visionrt_config})
            endif()
        else()
            get_target_property(_visionrt_candidate ${_visionrt_target} IMPORTED_IMPLIB)
            if(NOT _visionrt_candidate)
                get_target_property(_visionrt_candidate ${_visionrt_target} IMPORTED_LOCATION)
            endif()
        endif()
        if(_visionrt_candidate AND NOT _visionrt_candidate MATCHES "-NOTFOUND$")
            set(_visionrt_implib "${_visionrt_candidate}")
        endif()
    endforeach()
    if(NOT _visionrt_implib)
        message(FATAL_ERROR "无法确定 OpenCV 目标 ${_visionrt_target} 的运行时库路径；拒绝按目录通配复制 DLL")
    endif()
    get_filename_component(_visionrt_stem "${_visionrt_implib}" NAME_WE)
    set(_visionrt_runtime "${VISIONRT_OPENCV_BIN}/${_visionrt_stem}.dll")
    if(NOT EXISTS "${_visionrt_runtime}")
        message(FATAL_ERROR "OpenCV 运行时库不存在：${_visionrt_runtime}")
    endif()
    list(APPEND OPENCV_DLLS "${_visionrt_runtime}")
endforeach()
list(REMOVE_DUPLICATES OPENCV_DLLS)
