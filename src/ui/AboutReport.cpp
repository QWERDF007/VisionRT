#include "AboutReport.h"

#include <QDateTime>
#include <QLatin1Char>
#include <QStringList>

#include <string>

namespace visionrt {
namespace {

QString sanitize(const std::string &str)
{
    const QString text = QString::fromStdString(str).trimmed();
    if (text.isEmpty() || text.compare(QStringLiteral("unknown"), Qt::CaseInsensitive) == 0
        || text.contains(QStringLiteral("NOTFOUND")))
        return QStringLiteral("未知");
    return text;
}

// 构建时刻按“当前机器的本地时区”显示并带上偏移：同一个包换到别的时区打开时，看到的是同一个
// 瞬间的当地写法，而不是某个固定时区的字符串。
QString builtAtText(const std::optional<ComponentBuildInfo> &info)
{
    if (!info || info->built_at_epoch <= 0)
        return QStringLiteral("未知");
    const auto time      = QDateTime::fromSecsSinceEpoch(info->built_at_epoch).toLocalTime();
    const auto offset    = time.offsetFromUtc();
    const auto sign      = offset < 0 ? QLatin1Char('-') : QLatin1Char('+');
    const auto magnitude = offset < 0 ? -offset : offset;
    return time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + QLatin1Char(' ') + sign
         + QStringLiteral("%1:%2")
               .arg(magnitude / 3600, 2, 10, QLatin1Char('0'))
               .arg((magnitude % 3600) / 60, 2, 10, QLatin1Char('0'));
}

} // namespace

QString aboutReportText(const AboutReport &report)
{
    QStringList lines;

    // 三个组件遵循同一条规则：版本与 Commit 来自各自编译期输入，构建时间来自构建完成阶段写入
    // 的组件元数据，缺失即“未知”。任何组件都不会借用别的组件的时间。
    lines << QStringLiteral("VisionRT");
    lines << QStringLiteral("版本          %1").arg(sanitize(GetVersionString()));
    lines << QStringLiteral("Commit        %1").arg(sanitize(GetCommitHashString()));
    lines << QStringLiteral("构建时间      %1").arg(builtAtText(report.host));
    lines << QString();

    lines << QStringLiteral("VisionTools");
    if (!report.toolsInstalled)
        lines << QStringLiteral("状态          未安装");
    else if (!report.tools.has_value())
        lines << QStringLiteral("状态          构建信息不可用");
    else
    {
        lines << QStringLiteral("版本          %1").arg(sanitize(report.tools->version));
        lines << QStringLiteral("Commit        %1").arg(sanitize(report.tools->commit));
        lines << QStringLiteral("构建时间      %1").arg(builtAtText(report.tools));
    }
    lines << QString();

    lines << QStringLiteral("VisionRTContracts");
    lines << QStringLiteral("版本          %1").arg(sanitize(report.contracts.version));
    lines << QStringLiteral("Commit        %1").arg(sanitize(report.contracts.commit));
    lines << QStringLiteral("构建时间      %1").arg(builtAtText(report.contractsBuild));
    lines << QString();

    lines << QStringLiteral("运行环境");
    lines << QStringLiteral("Qt            %1").arg(QString::fromLatin1(qVersion()));
    lines << QStringLiteral("Python        %1")
                 .arg(report.pythonAvailable ? QStringLiteral("解释器可用") : QStringLiteral("不可用"));
    lines << QString();

    lines << QStringLiteral("Copyright © VisionRT contributors");

    return lines.join(QLatin1Char('\n'));
}

} // namespace visionrt
