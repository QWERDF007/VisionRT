#include "ui/AppController_p.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>

namespace visionrt {
void AppController::Impl::finishRoi()
{
    if (!roiSession.active())
        return;
    const auto view = roiSession.view();
    const int  result = roiSession.resultIndex();
    roiSession.end();
    presentation.restoreImageIndex(result);
    if (auto *viewport = presentation.viewport())
    {
        viewport->endRoiEdit();
        viewport->restoreViewState(view);
    }
}

void AppController::Impl::commitRoi(const RoiEditTarget &target, const EncodedRegion *value)
{
    auto *viewport = presentation.viewport();
    if (!idle() || !roiSession.active() || !viewport)
        return;
    const auto *node = currentNode();
    if (!node || node->id != target.nodeSeq)
        return;
    try
    {
        const auto outcome = roiSession.commitGeometry(target, value);
        if (!outcome.accepted)
            return;
        if (roiSession.singleRectangle())
            viewport->setDrawShape(value ? 0 : 2);
        if (outcome.changedBinding)
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
    auto *viewport = impl_->presentation.viewport();
    if (!impl_->idle() || !viewport)
        return false;
    try
    {
        const auto *node        = impl_->currentNode();
        const auto *declaration = impl_->parameter(parameter.toStdString(), false);
        if (!node || !declaration || declaration->type.type != Type::ROI)
            throw ContractError("参数不是可编辑的 ROI 输入：" + parameter.toStdString());
        if (!declaration->image)
            throw ContractError("ROI 输入未声明关联图像：" + parameter.toStdString());
        const auto  imageValue = impl_->resolvedInput(*node, declaration->image->port);
        const auto *images     = std::get_if<DataRef<Image>>(&imageValue);
        if (declaration->editor != "rect" && (!images || images->get().empty()))
            throw ContractError("ROI 输入 " + parameter.toStdString() + " 的关联图像 " + declaration->image->port
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
        RoiEditSession::Start start;
        start.node            = node->id;
        start.input           = parameter.toStdString();
        start.flow            = impl_->flow;
        start.type            = declaration->type;
        start.singleRectangle = declaration->editor == "rect";
        start.original        = binding->second;
        start.background      = imageValue;
        start.draft           = copyProjectLiteral(roiValue);
        start.displayIndex    = 0;
        start.resultIndex     = impl_->presentation.imageIndex();
        start.view            = viewport->viewState();
        impl_->roiSession.begin(std::move(start));
        if (declaration->editor == "rect")
            viewport->setDrawShape(std::get<DataRef<ROI>>(impl_->roiSession.draft()).get().empty() ? 2 : 0);
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
    return impl_->roiSession.editor();
}

bool AppController::setRoiRectangle(const QString &session, const QVariantList &coordinates)
{
    if (!impl_->idle() || !impl_->roiSession.active() || !impl_->roiSession.singleRectangle()
        || session != QString::number(impl_->roiSession.id()))
        return false;
    try
    {
        if (coordinates.size() != 5)
            throw ContractError("矩形参数 " + impl_->roiSession.input() + " 需要 x、y、width、height、angle 五个坐标，实际 "
                                + std::to_string(coordinates.size()) + " 项");
        std::array<double, 5> values;
        for (std::size_t index = 0; index < values.size(); ++index)
        {
            bool valid = false;
            values[index] = coordinates[qsizetype(index)].toDouble(&valid);
            if (!valid || !std::isfinite(values[index]))
                throw ContractError("矩形参数 " + impl_->roiSession.input() + " 的坐标[" + std::to_string(index)
                                    + "] 必须为有限数值");
        }
        const auto outcome = impl_->roiSession.commitRectangle(values);
        if (!outcome.accepted)
            return false;
        if (auto *viewport = impl_->presentation.viewport())
            viewport->setDrawShape(0);
        if (outcome.changedBinding)
        {
            emit projectChanged();
            emit parametersChanged();
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
    if (impl_->roiSession.active() && session == QString::number(impl_->roiSession.id()))
        impl_->roiSession.setFieldsValid(valid);
}

bool AppController::applyRoi()
{
    if (!impl_->idle() || !impl_->roiSession.active())
        return false;
    emit roiApplyRequested();
    if (!impl_->roiSession.active() || !impl_->roiSession.fieldsValid())
        return false;
    try
    {
        const auto  input       = impl_->roiSession.input();
        const auto  nodeValue   = impl_->roiSession.node();
        const auto  flow        = impl_->roiSession.flow();
        const auto *node        = impl_->currentNode();
        const auto *declaration = impl_->parameter(input, false);
        if (impl_->flow != flow || !node || node->id != nodeValue || !declaration
            || declaration->type != impl_->roiSession.type())
            throw ContractError("ROI 编辑目标已失效：" + input);
        const auto binding = node->inputs.find(input);
        if (binding == node->inputs.end() || binding->second.mode != impl_->roiSession.original().mode
            || binding->second.source != impl_->roiSession.original().source
            || !equalValues(binding->second.literal, impl_->roiSession.original().literal))
            throw ContractError("ROI 输入绑定已在编辑期间改变：" + input);
        if (!equalValues(impl_->roiSession.background(), impl_->resolvedInput(*node, declaration->image->port)))
            throw ContractError("ROI 背景图像已在编辑期间改变：" + input);
        validateValue(impl_->roiSession.type(), impl_->roiSession.draft());
        InputBinding committed{BindingMode::Literal, impl_->roiSession.draft(), std::nullopt};
        const bool   changed
            = impl_->editor.setBinding(flow.toStdString(), nodeValue, input, std::move(committed));
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
    return impl_->roiSession.active() ? QString::fromStdString(impl_->roiSession.input()) : QString{};
}

int AppController::activeRoiShape() const
{
    auto *viewport = impl_->presentation.viewport();
    return (impl_->roiSession.active() && viewport) ? viewport->drawShape() : 0;
}

bool AppController::startRoiDraw(const QString &parameter, int shape)
{
    if (!impl_->idle())
        return false;
    const auto *node = impl_->currentNode();
    const auto *declaration = impl_->parameter(parameter.toStdString(), false);
    if (!node || !declaration)
        return false;
    if (!impl_->roiSession.active() || impl_->roiSession.node() != node->id
        || impl_->roiSession.input() != parameter.toStdString())
    {
        if (!editRoi(parameter, true))
            return false;
    }
    if (auto *viewport = impl_->presentation.viewport())
        viewport->setDrawShape(shape);
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
    if (impl_->roiSession.active() && impl_->roiSession.input() == parameter.toStdString()
        && impl_->roiSession.node() == node->id)
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
