#include "ui/DurationText.h"
#include "runtime/Snapshot.h"

#include <cmath>

namespace visionrt {

NodeTimingText nodeTimingText(const NodeSnapshot *snapshot, bool running)
{
    if (running)
        return {QStringLiteral("运行中"), QStringLiteral("运行中")};
    if (!snapshot)
        return {QStringLiteral("未运行"), QStringLiteral("未运行")};
    return {durationText(snapshot->timing.totalMs), snapshot->timing.toolCallMeasured
                ? durationText(snapshot->timing.adapterExecuteMs) : QStringLiteral("—")};
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

} // namespace visionrt
