#include "ui/LogHighlighter.h"

#include <QTextDocument>

namespace visionrt {
// The formatter writes exactly one level marker per line, so the marker alone decides the colour.
LogHighlighter::LogHighlighter(QTextDocument *document, QColor warning, QColor error)
    : QSyntaxHighlighter(document)
    , warning_(warning)
    , error_(error)
{
}

void LogHighlighter::highlightBlock(const QString &text)
{
    const auto timestampEnd = text.indexOf(QLatin1StringView("] ["));
    if (timestampEnd < 0)
        return;
    const auto level = QStringView(text).mid(timestampEnd + 3, 1);
    if (level == QLatin1StringView("E") || level == QLatin1StringView("C"))
        setFormat(0, text.size(), error_);
    else if (level == QLatin1StringView("W"))
        setFormat(0, text.size(), warning_);
}
} // namespace visionrt
