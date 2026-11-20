visionrt_add_module(common SOURCES Version.cpp
    PUBLIC_LIBRARIES VisionRT::Contracts)
configure_version(visionrt_common VISIONRT common ${PROJECT_VERSION})
