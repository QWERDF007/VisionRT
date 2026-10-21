include_guard(GLOBAL)

set(_libavoid_dir "${CMAKE_SOURCE_DIR}/3rdparty/adaptagrams/cola/libavoid")
set(_libavoid_sources
    actioninfo.cpp
    connectionpin.cpp
    connector.cpp
    connend.cpp
    geometry.cpp
    geomtypes.cpp
    graph.cpp
    hyperedge.cpp
    hyperedgeimprover.cpp
    hyperedgetree.cpp
    junction.cpp
    makepath.cpp
    mtst.cpp
    obstacle.cpp
    orthogonal.cpp
    router.cpp
    scanline.cpp
    shape.cpp
    timer.cpp
    vertices.cpp
    viscluster.cpp
    visibility.cpp
    vpsc.cpp)
list(TRANSFORM _libavoid_sources PREPEND "${_libavoid_dir}/")
add_library(visionrt_avoid STATIC ${_libavoid_sources})
target_include_directories(visionrt_avoid SYSTEM PUBLIC "${CMAKE_SOURCE_DIR}/3rdparty/adaptagrams/cola")
target_compile_definitions(visionrt_avoid PUBLIC LIBAVOID_NO_DLL)
