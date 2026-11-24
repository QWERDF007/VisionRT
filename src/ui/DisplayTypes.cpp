#include "ui/DisplayTypes.h"
#include <visionrt/contracts/ROI.h>
#include <QStringList>
#include <set>

namespace visionrt {

QString displayDtypeName(DType dtype)
{
    switch (dtype)
    {
    case DType::UInt8: return QStringLiteral("无符号整数");
    case DType::Int32: return QStringLiteral("整数");
    case DType::Int64: return QStringLiteral("长整数");
    case DType::Float32: return QStringLiteral("单精度浮点数");
    case DType::Float64: return QStringLiteral("浮点数");
    case DType::Bool: return QStringLiteral("布尔值");
    case DType::String: return QStringLiteral("字符串");
    }
    return {};
}

QString displayTypeName(const TypeSpec &type)
{
    switch (type.type)
    {
    case Type::Int:
        return QStringLiteral("整数");
    case Type::Float:
        return QStringLiteral("浮点数");
    case Type::Bool:
        return QStringLiteral("布尔值");
    case Type::String:
        return QStringLiteral("字符串");
    case Type::Image:
        return QStringLiteral("图像");
    case Type::ROI:
        return QStringLiteral("几何");
    case Type::Array:
    {
        QString rankStr;
        switch (type.rank)
        {
        case 1: rankStr = QStringLiteral("一维"); break;
        case 2: rankStr = QStringLiteral("二维"); break;
        case 3: rankStr = QStringLiteral("三维"); break;
        case 4: rankStr = QStringLiteral("四维"); break;
        default: rankStr = (type.rank > 0) ? QStringLiteral("%1维").arg(type.rank) : QString(); break;
        }

        const auto dtypeStr = type.dtype ? displayDtypeName(*type.dtype) : QString{};
        return rankStr + dtypeStr + QStringLiteral("数组");
    }
    }
    return QString::fromLatin1(typeName(type.type));
}

QString displayTypeName(const TypeSpec &type, const PortValue &value)
{
    if (type.type != Type::ROI)
        return displayTypeName(type);
    std::set<RoiKind> kinds;
    if (const auto *geometry = std::get_if<DataRef<ROI>>(&value))
        for (const auto &group : geometry->get())
            for (const auto &record : group)
                kinds.insert(static_cast<RoiKind>(record.front()));
    QStringList names;
    for (const auto kind : kinds)
        switch (kind)
        {
        case RoiKind::Point: names.append(QStringLiteral("点")); break;
        case RoiKind::Segment: names.append(QStringLiteral("线")); break;
        case RoiKind::Rectangle: names.append(QStringLiteral("矩形")); break;
        case RoiKind::Circle: names.append(QStringLiteral("圆")); break;
        case RoiKind::Polygon: names.append(QStringLiteral("多边形")); break;
        }
    return names.empty() ? displayTypeName(type) : names.join(QStringLiteral("、"));
}

} // namespace visionrt
