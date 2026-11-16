#include "ui/Theme.h"

namespace visionrt {

static int styleThemeTypeId()
{
    static const int id = qmlRegisterSingletonType<Theme>(
        "VisionRT.Style", 1, 0, "Theme",
        [](QQmlEngine *engine, QJSEngine *) -> QObject *
        {
            return new Theme(engine);
        });
    return id;
}

void registerStyleModule()
{
    static const bool registered = []
    {
        styleThemeTypeId();
        return true;
    }();
    Q_UNUSED(registered);
}

Theme *themeForEngine(QQmlEngine *engine)
{
    if (!engine)
        return nullptr;
    const int typeId = styleThemeTypeId();
    return engine->singletonInstance<Theme *>(typeId);
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

    menuButtonBg_         = QColor(0, 0, 0, 0); // 默认透明底色
    buttonHoverBg_        = QColor("#f0f2f5");  // 悬浮浅灰底色（取消蓝色高亮）
    buttonPressedBg_      = QColor("#e4e7eb");
    menuButtonActiveBg_   = QColor("#1677ff"); // 菜单展开激活态实心蓝底色（参考 123055.png）
    menuButtonText_       = QColor("#262626"); // 统一深灰文字
    menuButtonActiveText_ = QColor("#ffffff"); // 激活状态文字反白
    menuButtonIcon_       = QColor("#262626"); // 统一深灰图标
    menuButtonActiveIcon_ = QColor("#ffffff"); // 激活状态图标反白

    // 运行/循环运行按钮色彩（常规状态与其他按钮完全一致）
    runButtonNormalBg_  = QColor(0, 0, 0, 0);
    runButtonHoverBg_   = QColor("#f0f2f5");
    runButtonPressedBg_ = QColor("#e4e7eb");
    runButtonIcon_      = QColor("#262626");
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

    // 底部参数面板标签页（基本输入、预处理、检测、结果处理）
    tabBarBg_           = QColor("#f6f8fa");
    tabBg_              = QColor("#f6f8fa");
    tabActiveBg_        = QColor("#ffffff");
    tabText_            = QColor("#24292f");
    tabHoverText_       = QColor("#1677ff");
    tabActiveText_      = QColor("#24292f");

    // 执行按钮
    executeButtonNormalBg_  = QColor("#ffffff");
    executeButtonHoverBg_   = QColor("#f0f4f9");
    executeButtonPressedBg_ = QColor("#e4e7eb");
    executeButtonIcon_      = QColor("#1677ff");
    executeButtonText_      = QColor("#24292f");

    // 参数编辑区与表格
    paramPanelBg_            = QColor("#ffffff");
    paramGroupHeaderBg_      = QColor("#f6f8fa");
    paramGroupHeaderText_    = QColor("#24292f");
    paramRowHoverBg_         = QColor("#f8fafd");
    parameterTextColor_      = QColor("#24292f");
    paramEditorBorder_       = QColor("#d0d7de");
    paramEditorFocusBorder_  = QColor("#1677ff");
    paramEditorBg_           = QColor("#ffffff");
    paramInputReadOnlyBg_    = QColor("#f0f2f5");
    paramActionActiveBg_     = QColor(22, 119, 255, 26);  // rgba(22, 119, 255, 0.1)
    paramActionHoverBg_      = QColor(22, 119, 255, 38);  // rgba(22, 119, 255, 0.15)

    // 输入来源按钮（选中态只表示输入模式）
    sourceButtonActiveBackground_ = QColor("#1677ff");
    sourceButtonActiveIcon_       = QColor("#ffffff");
    sourceButtonIdleIcon_         = QColor("#8c959f");

    // 全局通用色彩
    windowBg_      = QColor("#f5f7fa");
    surfaceBg_     = QColor("#ffffff");
    highlight_     = QColor("#1677ff");
    textPrimary_   = QColor("#24292f");
    textSecondary_ = QColor("#57606a");
    iconMuted_     = QColor("#8c959f");
    iconSecondary_ = QColor("#57606a");
    error_         = QColor("#cf222e");
    warning_       = QColor("#9a6700");
    disabledText_  = QColor("#8c959f");
    tooltipBg_     = QColor("#ffffff");
    viewportBg_    = QColor("#eaebef");
    navItemActiveBg_   = QColor("#e7f5ff"); // 设置导航选中浅蓝底
    navItemActiveText_ = QColor("#0875d9"); // 设置导航选中强调文字
    zoomText_      = QColor("#57606a");

    inputRoiColor_  = QColor(255, 220, 0);
    outputRoiColor_ = QColor(0, 120, 255);
    assistRoiColor_ = QColor(0, 190, 190);

    // 流程画布节点四态色彩与徽标
    nodeStateGray_       = QColor("#8c959f");
    nodeStateGreen_      = QColor("#1a7f37");
    nodeStateRed_        = QColor("#cf222e");
    nodeHeaderGray_      = QColor("#dfe4ea");
    nodeHeaderGreen_     = QColor("#1a7f37");
    nodeHeaderRed_       = QColor("#ffebe9");
    nodeHeaderTextGray_  = QColor("#57606a");
    nodeHeaderTextGreen_ = QColor("#ffffff");
    nodeHeaderTextRed_   = QColor("#cf222e");

    nodeWidth_ = 160;
    nodeHeight_ = 60;

    // 顶部工具栏尺寸与间距
    menuBarHeight_      = 38;
    menuButtonHeight_   = 26;
    menuButtonSpacing_  = 4;
    actionButtonWidth_  = 26;
    actionButtonHeight_ = 26;

    // 工具列表尺寸与间距（箭头可调整为 10~12px，规范统一）
    treeRowHeight_      = 30;
    treeIndent_         = 16;
    treeArrowSize_      = 10;
    treeArrowWidth_     = 14;
    treeFolderIconSize_ = 14;
    treeLeafIconSize_   = 11;

    separatorWidth_     = 1;
    separatorHeight_    = 32;

    // 底部参数面板与执行按钮尺寸
    tabBarHeight_             = 34;
    statusBarHeight_          = 32;
    segmentButtonWidth_       = 92;
    segmentButtonHeight_      = 22;
    executeButtonHeight_      = 24;
    executeButtonWidth_       = 68;
    paramGroupHeaderHeight_   = 28;
    parameterRowHeight_       = 32;
    paramActionBtnWidth_      = 28;
    logButtonHoverColor_       = QColor("#eaf2fd");
    parameterLabelWidth_       = 96;
    cardLabelWidth_            = 64;
    timingIconSize_            = 13;
    parameterRowSpacing_      = 6;
    paramToggleIconSize_      = 14;
    paramRoiIconSize_         = 16;
    paramActionIconSize_      = 13;

    // 全局图标与圆角
    iconSizeTiny_    = 10;
    iconSizeSmall_   = 12;
    iconSizeBase_    = 14;
    radiusSmall_     = 3;
    radiusBase_      = 4;

    // 结果面板与 tooltip
    toolbarWidth_          = 32;
    toolbarButtonSize_     = 26;
    thumbnailPanelWidth_   = 116;
    trainingPanelWidth_    = 220;
    thumbnailRowHeight_    = 86;
    thumbnailImageHeight_  = 60;
    tooltipWidth_          = 232;
    roiStrokeWidth_        = 2;
    roiHandleSize_         = 4;

    sourceButtonDisabledOpacity_ = 0.45;

}

} // namespace visionrt
