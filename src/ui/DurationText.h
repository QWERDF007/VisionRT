#pragma once

#include <QString>
#include <QVector>

#include <optional>

namespace visionrt {

struct NodeSnapshot;

// What a node shows about time. An unavailable measurement is the empty string, never a dash: the
// node card, the hover card and the copy actions all read this one struct, so "no measurement" has
// exactly one representation.
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

// One hover-card row. Only stages that were actually measured produce a row, so a running or gated
// node has no timing rows at all while a measured 0.000 ms stays a real row.
struct NodeTimingPhase
{
    QString label;
    QString value;
};

NodeTimingText nodeTimingText(const NodeSnapshot *snapshot, bool running);
// Hover-card rows built from the same struct the node card uses, so both surfaces can never
// disagree about which stage was measured.
QVector<NodeTimingPhase> timingPhasesOf(const NodeTimingText &text);

// Single display rule shared by node cards and the hover card. A missing,
// negative or non-finite measurement is reported as unavailable instead of a
// fabricated zero.
QString durationText(std::optional<double> milliseconds);

// Format session duration as HH:MM:SS:mmm
QString formatElapsed(double milliseconds);

// Format milliseconds as "xxx ms" or "— ms"
QString formatMs(std::optional<double> milliseconds);

} // namespace visionrt
