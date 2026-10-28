#include "ui/DurationText.h"
#include "runtime/Snapshot.h"

#include <cmath>

namespace visionrt {

namespace {
// A stage prints only when its own flag says the code actually entered it. `0.000 ms` is a valid
// measurement and must survive; an unentered stage prints nothing at all.
QString stageText(bool measured, double milliseconds)
{
    if (!measured)
        return {};
    const auto text = durationText(milliseconds);
    // An invalid measurement is not printed as a placeholder either: the card and the hover card
    // only ever show numbers the runtime actually measured.
    return text == QStringLiteral("—") ? QString() : text;
}

// The node ran when it entered its prepare phase. A gated node publishes a terminal snapshot
// without entering any stage, and a node that never ran has no snapshot at all - neither may look
// like a measurement.
bool enteredExecution(const NodeSnapshot *snapshot)
{
    return snapshot && snapshot->timing.prepareMeasured;
}
} // namespace

NodeTimingText nodeTimingText(const NodeSnapshot *snapshot, bool running)
{
    // While the node is running the state lives in the header colour and the status dot. Reusing
    // the previous round's numbers would describe work that is no longer running.
    if (running || !enteredExecution(snapshot))
        return {};
    if (snapshot->disposition == NodeDisposition::Skipped || snapshot->disposition == NodeDisposition::Blocked
        || snapshot->disposition == NodeDisposition::Canceled)
        return {};

    const auto &t  = snapshot->timing;
    const auto  op = t.pythonBridge ? t.pythonCallMs : t.adapterExecuteMs;
    const bool  opMeasured = t.pythonBridge ? t.pythonCallMeasured : t.toolCallMeasured;

    NodeTimingText text;
    text.total = stageText(true, t.totalMs);
    // Operator time is the tool's own work: the execute boundary for a native operator, the real
    // function call for a Python one, where the bridge cost belongs to the neighbouring stages.
    text.toolCall = stageText(opMeasured, op);
    // Hover-card phases. Bridging is counted once: the GIL wait and input mapping are preparation,
    // the output copy belongs to post-processing, and the function call is the operator time. Only
    // stages that were actually measured contribute, so a stale field can never leak into a sum.
    const bool preprocessMeasured  = t.prepareMeasured || t.initMeasured || t.pythonInputMeasured;
    const bool postprocessMeasured = t.validateMeasured || t.pythonOutputMeasured;
    double     preprocessMs        = 0.0;
    double     postprocessMs       = 0.0;
    if (t.prepareMeasured)
        preprocessMs += t.prepareMs;
    if (t.initMeasured)
        preprocessMs += t.initMs;
    if (t.pythonInputMeasured)
        preprocessMs += t.waitGilMs + t.pythonInputMs;
    if (t.validateMeasured)
        postprocessMs += t.validateMs;
    if (t.pythonOutputMeasured)
        postprocessMs += t.pythonOutputMs;
    text.preprocess  = stageText(preprocessMeasured, preprocessMs);
    text.postprocess = stageText(postprocessMeasured, postprocessMs);
    text.prepare     = stageText(t.prepareMeasured, t.prepareMs);
    text.init        = stageText(t.initMeasured, t.initMs);
    text.validate    = stageText(t.validateMeasured, t.validateMs);
    return text;
}

QVector<NodeTimingPhase> timingPhasesOf(const NodeTimingText &text)
{
    QVector<NodeTimingPhase> phases;
    phases.reserve(3);
    if (!text.preprocess.isEmpty())
        phases.push_back({QStringLiteral("预处理"), text.preprocess});
    if (!text.toolCall.isEmpty())
        phases.push_back({QStringLiteral("算子时间"), text.toolCall});
    if (!text.postprocess.isEmpty())
        phases.push_back({QStringLiteral("后处理"), text.postprocess});
    return phases;
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
