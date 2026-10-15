include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/ConfigDependencyDefaults.cmake")

visionrt_resolve_path(_visionrt_cuda_root
    VARIABLES CUDAToolkit_ROOT CUDA_TOOLKIT_ROOT_DIR CUDA_PATH
    ENVIRONMENT_VARIABLES CUDA_PATH CUDA_TOOLKIT_ROOT_DIR CUDAToolkit_ROOT
    DEFAULT "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.8"
)

if(_visionrt_cuda_root AND NOT DEFINED CUDAToolkit_ROOT)
    set(CUDAToolkit_ROOT "${_visionrt_cuda_root}" CACHE PATH "CUDA Toolkit root directory")
endif()

find_package(CUDAToolkit REQUIRED)

if(NOT DEFINED CUDAToolkit_BIN_DIR OR CUDAToolkit_BIN_DIR STREQUAL "")
    set(CUDAToolkit_BIN_DIR "${CUDAToolkit_TARGET_DIR}/bin" CACHE PATH "CUDA Toolkit runtime directory")
endif()

# 统一支持主流与最新 GPU 架构
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES OR CMAKE_CUDA_ARCHITECTURES STREQUAL "")
    set(CMAKE_CUDA_ARCHITECTURES "75;80;86;89;90" CACHE STRING "CUDA architectures to build for")
endif()
