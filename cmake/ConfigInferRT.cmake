include_guard(GLOBAL)

if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/ConfigDependencyDefaults.cmake")

visionrt_resolve_path(_visionrt_inferrt_root
    VARIABLES INFERRT_ROOT InferRT_ROOT INFERRT_DIR InferRT_DIR
    ENVIRONMENT_VARIABLES INFERRT_ROOT InferRT_ROOT INFERRT_DIR InferRT_DIR
    DEFAULT "F:/Projects/InferRT"
)

if(_visionrt_inferrt_root)
    set(INFERRT_ROOT "${_visionrt_inferrt_root}" CACHE PATH "InferRT installation root" FORCE)
    set(INFERRT_ROOT "${_visionrt_inferrt_root}")

    file(GLOB _installed_versions LIST_DIRECTORIES true "${_visionrt_inferrt_root}/InferRT-*")
    set(_candidates)
    foreach(_ver_dir IN LISTS _installed_versions)
        list(APPEND _candidates "${_ver_dir}/lib/cmake/InferRT")
    endforeach()
    list(APPEND _candidates
        "${_visionrt_inferrt_root}/lib/cmake/InferRT"
        "${_visionrt_inferrt_root}/lib/cmake"
        "${_visionrt_inferrt_root}/cmake"
        "${_visionrt_inferrt_root}/build"
        "${_visionrt_inferrt_root}"
    )
    foreach(_candidate IN LISTS _candidates)
        if(EXISTS "${_candidate}/InferRTConfig.cmake")
            set(InferRT_DIR "${_candidate}" CACHE PATH "InferRT CMake package directory" FORCE)
            set(InferRT_DIR "${_candidate}")
            break()
        endif()
    endforeach()
    list(PREPEND CMAKE_PREFIX_PATH "${_visionrt_inferrt_root}")
endif()

find_package(InferRT CONFIG REQUIRED COMPONENTS model)

if(InferRT_FOUND)
    # 计算 SDK 根目录与 bin 目录
    if(InferRT_DIR MATCHES "/lib/cmake/InferRT$")
        file(REAL_PATH "${InferRT_DIR}/../../.." VISIONRT_INFERRT_ROOT)
    elseif(DEFINED INFERRT_ROOT)
        set(VISIONRT_INFERRT_ROOT "${INFERRT_ROOT}")
    endif()

    if(EXISTS "${VISIONRT_INFERRT_ROOT}/bin")
        set(INFERRT_BIN_DIR "${VISIONRT_INFERRT_ROOT}/bin")
    elseif(EXISTS "${INFERRT_ROOT}/build/bin")
        set(INFERRT_BIN_DIR "${INFERRT_ROOT}/build/bin")
    endif()

    if(INFERRT_BIN_DIR)
        file(GLOB INFERRT_DLLS "${INFERRT_BIN_DIR}/inferrt_*.dll")
    endif()
endif()
