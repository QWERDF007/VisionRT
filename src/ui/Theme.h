#pragma once

#include <QColor>
#include <QObject>
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

class Theme : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Theme)
    QML_SINGLETON

public:
    explicit Theme(QObject* parent = nullptr);
    static Theme* instance();

    // 顶部菜单栏与工具栏按钮
    THEME_COLOR_PROP(menuButtonBg)
    THEME_COLOR_PROP(menuButtonHoverBg)
    THEME_COLOR_PROP(menuButtonPressedBg)
    THEME_COLOR_PROP(menuButtonBorder)
    THEME_COLOR_PROP(menuButtonText)
    THEME_COLOR_PROP(menuButtonHoverText)
    THEME_COLOR_PROP(menuButtonIcon)
    THEME_COLOR_PROP(menuButtonHoverIcon)
    THEME_COLOR_PROP(menuBarBg)
    THEME_COLOR_PROP(menuBarBorder)
    THEME_COLOR_PROP(menuSeparator)

    // 运行/循环运行按钮
    THEME_COLOR_PROP(runButtonNormalBg)
    THEME_COLOR_PROP(runButtonHoverBg)
    THEME_COLOR_PROP(runButtonIcon)
    THEME_COLOR_PROP(runButtonHoverIcon)
    THEME_COLOR_PROP(loopRunningIcon)

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

private:
    void initLightIndustrialPalette();
};

#undef THEME_COLOR_PROP

} // namespace visionrt
