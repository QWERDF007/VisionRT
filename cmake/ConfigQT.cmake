include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/ConfigDependencyDefaults.cmake")

visionrt_resolve_path(_visionrt_qt_root
    VARIABLES Qt6_ROOT QT6_ROOT Qt_ROOT QT_ROOT QTDIR
    ENVIRONMENT_VARIABLES Qt6_ROOT QT_ROOT QTDIR
    PATHS ${CMAKE_PREFIX_PATH}
    REQUIRED_FILE "lib/cmake/Qt6/Qt6Config.cmake"
    DEPENDENCY qt
)

if(_visionrt_qt_root)
    set(Qt6_ROOT "${_visionrt_qt_root}" CACHE PATH "Qt6 installation root directory" FORCE)
    set(Qt6_ROOT "${_visionrt_qt_root}")
    list(PREPEND CMAKE_PREFIX_PATH "${_visionrt_qt_root}")
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.5)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

qt_policy(SET QTP0001 NEW)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8.0)
    qt_policy(SET QTP0004 NEW)
endif()

get_target_property(VISIONRT_QT_CORE Qt6::Core IMPORTED_LOCATION_RELEASE)
if(NOT VISIONRT_QT_CORE)
    get_target_property(VISIONRT_QT_CORE Qt6::Core LOCATION)
endif()
if(VISIONRT_QT_CORE)
    get_filename_component(VISIONRT_QT_BIN "${VISIONRT_QT_CORE}" DIRECTORY)
endif()

find_program(VISIONRT_WINDEPLOYQT windeployqt HINTS "${VISIONRT_QT_BIN}" REQUIRED)
