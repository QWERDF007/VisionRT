#include "ui/Diagnostics.h"

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

const char *phaseName(RuntimePhase phase)
{
    switch (phase)
    {
    case RuntimePhase::Setup:
        return "setup";
    case RuntimePhase::Submission:
        return "submission";
    case RuntimePhase::Completion:
        return "completion";
    case RuntimePhase::Management:
        return "management";
    case RuntimePhase::Notification:
        return "notification";
    }
    std::terminate();
}

QString summary(const QString &message)
{
    return message;
}

QString summary(const ToolStatus &status)
{
    auto message = QString::fromStdString(status.message);
    if (message.trimmed().isEmpty() && status.code != 0)
        message = QStringLiteral("操作失败（错误码：%1）").arg(status.code);
    return message;
}

QString summary(const NodeDiagnostic &diagnostic)
{
    return diagnosticMessage(diagnostic);
}

QString summary(const RuntimeDiagnostic &diagnostic)
{
    return std::visit([](const auto &entry) { return summary(entry); }, diagnostic.payload);
}

QJsonObject encode(const QString &message)
{
    return {
        {"message", message}
    };
}

QJsonObject encode(const ToolStatus &status)
{
    return {
        {   "code",       static_cast<qint64>(status.code)},
        {"message", QString::fromStdString(status.message)},
        { "detail",  QString::fromStdString(status.detail)}
    };
}

QJsonObject encode(const NodeDiagnostic &diagnostic)
{
    auto object = encode(diagnostic.status);
    object.insert("node", QString::number(diagnostic.node));
    object.insert("tool_id", QString::fromStdString(diagnostic.toolId));
    object.insert("phase", QString::fromUtf8(phaseNames(diagnostic.phase).first));
    return object;
}

QJsonObject encode(const RuntimeDiagnostic &diagnostic)
{
    auto object = std::visit([](const auto &entry) { return encode(entry); }, diagnostic.payload);
    object.insert("generation", QString::number(diagnostic.generation));
    object.insert("sequence", QString::number(diagnostic.sequence));
    object.insert("runtime_phase", QString::fromUtf8(phaseName(diagnostic.phase)));
    return object;
}
}

QString diagnosticMessage(const NodeDiagnostic &diagnostic)
{
    return QStringLiteral("节点 %1（%2）%3：%4")
        .arg(diagnostic.node)
        .arg(QString::fromStdString(diagnostic.toolId), QString::fromUtf8(phaseNames(diagnostic.phase).second),
             summary(diagnostic.status));
}

QString diagnosticMessage(const ApplicationDiagnostic &diagnostic)
{
    const auto message = std::visit([](const auto &entry) { return summary(entry); }, diagnostic);
    return message.size() > 16384 ? message.left(16360) + QStringLiteral("…[诊断已截断]") : message;
}

QJsonObject diagnosticObject(const ApplicationDiagnostic &diagnostic)
{
    return std::visit([](const auto &entry) { return encode(entry); }, diagnostic);
}

QString diagnosticJson(const NodeDiagnostic &diagnostic, const NodeDiagnostic *lifecycle)
{
    auto object = encode(diagnostic);
    if (lifecycle)
        object.insert("lifecycle", encode(*lifecycle));
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
}
}
