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

find_package(Python3 REQUIRED COMPONENTS Development.Embed)

set(PYBIND11_NOPYTHON ON)
add_subdirectory(3rdparty/pybind11 EXCLUDE_FROM_ALL)

# 构建树使用 CPython 开发资源配套的 DLL；发行产物不包含解释器，运行期环境由用户选择。
# VISIONRT_PYTHON_BIN 只用于测试进程的环境（PATH/PYTHONHOME），不是给应用找解释器的入口。
if(_visionrt_python_root)
    set(VISIONRT_BUILD_PYTHON_BIN "${_visionrt_python_root}")
else()
    list(GET Python3_INCLUDE_DIRS 0 _visionrt_python_include)
    get_filename_component(VISIONRT_BUILD_PYTHON_BIN "${_visionrt_python_include}" DIRECTORY)
endif()
set(VISIONRT_PYTHON_BIN "${VISIONRT_BUILD_PYTHON_BIN}")
if(WIN32)
    set(_test_python "${VISIONRT_BUILD_PYTHON_BIN}/python.exe")
else()
    set(_test_python "${VISIONRT_BUILD_PYTHON_BIN}/bin/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}")
endif()
if(DEFINED ENV{VISIONRT_TEST_PYTHON_EXECUTABLE})
    set(_test_python "$ENV{VISIONRT_TEST_PYTHON_EXECUTABLE}")
endif()
set(VISIONRT_TEST_PYTHON_EXECUTABLE "${_test_python}" CACHE FILEPATH "Interpreter used only when running Python tests")
file(GLOB PYTHON_DLLS "${VISIONRT_BUILD_PYTHON_BIN}/python3*.dll" "${VISIONRT_BUILD_PYTHON_BIN}/zlib*.dll")
