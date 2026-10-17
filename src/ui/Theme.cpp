#include "ui/Theme.h"

namespace visionrt {

Theme* Theme::instance() {
    static Theme themeInstance;
    return &themeInstance;
}

Theme::Theme(QObject* parent) : QObject(parent) {
    initLightIndustrialPalette();
}

void Theme::initLightIndustrialPalette() {
    // 顶部菜单栏与工具栏按钮
    menuButtonBg_ = QColor("#f0f2f5");        // 原悬浮底色作为默认底色
    menuButtonHoverBg_ = QColor("#1677ff");   // 悬浮时蓝色高亮
    menuButtonPressedBg_ = QColor("#0958d9");
    menuButtonBorder_ = QColor("#d0d7de");
    menuButtonText_ = QColor("#24292f");
    menuButtonHoverText_ = QColor("#ffffff"); // 悬浮蓝色时文字白色
    menuButtonIcon_ = QColor("#57606a");      // 统一图标正常颜色
    menuButtonHoverIcon_ = QColor("#ffffff"); // 悬浮蓝色时图标白色
    menuBarBg_ = QColor("#ffffff");
    menuBarBorder_ = QColor("#d0d7de");
    menuSeparator_ = QColor("#d0d7de");

    // 运行/循环运行按钮（正常颜色与常规按钮一致）
    runButtonNormalBg_ = QColor("#f0f2f5");
    runButtonHoverBg_ = QColor("#1677ff");
    runButtonIcon_ = QColor("#57606a");
    runButtonHoverIcon_ = QColor("#ffffff");
    loopRunningIcon_ = QColor("#cf222e");

    // 底部参数面板标签页（基本输入、预处理、检测、结果处理）
    tabBarBg_ = QColor("#f6f8fa");
    tabBg_ = QColor("#f6f8fa");
    tabActiveBg_ = QColor("#ffffff");
    tabText_ = QColor("#57606a");
    tabHoverText_ = QColor("#1677ff");        // 悬浮字体高亮
    tabActiveText_ = QColor("#1677ff");       // 选中字体高亮
    tabActiveIndicator_ = QColor("#1677ff");
    tabBorder_ = QColor("#d0d7de");

    // 执行按钮
    executeButtonNormalBg_ = QColor("#ffffff");
    executeButtonHoverBg_ = QColor("#f0f4f9");
    executeButtonPressedBg_ = QColor("#e4e7eb");
    executeButtonBorder_ = QColor("#d0d7de");
    executeButtonIcon_ = QColor("#1677ff");
    executeButtonText_ = QColor("#24292f");

    // 参数编辑区与表格
    paramPanelBg_ = QColor("#ffffff");
    paramGroupHeaderBg_ = QColor("#f6f8fa");
    paramGroupHeaderHoverBg_ = QColor("#f0f2f5");
    paramGroupHeaderText_ = QColor("#24292f");
    paramGroupHeaderBorder_ = QColor("#d0d7de");
    paramRowBorder_ = QColor("#eef0f3");
    paramRowHoverBg_ = QColor("#f8fafd");
    paramLabelText_ = QColor("#24292f");
    paramEditorBorder_ = QColor("#d0d7de");
    paramEditorFocusBorder_ = QColor("#1677ff");
    paramEditorBg_ = QColor("#ffffff");
    bindingActiveColor_ = QColor("#1677ff");
    bindingInactiveColor_ = QColor("#8c959f");
    spinButtonBg_ = QColor("#ffffff");
    spinButtonHoverBg_ = QColor("#eef0f3");
    spinButtonBorder_ = QColor("#c5ccd5");
    spinButtonIcon_ = QColor("#57606a");

    // 全局通用色彩
    windowBg_ = QColor("#f5f7fa");
    surfaceBg_ = QColor("#ffffff");
    border_ = QColor("#d0d7de");
    highlight_ = QColor("#1677ff");
    textPrimary_ = QColor("#24292f");
    textSecondary_ = QColor("#57606a");
    error_ = QColor("#cf222e");
    success_ = QColor("#1a7f37");
    warning_ = QColor("#9a6700");
}

} // namespace visionrt
