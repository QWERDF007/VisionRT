#pragma once
#include <QColor>
#include <QObject>
#include "common/Singleton.h"

namespace visionrt {
class VisionRTColor final : public QObject
{
    Q_OBJECT
    VISIONRT_QML_SINGLETON(VisionRTColor)
    Q_PROPERTY(QColor NodeGrey READ nodeGrey CONSTANT)
    Q_PROPERTY(QColor NodeRed READ nodeRed CONSTANT)
    Q_PROPERTY(QColor NodeGreen READ nodeGreen CONSTANT)
    Q_PROPERTY(QColor NodeLine READ nodeLine CONSTANT)
    Q_PROPERTY(QColor NodeInputLine READ nodeInputLine CONSTANT)
    Q_PROPERTY(QColor NodeOutputLine READ nodeOutputLine CONSTANT)
    Q_PROPERTY(QColor InputROI READ inputRoi CONSTANT)
    Q_PROPERTY(QColor OutputROI READ outputRoi CONSTANT)
public:
    QColor nodeGrey() const { return QColor(0xcf, 0xcf, 0xcf); }
    QColor nodeRed() const { return QColor(0xed, 0x1c, 0x24); }
    QColor nodeGreen() const { return QColor(0x1a, 0x7f, 0x37); }
    QColor nodeLine() const { return QColor(0xcf, 0xcf, 0xcf); }
    QColor nodeInputLine() const { return QColor(0xff, 0x88, 0x00); }
    QColor nodeOutputLine() const { return QColor(0x00, 0x66, 0xb4); }
    QColor inputRoi() const { return QColor(0x00, 0xbe, 0xbe); }
    QColor outputRoi() const { return QColor(0x88, 0x00, 0xbe); }
private:
    explicit VisionRTColor(QObject *parent = nullptr) : QObject(parent) {}
};
}
