visionrt_add_module(runtime SOURCES RuntimeEngine.cpp NodeRuntime.cpp Receiver.cpp
    PUBLIC_LIBRARIES visionrt_core
    PRIVATE_LIBRARIES visionrt_taskflow)
