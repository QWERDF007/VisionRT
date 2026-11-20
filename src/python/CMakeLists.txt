visionrt_add_module(python SOURCES PythonBackend.cpp PythonProbe.cpp
    PUBLIC_LIBRARIES visionrt_core Qt6::Core
    PRIVATE_LIBRARIES visionrt_log)
target_compile_definitions(visionrt_python PRIVATE
    VISIONRT_PYTHON_BUILD_MAJOR=${Python3_VERSION_MAJOR}
    VISIONRT_PYTHON_BUILD_MINOR=${Python3_VERSION_MINOR}
    VISIONRT_PYTHON_BRIDGE_FILENAME=L"$<TARGET_FILE_NAME:visionrt_python_bridge>")
target_include_directories(visionrt_python PRIVATE
    "${CMAKE_SOURCE_DIR}/src/python_bridge/include"
    "${CMAKE_BINARY_DIR}/src/python_bridge/include")
