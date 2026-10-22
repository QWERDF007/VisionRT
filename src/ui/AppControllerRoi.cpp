#include "ui/AppController_p.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>

namespace visionrt {
void AppController::Impl::finishRoi()
{
    if (!roi)
        return;
    const auto view = roi->view;
    index           = roi->resultIndex;
    roi.reset();
    if (viewport)
    {
        viewport->endRoiEdit();
        viewport->restoreViewState(view);
    }
}

void AppController::Impl::commitRoi(const RoiEditTarget &target, const ROI::value_type::value_type *value)
{
    if (!idle() || !roi || target.session != roi->id || target.nodeSeq != roi->node || target.inputPortId != roi->input
        || target.imageIndex != roi->index)
        return;
    const auto *node = currentNode();
    if (!node || node->id != target.nodeSeq)
        return;
    const auto *images = std::get_if<DataRef<Image>>(&roi->background);
    if (!images || target.imageIndex < 0 || static_cast<std::size_t>(target.imageIndex) >= images->get().size())
        return;
    try
    {
        if (roi->singleRectangle)
        {
            roi->draft = DataRef<ROI>(value ? ROI{{*value}} : ROI{});
            viewport->setDrawShape(value ? 0 : 2);
        }
        else
        {
            auto updated = std::get<DataRef<ROI>>(roi->draft).get();
            updated.resize(std::max(updated.size(), static_cast<std::size_t>(target.imageIndex) + 1));
            auto &current = updated[target.imageIndex];
            if (target.roiIndex < 0 && value)
                current.push_back(*value);
            else if (target.roiIndex >= 0 && static_cast<std::size_t>(target.roiIndex) < current.size())
            {
                if (value)
                    current[target.roiIndex] = *value;
                else
                    current.erase(current.begin() + target.roiIndex);
            }
            else
                return;
            roi->draft = DataRef<ROI>(std::move(updated));
        }
        validateValue(roi->type, roi->draft);
        InputBinding committed{BindingMode::Literal, roi->draft, std::nullopt};
        roi->original = committed;
        const bool changed_binding = editor.setBinding(roi->flow.toStdString(), roi->node, roi->input, std::move(committed));
        if (changed_binding)
        {
            emit q->projectChanged();
            emit q->parametersChanged();
        }
        emit q->resultsChanged();
    }
    catch (const std::exception &error)
    {
        report(error, QStringLiteral("ROI 编辑失败"));
    }
}

bool AppController::editRoi(const QString &parameter, bool copyUpstream)
{
    if (!impl_->idle() || !impl_->viewport)
        return false;
    try
    {
        const auto *node        = impl_->currentNode();
        const auto *declaration = impl_->parameter(parameter.toStdString(), false);
        if (!node || !declaration || declaration->type.type != Type::ROI)
            throw ContractError("参数不是可编辑的 ROI 输入：" + parameter.toStdString());
        if (declaration->imageInput.empty())
            throw ContractError("ROI 输入未声明 image_input 图像关联：" + parameter.toStdString());
        const auto  imageValue = impl_->resolvedInput(*node, declaration->imageInput);
        const auto *images     = std::get_if<DataRef<Image>>(&imageValue);
        if (declaration->editor != "rect" && (!images || images->get().empty()))
            throw ContractError("ROI 输入 " + parameter.toStdString() + " 的关联图像 " + declaration->imageInput
                                + " 没有缓存，请先运行图像源或上游");
        auto roiValue = impl_->resolvedInput(*node, parameter.toStdString());
        if (isMissing(roiValue))
            roiValue = DataRef<ROI>(ROI{});
        validateValue(declaration->type, roiValue);
        const auto binding = node->inputs.find(parameter.toStdString());
        if (binding == node->inputs.end())
            throw ContractError("找不到 ROI 输入：" + parameter.toStdString());
        if (binding->second.mode == BindingMode::Upstream && !copyUpstream)
            throw ContractError("上游 ROI 输入只读，请先显式复制当前值再编辑：" + parameter.toStdString());
        impl_->finishRoi();
        impl_->roi = Impl::RoiPreview{++impl_->roiSerial,
                                      node->id,
                                      parameter.toStdString(),
                                      impl_->flow,
                                      declaration->type,
                                      declaration->editor == "rect",
                                      binding->second,
                                      imageValue,
                                      copyProjectLiteral(roiValue),
                                      0,
                                      impl_->index,
                                      impl_->viewport->viewState()};
        if (declaration->editor == "rect")
            impl_->viewport->setDrawShape(std::get<DataRef<ROI>>(impl_->roi->draft).get().empty() ? 2 : 0);
        impl_->updateDisplay();
        emit resultsChanged();
        return true;
    }
    catch (const std::exception &error)
    {
        impl_->report(error, QStringLiteral("ROI 编辑启动失败"));
        return false;
    }
}

QVariantMap AppController::roiEditor() const
{
    if (!impl_->roi || !impl_->roi->singleRectangle)
        return {};
    const auto &session = *impl_->roi;
    QVariantList coordinates;
    const auto  &regions = std::get<DataRef<ROI>>(session.draft).get();
    if (!regions.empty() && !regions[0].empty())
        for (const auto component : std::span<const double>(regions[0][0]).subspan(1)) coordinates.push_back(component);
    const auto *images = std::get_if<DataRef<Image>>(&session.background);
    const auto *image  = images && !images->get().empty() ? &images->get()[session.index] : nullptr;
    return {
        {         "id",   QString::number(session.id)},
        {       "node", QString::number(session.node)},
        {      "input",   workbench::q(session.input)},
        {  "rectangle",                   coordinates},
        { "imageWidth",       image ? image->cols : 0},
        {"imageHeight",       image ? image->rows : 0}
    };
}

bool AppController::setRoiRectangle(const QString &session, const QVariantList &coordinates)
{
    if (!impl_->idle() || !impl_->roi || !impl_->roi->singleRectangle || session != QString::number(impl_->roi->id))
        return false;
    try
    {
        if (coordinates.size() != 5)
            throw ContractError("矩形参数 " + impl_->roi->input + " 需要 x、y、width、height、angle 五个坐标，实际 "
                                + std::to_string(coordinates.size()) + " 项");
        std::array<double, 5> values;
        for (std::size_t index = 0; index < values.size(); ++index)
        {
            bool valid = false;
            values[index] = coordinates[qsizetype(index)].toDouble(&valid);
            if (!valid || !std::isfinite(values[index]))
                throw ContractError("矩形参数 " + impl_->roi->input + " 的坐标[" + std::to_string(index)
                                    + "] 必须为有限数值");
        }
        DataRef<ROI> rectangle(ROI{{{2., values[0], values[1], values[2], values[3], values[4]}}});
        validateValue(impl_->roi->type, rectangle);
        impl_->roi->draft = std::move(rectangle);
        impl_->viewport->setDrawShape(0);
        if (impl_->roi->original.mode != BindingMode::Upstream)
        {
            InputBinding committed{BindingMode::Literal, impl_->roi->draft, impl_->roi->original.source};
            impl_->roi->original = committed;
            const bool changed_binding = impl_->editor.setBinding(impl_->roi->flow.toStdString(), impl_->roi->node, impl_->roi->input, std::move(committed));
            if (changed_binding)
            {
                emit projectChanged();
                emit parametersChanged();
            }
        }
        impl_->updateDisplay();
        emit resultsChanged();
        return true;
    }
    catch (const std::exception &error)
    {
        impl_->report(error, QStringLiteral("ROI 坐标更新失败"));
        return false;
    }
}

void AppController::setRoiFieldsValid(const QString &session, bool valid)
{
    if (impl_->roi && session == QString::number(impl_->roi->id))
        impl_->roi->fieldsValid = valid;
}

bool AppController::applyRoi()
{
    if (!impl_->idle() || !impl_->roi)
        return false;
    emit roiApplyRequested();
    if (!impl_->roi || !impl_->roi->fieldsValid)
        return false;
    try
    {
        const auto &session = *impl_->roi;
        const auto *node = impl_->currentNode();
        const auto *declaration = impl_->parameter(session.input, false);
        if (impl_->flow != session.flow || !node || node->id != session.node || !declaration
            || declaration->type != session.type)
            throw ContractError("ROI 编辑目标已失效：" + session.input);
        const auto binding = node->inputs.find(session.input);
        if (binding == node->inputs.end() || binding->second.mode != session.original.mode
            || binding->second.source != session.original.source
            || !equalValues(binding->second.literal, session.original.literal))
            throw ContractError("ROI 输入绑定已在编辑期间改变：" + session.input);
        if (!equalValues(session.background, impl_->resolvedInput(*node, declaration->imageInput)))
            throw ContractError("ROI 背景图像已在编辑期间改变：" + session.input);
        validateValue(session.type, session.draft);
        InputBinding committed{BindingMode::Literal, session.draft, std::nullopt};
        const bool changed = impl_->editor.setBinding(session.flow.toStdString(), session.node, session.input,
                                                      std::move(committed));
        impl_->finishRoi();
        if (changed)
            impl_->changed();
        else
            impl_->updateResults();
        return true;
    }
    catch (const std::exception &error)
    {
        impl_->report(error, QStringLiteral("ROI 应用失败"));
        return false;
    }
}

void AppController::endRoiPreview()
{
    impl_->finishRoi();
    impl_->updateResults();
}

QString AppController::activeRoiParameter() const
{
    return impl_->roi ? workbench::q(impl_->roi->input) : QString{};
}

int AppController::activeRoiShape() const
{
    return (impl_->roi && impl_->viewport) ? impl_->viewport->drawShape() : 0;
}

bool AppController::startRoiDraw(const QString &parameter, int shape)
{
    if (!impl_->idle())
        return false;
    const auto *node = impl_->currentNode();
    const auto *declaration = impl_->parameter(parameter.toStdString(), false);
    if (!node || !declaration)
        return false;
    if (!impl_->roi || impl_->roi->node != node->id || impl_->roi->input != parameter.toStdString())
    {
        if (!editRoi(parameter, true))
            return false;
    }
    if (impl_->viewport)
        impl_->viewport->setDrawShape(shape);
    emit resultsChanged();
    return true;
}

bool AppController::clearRoi(const QString &parameter)
{
    if (!impl_->idle())
        return false;
    const auto *node = impl_->currentNode();
    const auto *declaration = impl_->parameter(parameter.toStdString(), false);
    if (!node || !declaration)
        return false;
    if (declaration->type.type != Type::ROI)
        return false;
    PortValue    emptyVal = DataRef<ROI>(ROI{});
    InputBinding committed{BindingMode::Literal, std::move(emptyVal), {}};
    const bool changed = impl_->editor.setBinding(impl_->flow.toStdString(), node->id, parameter.toStdString(), std::move(committed));
    if (impl_->roi && impl_->roi->input == parameter.toStdString() && impl_->roi->node == node->id)
    {
        impl_->finishRoi();
    }
    if (changed)
        impl_->changed();
    else
        impl_->updateResults();
    return true;
}
} // namespace visionrt
