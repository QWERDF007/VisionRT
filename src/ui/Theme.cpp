#include "Theme.h"
#include <QResource>
#include <quickui/Color.h>
static void loadStyleResources() { Q_INIT_RESOURCE(visionrt_styles); }
namespace visionrt
{
void initializeUiStyle()
{
    static const bool initialized = []
    {
        loadStyleResources();
        qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/visionrt/style/WorkbenchStyle.qml")), "VisionRT.Style", 1, 0,
                                 "WorkbenchStyle");
        qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/visionrt/style/ParameterStyle.qml")), "VisionRT.Style", 1, 0,
                                 "ParameterStyle");
        qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/visionrt/style/ToolTreeStyle.qml")), "VisionRT.Style", 1, 0,
                                 "ToolTreeStyle");
        qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/visionrt/style/ResultsStyle.qml")), "VisionRT.Style", 1, 0,
                                 "ResultsStyle");
        qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/visionrt/style/FlowStyle.qml")), "VisionRT.Style", 1, 0,
                                 "FlowStyle");
        auto *palette = quickui::QuiColor::getInstance();
        palette->setThemeMode(quickui::QuiColor::Light);
        palette->Background(QColor("#f5f7fa"));
        palette->Primary(palette->Button());
        palette->Highlight(QColor("#1677ff"));
        palette->FontPrimary(QColor("#24292f"));
        palette->FontDark(QColor("#57606a"));
        palette->FontCaption(palette->FontDark());
        palette->CardBorder(QColor("#d0d7de"));
        palette->ItemHover(QColor("#f8fafd"));
        palette->Hovered(QColor("#f0f2f5"));
        palette->ItemPress(QColor("#e4e7eb"));
        return true;
    }();
    Q_UNUSED(initialized);
}
} // namespace visionrt
