#include "ui/DurationText.h"
#include "runtime/Snapshot.h"

#include <cmath>

namespace visionrt {

NodeTimingText nodeTimingText(const NodeSnapshot *snapshot, bool running)
{
    if (running)
        return {QStringLiteral("运行中"), QStringLiteral("运行中"), QStringLiteral("—"), QStringLiteral("—"),
                QStringLiteral("运行中"), QStringLiteral("运行中"), QStringLiteral("运行中")};
    if (!snapshot)
        return {QStringLiteral("未运行"), QStringLiteral("未运行"), QStringLiteral("—"), QStringLiteral("—"),
                QStringLiteral("未运行"), QStringLiteral("未运行"), QStringLiteral("未运行")};

    const auto &t = snapshot->timing;
    QString totalStr = durationText(t.totalMs);
    QString toolCallStr = t.toolCallMeasured ? durationText(t.adapterExecuteMs) : QStringLiteral("—");
    QString prepareStr = t.prepareMeasured ? durationText(t.prepareMs) : QStringLiteral("—");
    QString initStr = t.initMeasured ? durationText(t.initMs) : QStringLiteral("—");
    QString validateStr = t.validateMeasured ? durationText(t.validateMs) : QStringLiteral("—");
    QString preprocessStr = QStringLiteral("—");
    QString postprocessStr = QStringLiteral("—");

    return {totalStr, toolCallStr, preprocessStr, postprocessStr, prepareStr, initStr, validateStr};
}

QString durationText(std::optional<double> milliseconds)
{
    if (!milliseconds || !std::isfinite(*milliseconds) || *milliseconds < 0.0)
        return QStringLiteral("—");
    const double value = *milliseconds;
    if (value == 0.0)
        return QStringLiteral("0 ms");
    if (value < 0.01)
        return QStringLiteral("<0.01 ms");
    QString text = QString::number(value, 'f', 2);
    while (text.endsWith(QLatin1Char('0')))
        text.chop(1);
    if (text.endsWith(QLatin1Char('.')))
        text.chop(1);
    return text + QStringLiteral(" ms");
}

QString formatElapsed(double milliseconds)
{
    if (milliseconds <= 0.0 || !std::isfinite(milliseconds))
        return QStringLiteral("00:00:00:000");
    auto totalMs = static_cast<std::uint64_t>(milliseconds);
    auto millis = totalMs % 1000;
    auto totalSec = totalMs / 1000;
    auto seconds = totalSec % 60;
    auto totalMin = totalSec / 60;
    auto minutes = totalMin % 60;
    auto hours = totalMin / 60;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

QString formatMs(std::optional<double> milliseconds)
{
    if (!milliseconds || !std::isfinite(*milliseconds) || *milliseconds < 0.0)
        return QStringLiteral("— ms");
    return QStringLiteral("%1 ms").arg(*milliseconds, 0, 'f', 2);
}
} // namespace visionrt
