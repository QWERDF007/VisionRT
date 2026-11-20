set(VISIONRT_LOG_SOURCES LogService.cpp LogLevel.cpp LogModel.cpp)
visionrt_add_module(log SOURCES ${VISIONRT_LOG_SOURCES}
    PUBLIC_LIBRARIES visionrt_common Qt6::Core
    PRIVATE_LIBRARIES spdlog::spdlog)

if(BUILD_TESTING)
    add_library(visionrt_log_testing STATIC ${VISIONRT_LOG_SOURCES}
        include/log/LogService.h include/log/LogLevel.h include/log/LogModel.h)
    target_link_libraries(visionrt_log_testing PUBLIC visionrt_common Qt6::Core spdlog::spdlog)
    target_include_directories(visionrt_log_testing PUBLIC include "${CMAKE_CURRENT_BINARY_DIR}/include")
    target_compile_definitions(visionrt_log_testing PUBLIC VISIONRT_LOG_STATIC_DEFINE
        PRIVATE VISIONRT_LOG_TEST_HOOKS=1)
endif()
