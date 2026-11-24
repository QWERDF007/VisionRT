visionrt_add_module(python_bridge SOURCES
    PythonRuntime.cpp PythonStorage.cpp PythonInterop.cpp PythonImages.cpp PythonBridge.cpp
    PUBLIC_LIBRARIES visionrt_python pybind11::headers Python3::Python)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/managed_packages.py")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/managed_packages.py" VISIONRT_PYTHON_LOADER)
configure_file(PythonLoader.h.in PythonLoader.h @ONLY)
target_include_directories(visionrt_python_bridge PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(visionrt_python_bridge PUBLIC PYBIND11_DETAILED_ERROR_MESSAGES)
