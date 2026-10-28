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

# The runtime set is taken from the imported targets themselves: `$<TARGET_FILE:...>` resolves to
# the library the linker consumed for the configuration being built, so a Debug deploy can never
# copy the Release DLL and a multi-config generator needs no default. The development copy target
# and any packaging list read the same variable.
set(VISIONRT_OPENCV_RUNTIME_DLLS "")
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
    # Nothing to deploy: the platform loader resolves shared objects by SONAME and no script may
    # guess a ".dll" name.
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
    list(APPEND VISIONRT_OPENCV_RUNTIME_DLLS "$<TARGET_FILE:${_visionrt_target}>")
endforeach()

if(_visionrt_opencv_modules)
    string(JOIN "\n" _visionrt_opencv_manifest ${VISIONRT_OPENCV_RUNTIME_DLLS})
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/opencv_runtime_$<CONFIG>.txt"
         CONTENT "${_visionrt_opencv_manifest}\n")
endif()
