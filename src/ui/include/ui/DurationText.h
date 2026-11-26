#pragma once

#include "ui/Export.h"

#include <QString>
#include <QVector>

#include <optional>

namespace visionrt {

struct NodeSnapshot;

// Measured timing text. Empty fields indicate absent measurements; hover rows format them as zero.
struct NodeTimingText
{
    QString total;
    QString toolCall;
    QString preprocess;
    QString postprocess;
    QString prepare;
    QString init;
    QString validate;
};

// Hover always has three rows; absent measurements are displayed as 0.000 ms.
struct NodeTimingPhase
{
    QString label;
    QString value;
};

VISIONRT_UI_API NodeTimingText nodeTimingText(const NodeSnapshot *snapshot);
// Hover-card rows built from the same struct the node card uses, so both surfaces can never
// disagree about which stage was measured.
VISIONRT_UI_API QVector<NodeTimingPhase> timingPhasesOf(const NodeTimingText &text);

// Single display rule shared by node cards and the hover card. A missing,
// negative or non-finite measurement is reported as unavailable instead of a
// fabricated zero.
VISIONRT_UI_API QString durationText(std::optional<double> milliseconds);

// Format session duration as HH:MM:SS:mmm
VISIONRT_UI_API QString formatElapsed(double milliseconds);

// Format milliseconds as "xxx ms" or "— ms"
VISIONRT_UI_API QString formatMs(std::optional<double> milliseconds);

} // namespace visionrt
