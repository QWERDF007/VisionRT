#include "ui/Theme.h"

namespace visionrt {

Theme *Theme::instance()
{
    static Theme themeInstance;
    return &themeInstance;
}

Theme::Theme(QObject *parent)
    : QObject(parent)
{
    initLightIndustrialPalette();
}

void Theme::initLightIndustrialPalette()
{
    // 顶部菜单栏与工具栏按钮色彩（完全对齐 120200.png / 120239.png 标准）
    menuBarBg_     = QColor("#ffffff");
    menuBarBorder_ = QColor("#e8e8e8");
    menuSeparator_ = QColor("#e8e8e8");

    menuButtonBg_         = QColor(0, 0, 0, 0); // 默认透明底色
    menuButtonHoverBg_    = QColor("#f0f2f5");  // 悬浮浅灰底色（取消蓝色高亮）
    menuButtonPressedBg_  = QColor("#e4e7eb");
    menuButtonActiveBg_   = QColor("#1677ff"); // 菜单展开激活态实心蓝底色（参考 123055.png）
    menuButtonBorder_     = QColor(0, 0, 0, 0);
    menuButtonText_       = QColor("#262626"); // 统一深灰文字
    menuButtonHoverText_  = QColor("#262626"); // 悬浮时保持深灰
    menuButtonActiveText_ = QColor("#ffffff"); // 激活状态文字反白
    menuButtonIcon_       = QColor("#262626"); // 统一深灰图标
    menuButtonHoverIcon_  = QColor("#262626"); // 悬浮时保持深灰
    menuButtonActiveIcon_ = QColor("#ffffff"); // 激活状态图标反白

    // 运行/循环运行按钮色彩（常规状态与其他按钮完全一致）
    runButtonNormalBg_  = QColor(0, 0, 0, 0);
    runButtonHoverBg_   = QColor("#f0f2f5");
    runButtonPressedBg_ = QColor("#e4e7eb");
    runButtonIcon_      = QColor("#262626");
    runButtonHoverIcon_ = QColor("#262626");
    loopRunningIcon_    = QColor("#cf222e");

    // 工具列表（工具库树控件）色彩（完全对齐 125712.png 标准）
    treeArrowColor_            = QColor("#8c8c8c");
    treeArrowHoverColor_       = QColor("#1677ff");
    treeFolderIconColor_       = QColor("#1677ff");
    treeLeafIconColor_         = QColor("#1677ff");
    treeItemText_              = QColor("#262626");
    treeItemHoverBg_           = QColor("#e6f1fe");  // 悬浮浅淡蓝底色（参考 125712.png）
    treeItemSelectedBg_        = QColor("#cce4fd");  // 选中浅蓝底色（参考 125712.png）
    treeItemSelectedText_      = QColor("#1677ff");  // 选中时文字蓝色高亮（参考 125712.png）
    treeItemSelectedIcon_      = QColor("#1677ff");  // 选中时图标蓝色高亮（参考 125712.png）
    treeItemSelectedIndicator_ = QColor(0, 0, 0, 0); // 通栏平滑无边框
    treeItemUnavailableText_   = QColor("#cf222e");

    // 底部参数面板标签页（基本输入、预处理、检测、结果处理）
    tabBarBg_           = QColor("#f6f8fa");
    tabBg_              = QColor("#f6f8fa");
    tabActiveBg_        = QColor("#ffffff");
    tabText_            = QColor("#57606a");
    tabHoverText_       = QColor("#1677ff");
    tabActiveText_      = QColor("#1677ff");
    tabActiveIndicator_ = QColor("#1677ff");
    tabBorder_          = QColor("#d0d7de");

    // 执行按钮
    executeButtonNormalBg_  = QColor("#ffffff");
    executeButtonHoverBg_   = QColor("#f0f4f9");
    executeButtonPressedBg_ = QColor("#e4e7eb");
    executeButtonBorder_    = QColor("#d0d7de");
    executeButtonIcon_      = QColor("#1677ff");
    executeButtonText_      = QColor("#24292f");

    // 参数编辑区与表格
    paramPanelBg_            = QColor("#ffffff");
    paramGroupHeaderBg_      = QColor("#f6f8fa");
    paramGroupHeaderHoverBg_ = QColor("#f0f2f5");
    paramGroupHeaderText_    = QColor("#24292f");
    paramGroupHeaderBorder_  = QColor("#d0d7de");
    paramRowBorder_          = QColor("#eef0f3");
    paramRowHoverBg_         = QColor("#f8fafd");
    paramLabelText_          = QColor("#24292f");
    paramEditorBorder_       = QColor("#d0d7de");
    paramEditorFocusBorder_  = QColor("#1677ff");
    paramEditorBg_           = QColor("#ffffff");
    bindingActiveColor_      = QColor("#1677ff");
    bindingInactiveColor_    = QColor("#8c959f");
    spinButtonBg_            = QColor("#ffffff");
    spinButtonHoverBg_       = QColor("#eef0f3");
    spinButtonBorder_        = QColor("#c5ccd5");
    spinButtonIcon_          = QColor("#57606a");

    // 全局通用色彩
    windowBg_      = QColor("#f5f7fa");
    surfaceBg_     = QColor("#ffffff");
    border_        = QColor("#d0d7de");
    highlight_     = QColor("#1677ff");
    textPrimary_   = QColor("#24292f");
    textSecondary_ = QColor("#57606a");
    error_         = QColor("#cf222e");
    success_       = QColor("#1a7f37");
    warning_       = QColor("#9a6700");

    // 流程画布节点四态色彩与徽标
    nodeStateGray_       = QColor("#8c959f");
    nodeStateBlue_       = QColor("#1887d8");
    nodeStateGreen_      = QColor("#2da44e");
    nodeStateRed_        = QColor("#cf222e");
    nodeStateOrange_     = QColor("#e66b37");
    nodeHeaderGray_      = QColor("#dfe4ea");
    nodeHeaderBlue_      = QColor("#ddf4ff");
    nodeHeaderGreen_     = QColor("#dafbe1");
    nodeHeaderRed_       = QColor("#ffebe9");
    nodeHeaderTextGray_  = QColor("#57606a");
    nodeHeaderTextBlue_  = QColor("#0969da");
    nodeHeaderTextGreen_ = QColor("#1a7f37");
    nodeHeaderTextRed_   = QColor("#cf222e");

    // 顶部工具栏尺寸与间距
    menuBarHeight_      = 38;
    menuButtonHeight_   = 26;
    menuButtonSpacing_  = 4;
    menuIconSize_       = 14;
    actionButtonWidth_  = 26;
    actionButtonHeight_ = 26;
    actionIconSize_     = 14;

    // 工具列表尺寸与间距（箭头可调整为 10~12px，规范统一）
    treeRowHeight_      = 26;
    treeIndent_         = 16;
    treeArrowSize_      = 10;
    treeArrowWidth_     = 14;
    treeFolderIconSize_ = 13;
    treeLeafIconSize_   = 11;
    treeItemFontSize_   = 12;

    // 底部参数面板与执行按钮尺寸
    tabBarHeight_             = 30;
    tabFontSize_              = 12;
    executeButtonHeight_      = 24;
    executeButtonWidth_       = 68;
    executeButtonIconSize_    = 14;
    executeButtonFontSize_    = 12;
    paramGroupHeaderHeight_   = 28;
    paramGroupHeaderFontSize_ = 12;
    paramLabelFontSize_       = 12;
    paramEditorHeight_        = 24;
    paramEditorFontSize_      = 12;

    // 全局字号与圆角
    fontSizeCaption_ = 10;
    fontSizeSmall_   = 11;
    fontSizeBase_    = 12;
    fontSizeMedium_  = 13;
    fontSizeLarge_   = 14;
    radiusSmall_     = 3;
    radiusBase_      = 4;
    radiusLarge_     = 6;
}

} // namespace visionrt
