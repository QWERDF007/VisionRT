#include "AppController_p.h"

#include <utility>

namespace visionrt {
void AppController::Impl::finishRoi()
{
    if (!roiSession.active())
        return;
    const auto view   = roiSession.view();
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
            if (trainingMode)
            {
                ++trainingSerial;
                clearTrainingFit();
                assist->invalidate();
                emit q->trainingChanged();
            }
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

bool AppController::editRoi(int parameter, bool copyUpstream)
{
    auto *viewport = impl_->presentation.viewport();
    if (!impl_->idle() || !viewport)
        return false;
    try
    {
        const auto *node        = impl_->currentNode();
        const auto  port        = static_cast<PortId>(parameter);
        const auto *declaration = impl_->parameter(port, false);
        if (!node || !declaration || declaration->type.type != Type::ROI)
            throw ContractError("参数不是可编辑的 ROI 输入：" + portLabel(port));
        if (!declaration->image)
            throw ContractError("ROI 输入未声明关联图像：" + portLabel(port));
        const auto imageValue = impl_->resolvedInput(*node, declaration->image->id);
        auto       roiValue   = impl_->resolvedInput(*node, port);
        if (isMissing(roiValue))
            roiValue = DataRef<ROI>(ROI{});
        validateValue(declaration->type, roiValue);
        const auto binding = node->inputs.find(port);
        if (binding == node->inputs.end())
            throw ContractError("找不到 ROI 输入：" + portLabel(port));
        if (binding->second.mode == BindingMode::Upstream && !copyUpstream)
            throw ContractError("上游 ROI 输入只读，请先显式复制当前值再编辑：" + portLabel(port));
        impl_->finishRoi();
        RoiEditSession::Start start;
        start.node            = node->id;
        start.input           = port;
        start.flow            = impl_->flow;
        start.type            = declaration->type;
        start.singleRectangle = declaration->editor == EditorKind::Rect;
        start.original        = binding->second;
        start.background      = imageValue;
        start.draft           = copyProjectLiteral(roiValue);
        start.displayIndex    = 0;
        start.resultIndex     = impl_->presentation.imageIndex();
        start.view            = viewport->viewState();
        impl_->roiSession.begin(std::move(start));
        if (declaration->editor == EditorKind::Rect)
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

void AppController::endRoiPreview()
{
    impl_->finishRoi();
    impl_->updateResults();
}

int AppController::activeRoiParameter() const
{
    return impl_->roiSession.active() ? static_cast<int>(impl_->roiSession.input()) : 0;
}

bool AppController::shapeToolsAvailable() const
{
    if (!impl_->editingAllowed())
        return false;
    return impl_->trainingMode ? impl_->training->selected() >= 0 : impl_->roiSession.active();
}

int AppController::drawShape() const
{
    if (!shapeToolsAvailable())
        return 0;
    auto *viewport = impl_->presentation.viewport();
    return viewport ? viewport->drawShape() : 0;
}

void AppController::setDrawShape(int shape)
{
    if (shape < 0 || shape > 5 || !shapeToolsAvailable())
        return;
    // A training record is a list of rectangles whatever the tool does with them, so the palette
    // offers only the rectangle there instead of collecting regions the asset cannot store.
    if (impl_->trainingMode && shape != 0 && shape != 2)
        return;
    auto *viewport = impl_->presentation.viewport();
    if (!viewport)
        return;
    viewport->setDrawShape(shape);
}

int AppController::activeRoiShape() const
{
    auto *viewport = impl_->presentation.viewport();
    return (impl_->roiSession.active() && viewport) ? viewport->drawShape() : 0;
}

bool AppController::startTrainingRoi(int parameter, int shape)
{
    if (!impl_->idle() || !impl_->ensureTrainingSession() || impl_->training->roiParameter() != static_cast<PortId>(parameter)) return false;
    if (!impl_->trainingMode) setTrainingMode(true);
    impl_->seatTrainingRegions();
    setDrawShape(shape);
    return shapeToolsAvailable();
}

bool AppController::startRoiDraw(int parameter, int shape)
{
    if (!impl_->idle())
        return false;
    const auto *node        = impl_->currentNode();
    const auto  port        = static_cast<PortId>(parameter);
    const auto *declaration = impl_->parameter(port, false);
    if (!node || !declaration)
        return false;
    if (!impl_->roiSession.active() || impl_->roiSession.node() != node->id || impl_->roiSession.input() != port)
    {
        if (!editRoi(parameter, true))
            return false;
    }
    if (auto *viewport = impl_->presentation.viewport())
        viewport->setDrawShape(shape);
    emit resultsChanged();
    return true;
}

bool AppController::clearRoi(int parameter)
{
    if (!impl_->idle())
        return false;
    const auto *node        = impl_->currentNode();
    const auto  port        = static_cast<PortId>(parameter);
    const auto *declaration = impl_->parameter(port, false);
    if (!node || !declaration)
        return false;
    if (declaration->type.type != Type::ROI)
        return false;
    PortValue    emptyVal = DataRef<ROI>(ROI{});
    InputBinding committed{BindingMode::Literal, std::move(emptyVal), {}};
    const bool   changed = impl_->editor.setBinding(impl_->flow.toStdString(), node->id, port, std::move(committed));
    if (impl_->roiSession.active() && impl_->roiSession.input() == port && impl_->roiSession.node() == node->id)
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
