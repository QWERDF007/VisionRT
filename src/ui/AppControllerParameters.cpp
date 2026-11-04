#include "ui/AppController_p.h"
#include "core/ValueJson.h"

#include "core/InputNormalizer.h"

#include <core/ToolValueValidation.h>

#include <cmath>
#include <limits>

namespace visionrt {

bool AppController::Impl::applyParameter(PortId id, bool initialization, PortValue value)
{
    const auto *node = currentNode();
    if (!node)
        throw ContractError("未选择节点");
    const auto &signatures = initialization ? node->signatures.initParams : node->signatures.inputs;
    const auto signature = signatures.find(id);
    if (signature == signatures.end())
        throw ContractError("参数不存在：" + portLabel(id));
    if (const auto *declaration = parameter(id, initialization))
    {
        // Manual edits run through the same normalisation as upstream values: declared ranges
        // clamp and options fall back to the declared default. Missing stays Missing in the
        // project; the effective value is produced when the tool runs.
        if (!isMissing(value))
        {
            auto normalized = normalizeInput(*declaration, value);
            if (!normalized.adjustment.empty())
                report(QString::fromStdString(portLabel(id) + "：" + normalized.adjustment));
            value = std::move(normalized.value);
        }
    }
    else if (signature->second.type == Type::Float && !isMissing(value)
             && !std::isfinite(std::get<double>(value)))
        throw ContractError("数值参数必须为有限值");
    return initialization
             ? editor.setInitParam(flow.toStdString(), node->id, id, std::move(value))
             : editor.setLiteral(flow.toStdString(), node->id, id, std::move(value));
}

bool AppController::setParameter(int port, bool initialization, const QString &text, bool jsonInput)
{
    return impl_->command([&]
    {
        const auto *node = impl_->currentNode();
        if (!node)
            throw ContractError("未选择节点");
        const auto &signatures = initialization ? node->signatures.initParams : node->signatures.inputs;
        const auto signature = signatures.find(static_cast<PortId>(port));
        if (signature == signatures.end())
            throw ContractError("参数不存在：" + portLabel(static_cast<PortId>(port)));
        auto value = signature->second.type == Type::String && !jsonInput ? PortValue{text.toUtf8().toStdString()}
                                                                          : ValueJson::parse(text, signature->second);
        return impl_->applyParameter(static_cast<PortId>(port), initialization, std::move(value));
    });
}

bool AppController::setRectParameter(int port, bool initialization, double x, double y, double width,
                                    double height, double angle)
{
    return impl_->command(
        [&] { return impl_->applyParameter(static_cast<PortId>(port), initialization,
                                       DataRef<ROI>(ROI{{makeRectangle(x, y, width, height, angle)}})); });
}

bool AppController::setParameterPath(int port, bool initialization, const QString &path)
{
    return impl_->command([&]
    {
        return impl_->applyParameter(static_cast<PortId>(port), initialization, path.toUtf8().toStdString());
    });
}

bool AppController::setParameterPath(int port, bool initialization, const QUrl &url)
{
    return impl_->command([&]
    {
        if (!url.isLocalFile())
            throw ContractError("参数路径必须是本地文件或目录：" + portLabel(static_cast<PortId>(port)));
        return impl_->applyParameter(static_cast<PortId>(port), initialization, url.toLocalFile().toUtf8().toStdString());
    });
}

bool AppController::stepIntegerParameter(int port, bool initialization, int direction,
                                        const QString &literal)
{
    if (direction != 1 && direction != -1)
        return false;
    return impl_->command([&]
    {
        const auto *node = impl_->currentNode();
        if (!node)
            throw ContractError("未选择节点");
        const auto &signatures = initialization ? node->signatures.initParams : node->signatures.inputs;
        const auto id = static_cast<PortId>(port);
        const auto signature = signatures.find(id);
        if (signature == signatures.end() || signature->second.type != Type::Int)
            throw ContractError("参数不是整数：" + portLabel(id));
        if (!initialization && node->inputs.at(id).mode != BindingMode::Literal)
            throw ContractError("上游绑定的整数参数不能步进：" + portLabel(id));
        const auto value = ValueJson::parse(literal, signature->second);
        if (isMissing(value))
            throw ContractError("步进前请输入有效整数");
        std::int64_t current = std::get<std::int64_t>(value);
        std::int64_t step    = 1;
        const auto  *declaration = impl_->parameter(static_cast<PortId>(port), initialization);
        if (declaration)
        {
            current = std::get<std::int64_t>(normalizeInput(*declaration, value).value);
            if (declaration->step)
            {
                const auto *declared = std::get_if<std::int64_t>(&*declaration->step);
                if (!declared || *declared <= 0)
                    throw ContractError("整数步长必须为正数：" + portLabel(id));
                step = *declared;
            }
        }
        if ((direction > 0 && current > std::numeric_limits<std::int64_t>::max() - step)
            || (direction < 0 && current < std::numeric_limits<std::int64_t>::min() + step))
            throw ContractError("参数步进超出 int64 范围：" + portLabel(id));
        const std::int64_t target = current + direction * step;
        // The stepped value is clamped by the same declaration rule instead of being rejected.
        return impl_->applyParameter(id, initialization, target);
    });
}

}
