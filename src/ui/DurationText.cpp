#include "ui/DurationText.h"
#include "runtime/Snapshot.h"

#include <cmath>

namespace visionrt {

NodeTimingText nodeTimingText(const NodeSnapshot *snapshot, bool running)
{
    if (running)
        return {QStringLiteral("运行中"), QStringLiteral("运行中"), QStringLiteral("—"), QStringLiteral("—"),
                QStringLiteral("运行中"), QStringLiteral("运行中"), QStringLiteral("运行中")};
    // A node that never ran has no measurement to show; the disposition badge (and nothing else)
    // carries the state, so the timing fields stay empty instead of inventing a zero.
    if (!snapshot)
        return {QStringLiteral("—"), QStringLiteral("—"), QStringLiteral("—"), QStringLiteral("—"),
                QStringLiteral("—"), QStringLiteral("—"), QStringLiteral("—")};

    // A gated node never entered the algorithm, so it must not look like a zero-millisecond
    // success. The gate decision itself is reported through the disposition badge instead.
    if (snapshot->disposition == NodeDisposition::Skipped || snapshot->disposition == NodeDisposition::Blocked)
        return {QStringLiteral("—"), QStringLiteral("—"), QStringLiteral("—"), QStringLiteral("—"),
                QStringLiteral("—"), QStringLiteral("—"), QStringLiteral("—")};

    const auto &t = snapshot->timing;
    QString totalStr = durationText(t.totalMs);
    // Operator time is the tool's own work: the execute boundary for a native operator, the real
    // function call for a Python one, where the bridge cost belongs to the neighbouring stages.
    QString toolCallStr = !t.toolCallMeasured || (t.pythonBridge && !t.pythonCallMeasured)
                            ? QStringLiteral("—")
                            : durationText(t.pythonBridge ? t.pythonCallMs : t.adapterExecuteMs);
    QString prepareStr = t.prepareMeasured ? durationText(t.prepareMs) : QStringLiteral("—");
    QString initStr = t.initMeasured ? durationText(t.initMs) : QStringLiteral("—");
    QString validateStr = t.validateMeasured ? durationText(t.validateMs) : QStringLiteral("—");

    // Tooltip phases. Bridging is counted once: the GIL wait and input mapping are input
    // preparation, the output copy belongs to post-processing, and the function call is the
    // operator time. Each stage keeps its own measured flag, so an unentered stage stays "—" and a
    // real zero is still a zero.
    const bool preprocessMeasured  = t.prepareMeasured || t.initMeasured || t.pythonInputMeasured;
    const bool postprocessMeasured = t.validateMeasured || t.pythonOutputMeasured;
    const auto preprocessMs        = t.prepareMs + t.initMs + t.waitGilMs + t.pythonInputMs;
    const auto postprocessMs       = t.validateMs + t.pythonOutputMs;
    QString    preprocessStr  = preprocessMeasured ? durationText(preprocessMs) : QStringLiteral("—");
    QString    postprocessStr = postprocessMeasured ? durationText(postprocessMs) : QStringLiteral("—");

    return {totalStr, toolCallStr, preprocessStr, postprocessStr, prepareStr, initStr, validateStr};
}

QString durationText(std::optional<double> milliseconds)
{
    if (!milliseconds || !std::isfinite(*milliseconds) || *milliseconds < 0.0)
        return QStringLiteral("—");
    // One rule for every millisecond display: below one microsecond reads as zero, everything else
    // is rounded to three decimals with no trimmed zeros.
    const double value = *milliseconds < 0.001 ? 0.0 : *milliseconds;
    return QStringLiteral("%1 ms").arg(value, 0, 'f', 3);
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
    return durationText(milliseconds);
}
} // namespace visionrt
