#include "ui/NodeDiagnostics.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <exception>
#include <utility>

namespace visionrt {
namespace {
std::pair<const char *, const char *> phaseNames(NodePhase phase)
{
    switch (phase)
    {
    case NodePhase::Create:
        return {"create", "创建实例"};
    case NodePhase::Prepare:
        return {"prepare", "准备输入"};
    case NodePhase::Initialize:
        return {"initialize", "初始化"};
    case NodePhase::Execute:
        return {"execute", "执行"};
    case NodePhase::Convert:
        return {"convert", "校验输出"};
    case NodePhase::Release:
        return {"release", "释放"};
    }
    std::terminate();
}
}

QString diagnosticMessage(const NodeDiagnostic &diagnostic)
{
    auto message = QString::fromStdString(diagnostic.status.message);
    if (message.trimmed().isEmpty() && diagnostic.status.code != 0)
        message = QStringLiteral("操作失败（错误码：%1）").arg(diagnostic.status.code);
    return QStringLiteral("节点 %1（%2）%3：%4")
        .arg(diagnostic.node)
        .arg(QString::fromStdString(diagnostic.toolId), QString::fromUtf8(phaseNames(diagnostic.phase).second),
             message);
}

QString diagnosticJson(const NodeDiagnostic &diagnostic, const NodeDiagnostic *lifecycle)
{
    const auto encode = [](const NodeDiagnostic &source)
    {
        return QJsonObject{
            {   "node",                      QString::number(source.node)},
            {"tool_id",             QString::fromStdString(source.toolId)},
            {  "phase", QString::fromUtf8(phaseNames(source.phase).first)},
            {   "code",           static_cast<qint64>(source.status.code)},
            {"message",     QString::fromStdString(source.status.message)},
            { "detail",      QString::fromStdString(source.status.detail)}
        };
    };
    auto object = encode(diagnostic);
    if (lifecycle)
        object.insert("lifecycle", encode(*lifecycle));
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
}
}
