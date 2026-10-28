#include "ui/Theme.h"

namespace visionrt {

static int styleThemeTypeId()
{
    static const int id = qmlRegisterSingletonType<Theme>(
        "VisionRT.Style", 1, 0, "Theme",
        [](QQmlEngine *engine, QJSEngine *) -> QObject *
        {
            if (engine)
            {
                auto *obj = engine->property("_visionrt_theme").value<QObject *>();
                if (auto *theme = qobject_cast<Theme *>(obj))
                    return theme;
            }
            auto *theme = new Theme(engine);
            if (engine)
                engine->setProperty("_visionrt_theme", QVariant::fromValue(static_cast<QObject *>(theme)));
            return theme;
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
    styleThemeTypeId();
    auto *obj = engine->property("_visionrt_theme").value<QObject *>();
    if (auto *theme = qobject_cast<Theme *>(obj))
        return theme;
    auto *theme = new Theme(engine);
    engine->setProperty("_visionrt_theme", QVariant::fromValue(static_cast<QObject *>(theme)));
    return theme;
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
    parameterTextColor_      = paramLabelText_;
    paramEditorBorder_       = QColor("#d0d7de");
    paramEditorFocusBorder_  = QColor("#1677ff");
    paramEditorBg_           = QColor("#ffffff");
    paramInputReadOnlyBg_    = QColor("#f0f2f5");
    paramActionActiveBg_     = QColor(22, 119, 255, 26);  // rgba(22, 119, 255, 0.1)
    paramActionHoverBg_      = QColor(22, 119, 255, 38);  // rgba(22, 119, 255, 0.15)
    bindingInactiveColor_    = QColor("#8c959f");

    // 输入来源按钮（选中态只表示输入模式）
    sourceButtonActiveBackground_ = QColor("#1677ff");
    sourceButtonActiveBorder_     = QColor("#1677ff");
    sourceButtonActiveIcon_       = QColor("#ffffff");
    sourceButtonIdleIcon_         = QColor("#8c959f");

    // 全局通用色彩
    windowBg_      = QColor("#f5f7fa");
    surfaceBg_     = QColor("#ffffff");
    border_        = QColor("#d0d7de");
    highlight_     = QColor("#1677ff");
    textPrimary_   = QColor("#24292f");
    textSecondary_ = QColor("#57606a");
    iconMuted_     = QColor("#8c959f");
    iconSecondary_ = QColor("#57606a");
    error_         = QColor("#cf222e");
    success_       = QColor("#1a7f37");
    warning_       = QColor("#9a6700");
    disabledText_  = QColor("#8c959f");
    tooltipBg_     = QColor("#ffffff");
    tooltipText_   = QColor("#24292f");
    viewportBg_    = QColor("#eaebef");
    assistHintText_ = QColor("#0f6f6f");
    zoomText_      = QColor("#57606a");

    inputRoiColor_  = QColor(255, 220, 0);
    outputRoiColor_ = QColor(0, 120, 255);
    assistRoiColor_ = QColor(0, 190, 190);

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
    parameterRowHeight_       = 32;
    paramEditorHeight_        = parameterRowHeight_;
    paramEditorFontSize_      = 12;
    paramActionBtnWidth_      = 28;
    parameterRowSpacing_      = 6;
    paramToggleIconSize_      = 14;
    paramRoiIconSize_         = 16;
    paramActionIconSize_      = 13;

    // 全局字号、图标与圆角
    fontSizeCaption_ = 10;
    fontSizeSmall_   = 11;
    fontSizeBase_    = 12;
    fontSizeMedium_  = 13;
    fontSizeLarge_   = 14;
    iconSizeTiny_    = 10;
    iconSizeSmall_   = 12;
    iconSizeBase_    = 14;
    iconSizeMedium_  = 16;
    iconSizeLarge_   = 18;
    iconSizeXLarge_  = 20;
    radiusSmall_     = 3;
    radiusBase_      = 4;
    radiusLarge_     = 6;

    // 结果面板与 tooltip
    toolbarWidth_          = 32;
    toolbarButtonSize_     = 26;
    thumbnailPanelWidth_   = 116;
    thumbnailRowHeight_    = 86;
    thumbnailImageHeight_  = 60;
    tooltipWidth_          = 232;
    tooltipRowHeight_      = 16;
    roiStrokeWidth_        = 2;
    roiHandleSize_         = 4;
    dialogWidth_           = 520;
    dialogMaxHeight_       = 560;
    menuItemHeight_        = 26;

    sourceButtonDisabledOpacity_ = 0.45;

    // 字体默认值：家族、字号、字重的唯一来源
    const QString family = QStringLiteral("Microsoft YaHei UI");
    bodyFont_    = QFont(family, -1, QFont::Normal);
    bodyFont_.setPixelSize(fontSizeBase_);
    captionFont_ = QFont(family, -1, QFont::Normal);
    captionFont_.setPixelSize(fontSizeSmall_);
    titleFont_   = QFont(family, -1, QFont::DemiBold);
    titleFont_.setPixelSize(fontSizeMedium_);
    monoFont_    = QFont(QStringLiteral("Consolas"), -1, QFont::Normal);
    monoFont_.setPixelSize(fontSizeBase_);
    // 参数行的字体：家族与字号只在 Theme 定义，QML 不再各写 Qt.font({...})
    parameterFont_ = QFont(family, -1, QFont::Normal);
    parameterFont_.setPixelSize(paramEditorFontSize_);

    iconFitWindow_  = 0xe740; // QuiFontIcon.FullScreen
    iconZoomIn_     = 0xe8a3; // QuiFontIcon.ZoomIn
    iconZoomOut_    = 0xe71f; // QuiFontIcon.ZoomOut
    iconOneToOne_   = 0xeb9f; // QuiFontIcon.Photo2
    iconEasyTouch_  = 0xe721; // QuiFontIcon.Search
    iconEdit_       = 0xe70f; // QuiFontIcon.Edit
    iconClear_      = 0xe74d; // QuiFontIcon.Delete
    iconFolderOpen_ = 0xe838; // QuiFontIcon.FolderOpen
}

} // namespace visionrt
