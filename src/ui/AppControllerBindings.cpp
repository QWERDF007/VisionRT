#include "ui/AppController_p.h"

namespace visionrt {
using workbench::nodeId;
using workbench::q;

QVariantMap AppController::portContext(const QString &port, bool output) const
{
    const auto *flow = impl_->currentFlow();
    const auto *node = impl_->currentNode();
    if (!flow || !node)
        return {};
    QVariantMap context{{"project", QString::number(impl_->projectEpoch)},
                        {"flow", impl_->flow},
                        {"node", QString::number(node->id)},
                        {"port", port},
                        {"output", output},
                        {"interaction", QString::number(impl_->roiSerial)},
                        {"structure", QString::number(flow->structureRevision)},
                        {"revision", QString::number(node->executionRevision)}};
    if (output)
    {
        const auto captured = impl_->outputs.capture(port);
        if (!captured)
            return {};
        context.insert("publication", QString::number(captured->publication));
    }
    else if (!node->inputs.contains(port.toStdString()))
        return {};
    return context;
}

bool AppController::portContextCurrent(const QVariantMap &context) const
{
    return !context.isEmpty()
           && context == portContext(context.value("port").toString(), context.value("output").toBool());
}

QVariantList AppController::bindingCandidates(const QVariantMap &context) const
{
    if (context.value("output").toBool())
        return {};
    QVariantList result;
    if (!portContextCurrent(context))
        return result;
    const auto *node = impl_->currentNode();
    if (!node)
        return result;
    try
    {
        for (const auto &source : impl_->editor.compatibleSources(
                 impl_->flow.toStdString(), node->id, context.value("port").toString().toStdString()))
        {
            const auto *parent = impl_->findNode(source.node);
            if (!parent)
                continue;
            const auto *definition = impl_->registry.find(parent->toolId);
            const auto *port       = definition ? workbench::port(definition->outputs, source.port) : nullptr;
            result.push_back(QVariantMap{
                { "node",            QString::number(source.node)},
                {"title",                      q(parent->title())},
                { "port",                          q(source.port)},
                { "name",   port ? q(port->name) : q(source.port)},
                { "type", typeText(parent->signatures.outputs.at(source.port))}
            });
        }
    }
    catch (const std::exception &error)
    {
        impl_->report(error, QStringLiteral("输入关联候选读取失败"));
    }
    return result;
}

bool AppController::bindInput(const QVariantMap &context, const QString &node, const QString &output)
{
    if (context.value("output").toBool() || !portContextCurrent(context))
        return false;
    const auto input = context.value("port").toString();
    return impl_->command(
        [&]
        {
            const auto *target = impl_->currentNode();
            if (!target)
                throw ContractError("未选择节点");
            auto binding   = target->inputs.at(input.toStdString());
            binding.mode   = BindingMode::Upstream;
            binding.source = SourcePort{nodeId(node), output.toStdString()};
            impl_->finishRoi();
            return impl_->editor.setBinding(impl_->flow.toStdString(), target->id, input.toStdString(),
                                            std::move(binding));
        });
}

bool AppController::unbindInput(const QVariantMap &context)
{
    if (context.value("output").toBool() || !portContextCurrent(context))
        return false;
    const auto input = context.value("port").toString();
    return impl_->command(
        [&]
        {
            const auto *node = impl_->currentNode();
            if (!node)
                throw ContractError("未选择节点");
            impl_->finishRoi();
            return impl_->editor.unbind(impl_->flow.toStdString(), node->id, input.toStdString());
        });
}

QVariantList AppController::relationTargets(const QVariantMap &context) const
{
    if (!context.value("output").toBool())
        return {};
    QVariantList result;
    if (!portContextCurrent(context))
        return result;
    const auto *node = impl_->currentNode();
    const auto *flow = impl_->currentFlow();
    if (!node || !flow)
        return result;
    try
    {
        const SourcePort source{node->id, context.value("port").toString().toStdString()};
        auto inputs = impl_->editor.compatibleInputs(impl_->flow.toStdString(), source);
        for (const auto &relation : portRelations(*flow).active)
            if (relation.source == source)
                inputs.push_back(relation.target);
        for (const auto &input : inputs)
        {
            const auto *target = impl_->findNode(input.node);
            if (!target)
                continue;
            const auto *definition = impl_->registry.find(target->toolId);
            const auto *port       = definition ? workbench::port(definition->inputs, input.port) : nullptr;
            result.push_back(QVariantMap{
                { "node",             QString::number(input.node)},
                {"title",                      q(target->title())},
                { "port",                           q(input.port)},
                { "name",    port ? q(port->name) : q(input.port)},
                { "type", port ? typeText(port->type) : QString{}},
                {"connected", target->inputs.at(input.port).mode == BindingMode::Upstream
                                  && target->inputs.at(input.port).source == source}
            });
        }
    }
    catch (const std::exception &error)
    {
        impl_->report(error, QStringLiteral("输出关联目标读取失败"));
    }
    return result;
}

bool AppController::bindOutputTo(const QVariantMap &context, const QString &node, const QString &input)
{
    if (!context.value("output").toBool() || !portContextCurrent(context))
        return false;
    const auto output = context.value("port").toString();
    return impl_->command(
        [&]
        {
            const auto *source = impl_->currentNode();
            if (!source)
                throw ContractError("未选择节点");
            return impl_->editor.bindFreeInput(impl_->flow.toStdString(), {source->id, output.toStdString()},
                                               {nodeId(node), input.toStdString()});
        });
}

bool AppController::unbindOutputFrom(const QVariantMap &context, const QString &node, const QString &input)
{
    if (!context.value("output").toBool() || !portContextCurrent(context))
        return false;
    return impl_->command(
        [&]
        {
            const auto *source = impl_->currentNode();
            const auto *target = impl_->findNode(nodeId(node));
            if (!source)
                throw ContractError("未选择节点");
            if (!target)
                throw ContractError("找不到节点：" + node.toStdString());
            const auto binding = target->inputs.find(input.toStdString());
            if (binding == target->inputs.end() || binding->second.mode != BindingMode::Upstream
                || binding->second.source != SourcePort{source->id, context.value("port").toString().toStdString()})
                throw ContractError("输入端口未连接到当前输出：" + node.toStdString() + "." + input.toStdString());
            return impl_->editor.unbind(impl_->flow.toStdString(), target->id, input.toStdString());
        });
}

} // namespace visionrt
