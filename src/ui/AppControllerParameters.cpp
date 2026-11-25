#include "AppController_p.h"
#include "core/InputNormalizer.h"
#include "core/ValueJson.h"

#include <core/ToolValueValidation.h>
#include <QFile>
#include <opencv2/imgcodecs.hpp>

#include <cmath>
#include <limits>

namespace visionrt {

std::string AppController::Impl::parameterLabel(PortId id, bool initialization) const
{
    const auto *declaration = parameter(id, initialization);
    return declaration ? declaration->nameZh : "未声明的参数（编号 " + std::to_string(id) + "）";
}

bool AppController::Impl::applyParameter(PortId id, bool initialization, PortValue value)
{
    const auto *node = currentNode();
    if (!node)
        throw ContractError("未选择节点");
    const auto &signatures = initialization ? node->signatures.initParams : node->signatures.inputs;
    const auto  signature  = signatures.find(id);
    if (signature == signatures.end())
        throw ContractError("参数不存在：" + parameterLabel(id, initialization));
    if (const auto *declaration = parameter(id, initialization))
    {
        // Manual edits run through the same normalisation as upstream values: declared ranges
        // clamp and invalid options are refused. Missing stays Missing in the
        // project; the effective value is produced when the tool runs.
        if (!isMissing(value))
        {
            auto normalized = normalizeInput(*declaration, value);
            if (!normalized.adjustment.empty())
                report(QString::fromStdString(parameterLabel(id, initialization) + "：" + normalized.adjustment));
            value = std::move(normalized.value);
        }
    }
    else if (signature->second.type == Type::Float && !isMissing(value) && !std::isfinite(std::get<double>(value)))
        throw ContractError("数值参数必须为有限值");
    const bool changed = initialization ? editor.setInitParam(flow.toStdString(), node->id, id, std::move(value))
                                        : editor.setLiteral(flow.toStdString(), node->id, id, std::move(value));
    return changed;
}

bool AppController::setParameter(int port, bool initialization, const QString &text, bool jsonInput)
{
    return impl_->command(
        [&]
        {
            const auto *node = impl_->currentNode();
            if (!node)
                throw ContractError("未选择节点");
            const auto &signatures = initialization ? node->signatures.initParams : node->signatures.inputs;
            const auto  signature  = signatures.find(static_cast<PortId>(port));
            if (signature == signatures.end())
                throw ContractError("参数不存在：" + impl_->parameterLabel(static_cast<PortId>(port), initialization));
            auto value = signature->second.type == Type::String && !jsonInput
                           ? PortValue{text.toUtf8().toStdString()}
                           : ValueJson::parse(text, signature->second);
            return impl_->applyParameter(static_cast<PortId>(port), initialization, std::move(value));
        });
}

bool AppController::setParameterPath(int port, bool initialization, const QString &path)
{
    return impl_->command(
        [&] { return impl_->applyParameter(static_cast<PortId>(port), initialization, path.toUtf8().toStdString()); });
}

bool AppController::setParameterPath(int port, bool initialization, const QUrl &url)
{
    return impl_->command(
        [&]
        {
            if (!url.isLocalFile())
                throw ContractError("参数路径必须是本地文件或目录："
                                    + impl_->parameterLabel(static_cast<PortId>(port), initialization));
            return impl_->applyParameter(static_cast<PortId>(port), initialization,
                                         url.toLocalFile().toUtf8().toStdString());
        });
}

bool AppController::loadParameterImage(int parameter, int stage, const QUrl &url, const QString &record)
{
    return impl_->command([&]
    {
        const auto owner = static_cast<PortStage>(stage);
        if (owner != PortStage::Init && owner != PortStage::Train && owner != PortStage::Inputs) return false;
        if (!url.isLocalFile()) throw ContractError("请选择本地图像文件");
        if (owner == PortStage::Train && !impl_->ensureTrainingSession()) return false;
        QFile file(url.toLocalFile());
        if (!file.open(QIODevice::ReadOnly)) throw ContractError("无法读取图像文件");
        const auto bytes = file.readAll();
        const cv::Mat buffer(1, static_cast<int>(bytes.size()), CV_8U, const_cast<char *>(bytes.constData()));
        auto pixels = cv::imdecode(buffer, cv::IMREAD_UNCHANGED);
        if (pixels.empty()) throw ContractError("图像文件无法解码");
        impl_->finishRoi();
        PortValue value = DataRef<Image>(Image{std::move(pixels)});
        const auto id = static_cast<PortId>(parameter);
        if (owner == PortStage::Train) return impl_->applyTrainingValue(id, std::move(value), record);
        if (owner == PortStage::Inputs)
            return impl_->editor.setBinding(impl_->flow.toStdString(), impl_->currentNode()->id, id, {BindingMode::Literal, std::move(value), {}});
        return impl_->applyParameter(id, true, std::move(value));
    });
}

bool AppController::stepIntegerParameter(int port, bool initialization, int direction, const QString &literal, int stage, const QString &record)
{
    if (direction != 1 && direction != -1)
        return false;
    return impl_->command(
        [&]
        {
            const auto *node = impl_->currentNode();
            if (!node)
                throw ContractError("未选择节点");
            const bool training = stage == static_cast<int>(PortStage::Train);
            if (training && !impl_->ensureTrainingSession()) return false;
            const auto &signatures = training ? node->signatures.trainParams : initialization ? node->signatures.initParams : node->signatures.inputs;
            const auto  id         = static_cast<PortId>(port);
            const auto  signature  = signatures.find(id);
            const auto *declaration = training
                ? workbench::port(record.isEmpty() ? impl_->training->toolParameters() : impl_->training->recordParameters(), id)
                : impl_->parameter(id, initialization);
            const auto *type = training && !record.isEmpty() ? declaration ? &declaration->type : nullptr
                : signature == signatures.end() ? nullptr : &signature->second;
            if (!type || type->type != Type::Int)
                throw ContractError("参数不是整数：" + impl_->parameterLabel(id, initialization));
            if (!training && !initialization && node->inputs.at(id).mode != BindingMode::Literal)
                throw ContractError("上游绑定的整数参数不能步进：" + impl_->parameterLabel(id, initialization));
            const auto value = ValueJson::parse(literal, *type);
            if (isMissing(value))
                throw ContractError("步进前请输入有效整数");
            std::int64_t current     = std::get<std::int64_t>(value);
            std::int64_t step        = 1;
            if (declaration)
            {
                current = std::get<std::int64_t>(normalizeInput(*declaration, value).value);
                if (declaration->step)
                {
                    const auto *declared = std::get_if<std::int64_t>(&*declaration->step);
                    if (!declared || *declared <= 0)
                        throw ContractError("整数步长必须为正数：" + impl_->parameterLabel(id, initialization));
                    step = *declared;
                }
            }
            if ((direction > 0 && current > std::numeric_limits<std::int64_t>::max() - step)
                || (direction < 0 && current < std::numeric_limits<std::int64_t>::min() + step))
                throw ContractError("参数步进超出 int64 范围：" + impl_->parameterLabel(id, initialization));
            const std::int64_t target = current + direction * step;
            // The stepped value is clamped by the same declaration rule instead of being rejected.
            if (training)
            {
                impl_->applyTrainingValue(id, declaration ? normalizeInput(*declaration, PortValue{target}).value : PortValue{target}, record);
                return false;
            }
            return impl_->applyParameter(id, initialization, target);
        });
}

} // namespace visionrt
