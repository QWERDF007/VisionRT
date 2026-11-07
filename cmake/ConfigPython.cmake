include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/ConfigDependencyDefaults.cmake")

visionrt_resolve_path(_visionrt_python_root
    VARIABLES Python3_ROOT_DIR PYTHON_ROOT
    ENVIRONMENT_VARIABLES Python3_ROOT_DIR PYTHON_ROOT
    REQUIRED_FILE "include/Python.h"
    DEPENDENCY python
)

if(_visionrt_python_root)
    set(Python3_ROOT_DIR "${_visionrt_python_root}" CACHE PATH "Python3 installation root" FORCE)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Embed)

set(PYBIND11_FINDPYTHON ON)
add_subdirectory(3rdparty/pybind11 EXCLUDE_FROM_ALL)

# 构建树需要与开发解释器匹配的 DLL 才能运行测试；发行产物不包含解释器，运行期环境由用户选择。
# VISIONRT_PYTHON_BIN 只用于测试进程的环境（PATH/PYTHONHOME），不是给应用找解释器的入口。
get_filename_component(VISIONRT_BUILD_PYTHON_BIN "${Python3_EXECUTABLE}" DIRECTORY)
set(VISIONRT_PYTHON_BIN "${VISIONRT_BUILD_PYTHON_BIN}")
file(GLOB PYTHON_DLLS "${VISIONRT_BUILD_PYTHON_BIN}/python3*.dll" "${VISIONRT_BUILD_PYTHON_BIN}/zlib*.dll")
