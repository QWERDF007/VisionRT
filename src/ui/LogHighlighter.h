#pragma once

#include <QColor>
#include <QSyntaxHighlighter>

namespace visionrt {

// Colours the level marker of an already-formatted log line. It reads nothing but the rendered
// text, changes no character, and owns no log data: the window keeps one text buffer, this only
// paints it.
class LogHighlighter final : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    LogHighlighter(QTextDocument *document, QColor warning, QColor error);

protected:
    void highlightBlock(const QString &text) override;

private:
    QColor warning_;
    QColor error_;
};
} // namespace visionrt
