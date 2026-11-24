include_guard(GLOBAL)
include(GenerateExportHeader)

function(visionrt_module_logging target module)
    string(TOUPPER "${module}" module_upper)
    set(export_macro VISIONRT_${module_upper}_API)
    configure_file("${CMAKE_SOURCE_DIR}/cmake/ModuleLogging.h.in"
        "include/${module}/Logging.h" @ONLY)
    configure_file("${CMAKE_SOURCE_DIR}/cmake/ModuleLogging.cpp.in"
        "${target}_logging.cpp" @ONLY)
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${target}_logging.cpp")
    target_link_libraries(${target} PRIVATE spdlog::spdlog)
endfunction()

function(visionrt_add_module module)
    cmake_parse_arguments(ARG "" "" "SOURCES;PUBLIC_LIBRARIES;PRIVATE_LIBRARIES" ${ARGN})
    set(target visionrt_${module})
    file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*")
    set_source_files_properties(${headers} PROPERTIES HEADER_FILE_ONLY TRUE)
    add_library(${target} SHARED ${ARG_SOURCES} ${headers})
    string(TOUPPER "${module}" module_upper)
    set(export_header "${CMAKE_CURRENT_BINARY_DIR}/include/${module}/Export.h")
    generate_export_header(${target}
        EXPORT_MACRO_NAME VISIONRT_${module_upper}_API
        EXPORT_FILE_NAME "${export_header}")
    visionrt_module_logging(${target} ${module})
    target_include_directories(${target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${target} PUBLIC ${ARG_PUBLIC_LIBRARIES} PRIVATE ${ARG_PRIVATE_LIBRARIES})
    set_target_properties(${target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
    install(TARGETS ${target}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT dev)
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT dev)
    install(FILES "${export_header}" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${module} COMPONENT dev)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/${module}/Logging.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${module} COMPONENT dev)
    set_property(GLOBAL APPEND PROPERTY VISIONRT_MODULE_TARGETS ${target})
    set(metadata "$<TARGET_FILE_BASE_NAME:${target}>-build-info.json")
    set_property(TARGET ${target} PROPERTY VISIONRT_BUILD_METADATA "${metadata}")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -DCOMPONENT=${target}
            -DVERSION=${PROJECT_VERSION}${PROJECT_VERSION_SUFFIX}
            -DREPO_DIR=${CMAKE_SOURCE_DIR}
            -DSOURCE_FILE=$<TARGET_FILE:${target}>
            -DOUTPUT=$<TARGET_FILE_DIR:${target}>/${metadata}
            -DRELEASE_BUILD=${VISIONRT_RELEASE_BUILD}
            -P "${CMAKE_SOURCE_DIR}/cmake/WriteBuildInfo.cmake"
        VERBATIM)
    install(FILES "$<TARGET_FILE_DIR:${target}>/${metadata}"
        DESTINATION ${CMAKE_INSTALL_BINDIR})
endfunction()

function(visionrt_write_module_catalog)
    get_property(targets GLOBAL PROPERTY VISIONRT_MODULE_TARGETS)
    set(entries "")
    foreach(target IN LISTS targets)
        get_target_property(metadata ${target} VISIONRT_BUILD_METADATA)
        list(APPEND entries "{\"component\":\"${target}\",\"library\":\"$<TARGET_FILE_NAME:${target}>\",\"metadata\":\"${metadata}\"}")
    endforeach()
    list(JOIN entries ",\n    " entries)
    set(catalog "${CMAKE_BINARY_DIR}/module-catalog/$<CONFIG>/visionrt-modules.json")
    file(GENERATE OUTPUT "${catalog}" CONTENT "{\n  \"schema_version\":1,\n  \"modules\":[\n    ${entries}\n  ]\n}\n")
    add_custom_target(visionrt_module_catalog ALL
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${catalog}"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/visionrt-modules.json"
        VERBATIM)
    foreach(target IN LISTS targets)
        add_dependencies(${target} visionrt_module_catalog)
    endforeach()
    install(FILES "${catalog}" DESTINATION ${CMAKE_INSTALL_BINDIR})
endfunction()
