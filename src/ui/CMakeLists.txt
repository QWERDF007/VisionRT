include(ConfigLibavoid)
visionrt_add_module(ui SOURCES
    UiTypes.cpp
    Color.cpp
    Size.cpp
    DurationText.cpp
    WireRouter.cpp
    AppController.cpp
    AppControllerAssist.cpp
    AboutReport.cpp
    UiPreferences.cpp
    RecentProjects.cpp
    AppControllerBindings.cpp
    AppControllerDisplay.cpp
    AppControllerReadback.cpp
    AppControllerRoi.cpp
    NodeListModel.cpp
    ResultPresentation.cpp
    RoiEditSession.cpp
    TrainingSession.cpp
    TrainingAssets.cpp
    AppControllerTraining.cpp
    ProjectOperations.cpp
    AppControllerMetadata.cpp
    AppControllerExpression.cpp
    AppControllerParameters.cpp
    AppControllerRouting.cpp
    AssistPreview.cpp
    AppControllerDiagnostics.cpp
    LogHighlighter.cpp
    ToolTreeModel.cpp
    ToolPortListModel.cpp
    ToolEditorController.cpp
    DisplayConverter.cpp
    DisplayTypes.cpp
    OutputModel.cpp
    Diagnostics.cpp
    ImageViewport.cpp
    ImageViewportRoi.cpp
    RoiOverlay.cpp
    PUBLIC_LIBRARIES visionrt_runtime visionrt_python visionrt_plugins visionrt_tools visionrt_log
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 quickui::quickui
    PRIVATE_LIBRARIES visionrt_avoid)
set(_ui_enum_headers
    "${CMAKE_SOURCE_DIR}/src/ui/include/ui/UiState.h"
    "${CMAKE_SOURCE_DIR}/3rdparty/VisionRTContracts/include/visionrt/contracts/ProtocolEnums.h"
    "${CMAKE_SOURCE_DIR}/3rdparty/VisionRTContracts/include/visionrt/contracts/TypeSpec.h"
    "${CMAKE_SOURCE_DIR}/3rdparty/VisionRTContracts/include/visionrt/contracts/ROI.h"
    "${CMAKE_SOURCE_DIR}/src/runtime/include/runtime/RuntimeEngine.h"
    "${CMAKE_SOURCE_DIR}/src/runtime/include/runtime/Snapshot.h"
    "${CMAKE_SOURCE_DIR}/src/python/include/python/PythonEnvironment.h"
    "${CMAKE_SOURCE_DIR}/src/log/include/log/LogLevel.h")
set(_ui_enums "${CMAKE_CURRENT_BINARY_DIR}/include/UiEnums.h")
add_custom_command(OUTPUT "${_ui_enums}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/3rdparty/VisionRTContracts/tools/export_enums.py"
        --qml "${_ui_enums}" ${_ui_enum_headers}
    DEPENDS ${_ui_enum_headers} "${CMAKE_SOURCE_DIR}/3rdparty/VisionRTContracts/tools/export_enums.py"
    VERBATIM)
target_sources(visionrt_ui PRIVATE "${_ui_enums}")
target_include_directories(visionrt_ui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/include")
file(GLOB visionrt_qml CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/ui/qml/*.qml")
foreach(qml_file IN LISTS visionrt_qml)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()
qt_add_qml_module(visionrt_ui NO_PLUGIN URI VisionRT VERSION 1.0 OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/VisionRT" DEPENDENCIES quickui QML_FILES ${visionrt_qml})
