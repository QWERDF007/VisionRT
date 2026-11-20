#include "UiTypes.h"
#include "Color.h"
#include "Size.h"
#include "core/ToolDefinition.h"
#include "runtime/RuntimeEngine.h"
#include "runtime/Snapshot.h"
#include "python/PythonEnvironment.h"
#include "log/LogLevel.h"
#include "UiState.h"
#include "UiEnums.h"
#include <QQmlEngine>
#include <quickui/Color.h>

namespace visionrt {
void registerUiTypes()
{
    static const bool registered = [] {
        qmlRegisterSingletonType<VisionRTColor>("VisionRT.Ui", 1, 0, "VisionRTColor",
            [](QQmlEngine *, QJSEngine *) -> QObject * { return new VisionRTColor; });
        qmlRegisterUncreatableMetaObject(VisionRTSize::staticMetaObject, "VisionRT.Ui", 1, 0,
                                       "VisionRTSize", "Size constants");
        qmlRegisterUncreatableMetaObject(qml::ToolKind::staticMetaObject, "VisionRT.Ui", 1, 0, "ToolKind", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::PortStage::staticMetaObject, "VisionRT.Ui", 1, 0, "PortStage", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::NodeModel::staticMetaObject, "VisionRT.Ui", 1, 0, "NodeModel", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::ToolVisibility::staticMetaObject, "VisionRT.Ui", 1, 0, "ToolVisibility", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::ToolOrigin::staticMetaObject, "VisionRT.Ui", 1, 0, "ToolOrigin", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::TrainingKind::staticMetaObject, "VisionRT.Ui", 1, 0, "TrainingKind", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::EditorKind::staticMetaObject, "VisionRT.Ui", 1, 0, "EditorKind", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::ParameterTab::staticMetaObject, "VisionRT.Ui", 1, 0, "ParameterTab", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::OptionsProvider::staticMetaObject, "VisionRT.Ui", 1, 0, "OptionsProvider", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::ColorOrder::staticMetaObject, "VisionRT.Ui", 1, 0, "ColorOrder", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::Type::staticMetaObject, "VisionRT.Ui", 1, 0, "PortType", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::DType::staticMetaObject, "VisionRT.Ui", 1, 0, "DType", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::RoiKind::staticMetaObject, "VisionRT.Ui", 1, 0, "RoiKind", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::RunMode::staticMetaObject, "VisionRT.Ui", 1, 0, "RunMode", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::RuntimeState::staticMetaObject, "VisionRT.Ui", 1, 0, "RuntimeState", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::RuntimePhase::staticMetaObject, "VisionRT.Ui", 1, 0, "RuntimePhase", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::NodeDisposition::staticMetaObject, "VisionRT.Ui", 1, 0, "NodeDisposition", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::NodePhase::staticMetaObject, "VisionRT.Ui", 1, 0, "NodePhase", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::PythonEnvironmentPhase::staticMetaObject, "VisionRT.Ui", 1, 0, "PythonEnvironmentPhase", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::LogLevel::staticMetaObject, "VisionRT.Ui", 1, 0, "LogLevel", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::NodeState::staticMetaObject, "VisionRT.Ui", 1, 0, "NodeState", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::SettingsPage::staticMetaObject, "VisionRT.Ui", 1, 0, "SettingsPage", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::BindingMode::staticMetaObject, "VisionRT.Ui", 1, 0, "BindingMode", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::CatalogEntryKind::staticMetaObject, "VisionRT.Ui", 1, 0, "CatalogEntryKind", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::RelationKind::staticMetaObject, "VisionRT.Ui", 1, 0, "RelationKind", "Enum values");
        qmlRegisterUncreatableMetaObject(qml::UnsavedDecision::staticMetaObject, "VisionRT.Ui", 1, 0, "UnsavedDecision", "Enum values");
        return true;
    }();
    Q_UNUSED(registered);
    quickui::QuiColor::getInstance()->setThemeMode(quickui::QuiColor::Light);
}
}
