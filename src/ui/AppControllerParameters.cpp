#include "ui/AppController_p.h"
#include "core/ValueJson.h"

#include <core/ToolValueValidation.h>

#include <cmath>
#include <limits>

namespace visionrt {

bool AppController::Impl::applyParameter(const QString &id, bool initialization, PortValue value)
{
    const auto *node = currentNode();
    if (!node)
        throw ContractError("未选择节点");
    const auto &signatures = initialization ? node->signatures.initParams : node->signatures.inputs;
    const auto signature = signatures.find(id.toStdString());
    if (signature == signatures.end())
        throw ContractError("参数不存在：" + id.toStdString());
    if (const auto *declaration = parameter(id.toStdString(), initialization))
    {
        if (!isMissing(value))
            validateToolValue(*declaration, value);
    }
    else if (signature->second.type == Type::Float && !isMissing(value)
             && !std::isfinite(std::get<double>(value)))
        throw ContractError("数值参数必须为有限值");
    return initialization
             ? editor.setInitParam(flow.toStdString(), node->id, id.toStdString(), std::move(value))
             : editor.setLiteral(flow.toStdString(), node->id, id.toStdString(), std::move(value));
}

bool AppController::setParameter(const QString &id, bool initialization, const QString &text, bool jsonInput)
{
    return impl_->command([&]
    {
        const auto *node = impl_->currentNode();
        if (!node)
            throw ContractError("未选择节点");
        const auto &signatures = initialization ? node->signatures.initParams : node->signatures.inputs;
        const auto signature = signatures.find(id.toStdString());
        if (signature == signatures.end())
            throw ContractError("参数不存在：" + id.toStdString());
        auto value = signature->second.type == Type::String && !jsonInput ? PortValue{text.toUtf8().toStdString()}
                                                                          : ValueJson::parse(text, signature->second);
        return impl_->applyParameter(id, initialization, std::move(value));
    });
}

bool AppController::setRectParameter(const QString &id, bool initialization, double x, double y, double width,
                                    double height, double angle)
{
    return impl_->command(
        [&]
        { return impl_->applyParameter(id, initialization, DataRef<ROI>(ROI{{{2., x, y, width, height, angle}}})); });
}

bool AppController::setParameterPath(const QString &id, bool initialization, const QUrl &url)
{
    return impl_->command([&]
    {
        if (!url.isLocalFile())
            throw ContractError("参数路径必须是本地文件或目录：" + id.toStdString());
        return impl_->applyParameter(id, initialization, url.toLocalFile().toUtf8().toStdString());
    });
}

bool AppController::stepIntegerParameter(const QString &id, bool initialization, int direction,
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
        const auto signature = signatures.find(id.toStdString());
        if (signature == signatures.end() || signature->second.type != Type::Int)
            throw ContractError("参数不是整数：" + id.toStdString());
        if (!initialization && node->inputs.at(id.toStdString()).mode != BindingMode::Literal)
            throw ContractError("上游绑定的整数参数不能步进：" + id.toStdString());
        const auto value = ValueJson::parse(literal, signature->second);
        if (isMissing(value))
            throw ContractError("步进前请输入有效整数");
        const auto current = std::get<std::int64_t>(value);
        std::int64_t step = 1;
        if (const auto *declaration = impl_->parameter(id.toStdString(), initialization))
        {
            validateToolValue(*declaration, value);
            if (declaration->step)
            {
                const auto *declared = std::get_if<std::int64_t>(&*declaration->step);
                if (!declared || *declared <= 0)
                    throw ContractError("整数步长必须为正数：" + id.toStdString());
                step = *declared;
            }
        }
        if ((direction > 0 && current > std::numeric_limits<std::int64_t>::max() - step)
            || (direction < 0 && current < std::numeric_limits<std::int64_t>::min() + step))
            throw ContractError("参数步进超出 int64 范围：" + id.toStdString());
        return impl_->applyParameter(id, initialization, current + direction * step);
    });
}

}
