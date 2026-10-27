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

# Runtime libraries are resolved per build configuration: a Debug build must deploy the debug
# import library's DLL and a Release build the release one. Multi-config generators resolve the
# manifest per configuration through $<CONFIG> below.
set(_visionrt_opencv_config "${CMAKE_BUILD_TYPE}")
if(NOT _visionrt_opencv_config)
    set(_visionrt_opencv_config "Release")
endif()
string(TOUPPER "${_visionrt_opencv_config}" _visionrt_opencv_config)
if(NOT _visionrt_opencv_config MATCHES "^(DEBUG|RELEASE|RELWITHDEBINFO|MINSIZEREL)$")
    set(_visionrt_opencv_config "RELEASE")
endif()

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

if(NOT WIN32)
    # Nothing to derive: the platform loader resolves shared objects by SONAME and no deployment
    # step may guess a ".dll" name.
    return()
endif()

if(EXISTS "${OpenCV_DIR}/../bin")
    file(REAL_PATH "${OpenCV_DIR}/../bin" VISIONRT_OPENCV_BIN)
elseif(EXISTS "${OpenCV_DIR}/bin")
    file(REAL_PATH "${OpenCV_DIR}/bin" VISIONRT_OPENCV_BIN)
elseif(DEFINED _visionrt_opencv_home AND EXISTS "${_visionrt_opencv_home}/bin")
    file(REAL_PATH "${_visionrt_opencv_home}/bin" VISIONRT_OPENCV_BIN)
endif()

foreach(_visionrt_target IN LISTS _visionrt_opencv_modules)
    set(_visionrt_implib "")
    get_target_property(_visionrt_candidate ${_visionrt_target} IMPORTED_IMPLIB_${_visionrt_opencv_config})
    if(NOT _visionrt_candidate OR _visionrt_candidate MATCHES "-NOTFOUND$")
        get_target_property(_visionrt_candidate ${_visionrt_target} IMPORTED_LOCATION_${_visionrt_opencv_config})
    endif()
    if((NOT _visionrt_candidate OR _visionrt_candidate MATCHES "-NOTFOUND$")
       AND NOT CMAKE_CONFIGURATION_TYPES)
        get_target_property(_visionrt_candidate ${_visionrt_target} IMPORTED_IMPLIB)
        if(NOT _visionrt_candidate OR _visionrt_candidate MATCHES "-NOTFOUND$")
            get_target_property(_visionrt_candidate ${_visionrt_target} IMPORTED_LOCATION)
        endif()
    endif()
    if(_visionrt_candidate AND NOT _visionrt_candidate MATCHES "-NOTFOUND$")
        set(_visionrt_implib "${_visionrt_candidate}")
    endif()
    if(NOT _visionrt_implib)
        message(FATAL_ERROR
            "无法确定 OpenCV 目标 ${_visionrt_target} 的 ${_visionrt_opencv_config} 运行时库；"
            "拒绝按目录通配或改用其他配置的 DLL")
    endif()
    get_filename_component(_visionrt_stem "${_visionrt_implib}" NAME_WE)
    set(_visionrt_runtime "${VISIONRT_OPENCV_BIN}/${_visionrt_stem}.dll")
    if(NOT EXISTS "${_visionrt_runtime}")
        message(FATAL_ERROR "OpenCV ${_visionrt_opencv_config} 运行时库不存在：${_visionrt_runtime}")
    endif()
    list(APPEND OPENCV_DLLS "${_visionrt_runtime}")
endforeach()
list(REMOVE_DUPLICATES OPENCV_DLLS)

# The exact per-configuration runtime set consumed by tools/package_app.py. The packaging script
# reads this list instead of scanning the SDK folder.
if(_visionrt_opencv_modules)
    set(_visionrt_opencv_genex "")
    foreach(_visionrt_target IN LISTS _visionrt_opencv_modules)
        list(APPEND _visionrt_opencv_genex "$<TARGET_FILE:${_visionrt_target}>")
    endforeach()
    string(JOIN "\n" _visionrt_opencv_manifest ${_visionrt_opencv_genex})
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/opencv_runtime_$<CONFIG>.txt"
         CONTENT "${_visionrt_opencv_manifest}\n")
endif()
