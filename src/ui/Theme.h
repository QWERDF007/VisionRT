#pragma once

#include <QColor>
#include <QObject>
#include <QtGlobal>
#include <QtQml>

namespace visionrt {

#define THEME_COLOR_PROP(Name) \
    Q_PROPERTY(QColor Name READ Name WRITE set##Name NOTIFY Name##Changed) \
public: \
    QColor Name() const { return Name##_; } \
    void set##Name(const QColor& color) { \
        if (Name##_ != color) { \
            Name##_ = color; \
            Q_EMIT Name##Changed(); \
        } \
    } \
    Q_SIGNAL void Name##Changed(); \
private: \
    QColor Name##_;

#define THEME_INT_PROP(Name) \
    Q_PROPERTY(int Name READ Name WRITE set##Name NOTIFY Name##Changed) \
public: \
    int Name() const { return Name##_; } \
    void set##Name(int value) { \
        if (Name##_ != value) { \
            Name##_ = value; \
            Q_EMIT Name##Changed(); \
        } \
    } \
    Q_SIGNAL void Name##Changed(); \
private: \
    int Name##_;

#define THEME_REAL_PROP(Name) \
    Q_PROPERTY(qreal Name READ Name WRITE set##Name NOTIFY Name##Changed) \
public: \
    qreal Name() const { return Name##_; } \
    void set##Name(qreal value) { \
        if (!qFuzzyCompare(Name##_, value)) { \
            Name##_ = value; \
            Q_EMIT Name##Changed(); \
        } \
    } \
    Q_SIGNAL void Name##Changed(); \
private: \
    qreal Name##_;

class Theme : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Theme)
    QML_SINGLETON

public:
    explicit Theme(QObject* parent = nullptr);
    static Theme* instance();

    // 顶部菜单栏与工具栏按钮色彩
    THEME_COLOR_PROP(menuBarBg)
    THEME_COLOR_PROP(menuBarBorder)
    THEME_COLOR_PROP(menuSeparator)
    THEME_COLOR_PROP(menuButtonBg)
    THEME_COLOR_PROP(menuButtonHoverBg)
    THEME_COLOR_PROP(menuButtonPressedBg)
    THEME_COLOR_PROP(menuButtonActiveBg)
    THEME_COLOR_PROP(menuButtonBorder)
    THEME_COLOR_PROP(menuButtonText)
    THEME_COLOR_PROP(menuButtonHoverText)
    THEME_COLOR_PROP(menuButtonActiveText)
    THEME_COLOR_PROP(menuButtonIcon)
    THEME_COLOR_PROP(menuButtonHoverIcon)
    THEME_COLOR_PROP(menuButtonActiveIcon)

    // 运行/循环运行按钮色彩
    THEME_COLOR_PROP(runButtonNormalBg)
    THEME_COLOR_PROP(runButtonHoverBg)
    THEME_COLOR_PROP(runButtonPressedBg)
    THEME_COLOR_PROP(runButtonIcon)
    THEME_COLOR_PROP(runButtonHoverIcon)
    THEME_COLOR_PROP(loopRunningIcon)

    // 工具列表（工具库树控件）色彩
    THEME_COLOR_PROP(treeArrowColor)
    THEME_COLOR_PROP(treeArrowHoverColor)
    THEME_COLOR_PROP(treeFolderIconColor)
    THEME_COLOR_PROP(treeLeafIconColor)
    THEME_COLOR_PROP(treeItemText)
    THEME_COLOR_PROP(treeItemHoverBg)
    THEME_COLOR_PROP(treeItemSelectedBg)
    THEME_COLOR_PROP(treeItemSelectedText)
    THEME_COLOR_PROP(treeItemSelectedIcon)
    THEME_COLOR_PROP(treeItemSelectedIndicator)
    THEME_COLOR_PROP(treeItemUnavailableText)

    // 底部参数面板标签页（基本输入、预处理、检测、结果处理）
    THEME_COLOR_PROP(tabBarBg)
    THEME_COLOR_PROP(tabBg)
    THEME_COLOR_PROP(tabActiveBg)
    THEME_COLOR_PROP(tabText)
    THEME_COLOR_PROP(tabHoverText)
    THEME_COLOR_PROP(tabActiveText)
    THEME_COLOR_PROP(tabActiveIndicator)
    THEME_COLOR_PROP(tabBorder)

    // 执行按钮
    THEME_COLOR_PROP(executeButtonNormalBg)
    THEME_COLOR_PROP(executeButtonHoverBg)
    THEME_COLOR_PROP(executeButtonPressedBg)
    THEME_COLOR_PROP(executeButtonBorder)
    THEME_COLOR_PROP(executeButtonIcon)
    THEME_COLOR_PROP(executeButtonText)

    // 参数编辑区与表格
    THEME_COLOR_PROP(paramPanelBg)
    THEME_COLOR_PROP(paramGroupHeaderBg)
    THEME_COLOR_PROP(paramGroupHeaderHoverBg)
    THEME_COLOR_PROP(paramGroupHeaderText)
    THEME_COLOR_PROP(paramGroupHeaderBorder)
    THEME_COLOR_PROP(paramRowBorder)
    THEME_COLOR_PROP(paramRowHoverBg)
    THEME_COLOR_PROP(paramLabelText)
    THEME_COLOR_PROP(paramEditorBorder)
    THEME_COLOR_PROP(paramEditorFocusBorder)
    THEME_COLOR_PROP(paramEditorBg)
    THEME_COLOR_PROP(bindingActiveColor)
    THEME_COLOR_PROP(bindingInactiveColor)
    THEME_COLOR_PROP(spinButtonBg)
    THEME_COLOR_PROP(spinButtonHoverBg)
    THEME_COLOR_PROP(spinButtonBorder)
    THEME_COLOR_PROP(spinButtonIcon)

    // 全局通用色彩
    THEME_COLOR_PROP(windowBg)
    THEME_COLOR_PROP(surfaceBg)
    THEME_COLOR_PROP(border)
    THEME_COLOR_PROP(highlight)
    THEME_COLOR_PROP(textPrimary)
    THEME_COLOR_PROP(textSecondary)
    THEME_COLOR_PROP(error)
    THEME_COLOR_PROP(success)
    THEME_COLOR_PROP(warning)

    // 流程画布节点四态色彩
    THEME_COLOR_PROP(nodeStateGray)
    THEME_COLOR_PROP(nodeStateBlue)
    THEME_COLOR_PROP(nodeStateGreen)
    THEME_COLOR_PROP(nodeStateRed)
    THEME_COLOR_PROP(nodeStateOrange)
    THEME_COLOR_PROP(nodeHeaderGray)
    THEME_COLOR_PROP(nodeHeaderBlue)
    THEME_COLOR_PROP(nodeHeaderGreen)
    THEME_COLOR_PROP(nodeHeaderRed)
    THEME_COLOR_PROP(nodeHeaderTextGray)
    THEME_COLOR_PROP(nodeHeaderTextBlue)
    THEME_COLOR_PROP(nodeHeaderTextGreen)
    THEME_COLOR_PROP(nodeHeaderTextRed)

    // 顶部工具栏尺寸与间距
    THEME_INT_PROP(menuBarHeight)
    THEME_INT_PROP(menuButtonHeight)
    THEME_INT_PROP(menuButtonSpacing)
    THEME_INT_PROP(menuIconSize)
    THEME_INT_PROP(actionButtonWidth)
    THEME_INT_PROP(actionButtonHeight)
    THEME_INT_PROP(actionIconSize)

    // 工具列表尺寸与间距
    THEME_INT_PROP(treeRowHeight)
    THEME_INT_PROP(treeIndent)
    THEME_INT_PROP(treeArrowSize)
    THEME_INT_PROP(treeArrowWidth)
    THEME_INT_PROP(treeFolderIconSize)
    THEME_INT_PROP(treeLeafIconSize)
    THEME_INT_PROP(treeItemFontSize)

    // 底部参数面板与执行按钮尺寸
    THEME_INT_PROP(tabBarHeight)
    THEME_INT_PROP(tabFontSize)
    THEME_INT_PROP(executeButtonHeight)
    THEME_INT_PROP(executeButtonWidth)
    THEME_INT_PROP(executeButtonIconSize)
    THEME_INT_PROP(executeButtonFontSize)
    THEME_INT_PROP(paramGroupHeaderHeight)
    THEME_INT_PROP(paramGroupHeaderFontSize)
    THEME_INT_PROP(paramLabelFontSize)
    THEME_INT_PROP(paramEditorHeight)
    THEME_INT_PROP(paramEditorFontSize)

    // 全局字号与圆角
    THEME_INT_PROP(fontSizeCaption)
    THEME_INT_PROP(fontSizeSmall)
    THEME_INT_PROP(fontSizeBase)
    THEME_INT_PROP(fontSizeMedium)
    THEME_INT_PROP(fontSizeLarge)
    THEME_INT_PROP(radiusSmall)
    THEME_INT_PROP(radiusBase)
    THEME_INT_PROP(radiusLarge)

private:
    void initLightIndustrialPalette();
};

#undef THEME_COLOR_PROP
#undef THEME_INT_PROP
#undef THEME_REAL_PROP

} // namespace visionrt
