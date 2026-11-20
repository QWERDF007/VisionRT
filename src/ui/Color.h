#pragma once
#include <QColor>
#include <QObject>

namespace visionrt {
class VisionRTColor final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QColor NodeGreen READ nodeGreen CONSTANT)
    Q_PROPERTY(QColor NodeOrange READ nodeOrange CONSTANT)
    Q_PROPERTY(QColor InputROI READ inputRoi CONSTANT)
    Q_PROPERTY(QColor OutputROI READ outputRoi CONSTANT)
public:
    using QObject::QObject;
    QColor nodeGreen() const { return QColor(0x1a, 0x7f, 0x37); }
    QColor nodeOrange() const { return QColor(0xff, 0x88, 0x00); }
    QColor inputRoi() const { return QColor(0x00, 0xbe, 0xbe); }
    QColor outputRoi() const { return QColor(0x88, 0x00, 0xbe); }
};
}
