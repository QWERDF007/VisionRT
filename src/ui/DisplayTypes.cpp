#include "ui/DisplayTypes.h"

namespace visionrt {

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
        return QStringLiteral("区域");
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

        QString dtypeStr;
        if (type.dtype)
        {
            switch (*type.dtype)
            {
            case DType::String:  dtypeStr = QStringLiteral("字符串"); break;
            case DType::UInt8:   dtypeStr = QStringLiteral("无符号整数（8位）"); break;
            case DType::Int32:   dtypeStr = QStringLiteral("整数（32位）"); break;
            case DType::Int64:   dtypeStr = QStringLiteral("整数（64位）"); break;
            case DType::Float32: dtypeStr = QStringLiteral("浮点数（32位）"); break;
            case DType::Float64: dtypeStr = QStringLiteral("浮点数（64位）"); break;
            case DType::Bool:    dtypeStr = QStringLiteral("布尔值"); break;
            }
        }
        return rankStr + dtypeStr + QStringLiteral("数组");
    }
    }
    return QString::fromLatin1(typeName(type.type));
}

} // namespace visionrt
