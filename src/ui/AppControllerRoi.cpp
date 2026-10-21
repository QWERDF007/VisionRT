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

void AppController::Impl::commitRoi(const RoiEditTarget &target, const Roi *value)
{
    if (!idle() || !roi || target.session != roi->id || target.nodeSeq != roi->node || target.inputPortId != roi->input
        || target.imageIndex != roi->index)
        return;
    const auto *node = currentNode();
    if (!node || node->id != target.nodeSeq)
        return;
    const auto *images = std::get_if<ImageBatch>(&roi->background);
    if (!images || target.imageIndex < 0 || static_cast<std::size_t>(target.imageIndex) >= images->images().size())
        return;
    try
    {
        if (roi->type.type == Type::Rect)
        {
            PortValue rectangle = value ? PortValue{*value} : PortValue{Missing{}};
            validateValue(roi->type, rectangle);
            roi->draft = std::move(rectangle);
            viewport->setDrawShape(value ? 0 : 2);
        }
        else
        {
            auto draft = std::get<RoiBatch>(roi->draft).images();
            std::vector<std::vector<Roi>> updated(draft.begin(), draft.end());
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
            roi->draft = RoiBatch(std::move(updated));
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
        report(workbench::q(error.what()));
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
        if (!node || !declaration
            || (declaration->type.type != Type::RoiBatch && declaration->type.type != Type::Rect))
            throw ContractError("Selected parameter is not an ROI input");
        if (declaration->imageInput.empty())
            throw ContractError("ROI input has no image_input association");
        const auto  imageValue = impl_->resolvedInput(*node, declaration->imageInput);
        const auto *images     = std::get_if<ImageBatch>(&imageValue);
        if (declaration->type.type != Type::Rect && (!images || images->images().empty()))
            throw ContractError("No input image cache; run the image source/upstream first");
        auto roiValue = impl_->resolvedInput(*node, parameter.toStdString());
        if (isMissing(roiValue) && declaration->type.type == Type::RoiBatch)
            roiValue = RoiBatch{};
        validateValue(declaration->type, roiValue);
        const auto binding = node->inputs.find(parameter.toStdString());
        if (binding == node->inputs.end())
            throw ContractError("Unknown ROI input");
        if (binding->second.mode == BindingMode::Upstream && !copyUpstream)
            throw ContractError("Upstream ROI is read-only; explicitly copy current value for editing first");
        impl_->finishRoi();
        impl_->roi = Impl::RoiPreview{++impl_->roiSerial, node->id, parameter.toStdString(), impl_->flow, declaration->type,
                                      binding->second, imageValue, std::move(roiValue), 0,
                                      impl_->index, impl_->viewport->viewState()};
        if (declaration->type.type == Type::Rect)
            impl_->viewport->setDrawShape(isMissing(impl_->roi->draft) ? 2 : 0);
        impl_->updateDisplay();
        emit resultsChanged();
        return true;
    }
    catch (const std::exception &error)
    {
        impl_->report(workbench::q(error.what()));
        return false;
    }
}

QVariantMap AppController::roiEditor() const
{
    if (!impl_->roi || impl_->roi->type.type != Type::Rect)
        return {};
    const auto &session = *impl_->roi;
    QVariantList coordinates;
    if (const auto *rectangle = std::get_if<Roi>(&session.draft))
        for (const auto component : rectangle->encoded().subspan(1))
            coordinates.push_back(double(component));
    const auto *images = std::get_if<ImageBatch>(&session.background);
    const auto *image = images && !images->images().empty() ? &images->images()[session.index] : nullptr;
    return {{"id", QString::number(session.id)},
            {"node", QString::number(session.node)}, {"input", workbench::q(session.input)},
            {"rectangle", coordinates}, {"imageWidth", image ? image->cols() : 0},
            {"imageHeight", image ? image->rows() : 0}};
}

bool AppController::setRoiRectangle(const QString &session, const QVariantList &coordinates)
{
    if (!impl_->idle() || !impl_->roi || impl_->roi->type.type != Type::Rect
        || session != QString::number(impl_->roi->id))
        return false;
    try
    {
        if (coordinates.size() != 5)
            throw ContractError("Rectangle requires x, y, width, height and angle");
        std::array<float, 5> values;
        for (std::size_t index = 0; index < values.size(); ++index)
        {
            bool valid = false;
            values[index] = coordinates[qsizetype(index)].toFloat(&valid);
            if (!valid || !std::isfinite(values[index]))
                throw ContractError("Rectangle coordinates must be finite numbers");
        }
        impl_->roi->draft = Roi::rectangle(values[0], values[1], values[2], values[3], values[4]);
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
        impl_->report(workbench::q(error.what()));
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
            throw ContractError("ROI edit target is no longer valid");
        const auto binding = node->inputs.find(session.input);
        if (binding == node->inputs.end() || binding->second.mode != session.original.mode
            || binding->second.source != session.original.source
            || !equalValues(binding->second.literal, session.original.literal))
            throw ContractError("ROI input binding changed during editing");
        if (!equalValues(session.background, impl_->resolvedInput(*node, declaration->imageInput)))
            throw ContractError("ROI background image changed during editing");
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
        impl_->report(workbench::q(error.what()));
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
    PortValue emptyVal = declaration->type.type == Type::Rect ? PortValue{Missing{}} : PortValue{RoiBatch{}};
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
