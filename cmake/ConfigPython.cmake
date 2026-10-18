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

get_filename_component(VISIONRT_PYTHON_HOME "${Python3_EXECUTABLE}" DIRECTORY)
set(VISIONRT_PYTHON_BIN "${VISIONRT_PYTHON_HOME}")
file(GLOB PYTHON_DLLS "${VISIONRT_PYTHON_BIN}/python3*.dll" "${VISIONRT_PYTHON_BIN}/zlib*.dll")
