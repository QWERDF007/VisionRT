#pragma once

#include <QString>

#include <optional>

namespace visionrt {

struct NodeSnapshot;

struct NodeTimingText
{
    QString total;
    QString toolCall;
    QString preprocess = QStringLiteral("—");
    QString postprocess = QStringLiteral("—");
    QString prepare = QStringLiteral("—");
    QString init = QStringLiteral("—");
    QString validate = QStringLiteral("—");
};

NodeTimingText nodeTimingText(const NodeSnapshot *snapshot, bool running);

// Single display rule shared by node cards and the hover card. A missing,
// negative or non-finite measurement is reported as unavailable instead of a
// fabricated zero.
QString durationText(std::optional<double> milliseconds);

// Format session duration as HH:MM:SS:mmm
QString formatElapsed(double milliseconds);

// Format milliseconds as "xxx ms" or "— ms"
QString formatMs(std::optional<double> milliseconds);

} // namespace visionrt
