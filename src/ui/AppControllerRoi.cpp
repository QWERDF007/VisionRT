#include "AppController_p.h"

#include <utility>

namespace visionrt {
void AppController::Impl::finishRoi()
{
    stopRoiDraftSync();
    if (!roiSession.active())
        return;
    const int result = roiSession.resultIndex();
    roiSession.end();
    presentation.restoreImageIndex(result);
    if (auto *viewport = presentation.viewport())
        viewport->endRoiEdit();
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
        if (!roiSession.commitGeometry(target, value))
            return;
        const auto stage = roiSession.parameterStage();
        const bool changed = stage == PortStage::Train
            ? applyTrainingValue(target.inputPortId, roiSession.draft())
            : editor.setBinding(flow.toStdString(), node->id, target.inputPortId, roiSession.original(), stage);
        if (roiSession.singleRectangle())
            viewport->setDrawShape(value ? 0 : 2);
        if (changed)
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
    return impl_->beginParameterRoi(static_cast<PortId>(parameter), PortStage::Inputs, copyUpstream);
}

bool AppController::Impl::beginParameterRoi(PortId port, PortStage stage, bool copyUpstream)
{
    auto *viewport = presentation.viewport();
    if (!idle() || !viewport)
        return false;
    try
    {
        const auto *node = currentNode();
        if (stage == PortStage::Train && !ensureTrainingSession()) return false;
        const auto *tool = effectiveTool();
        const auto *declaration = tool ? workbench::port(stage == PortStage::Init ? tool->initParams : stage == PortStage::Train ? tool->trainParams : tool->inputs, port) : nullptr;
        if (!node || !declaration || !isShapeType(declaration->type.type))
            throw ContractError("参数不是 ROI：" + portLabel(port));
        const auto resolve = [&](PortRef ref)
        {
            if (ref.stage == PortStage::Train && training)
            {
                const auto it = node->trainParams.find(ref.id);
                if (it == node->trainParams.end() || it->second.mode == BindingMode::Literal)
                    return training->parameter(ref.id).value_or(PortValue{Missing{}});
            }
            return presentation.resolvedValue(*node, ref);
        };
        const auto imageValue = declaration->image ? resolve(*declaration->image) : PortValue{Missing{}};
        auto roiValue = resolve({stage, port});
        if (isMissing(roiValue))
            roiValue = defaultValue(declaration->type);
        validateValue(declaration->type, roiValue);
        auto binding = node->stageBindings(stage).at(port);
        if (stage == PortStage::Train && binding.mode == BindingMode::Literal)
            binding.literal = roiValue;
        if (binding.mode == BindingMode::Upstream && !copyUpstream)
            throw ContractError("上游 ROI 输入只读，请先显式复制当前值再编辑：" + portLabel(port));
        finishRoi();
        if (trainingMode) leaveTraining();
        RoiEditSession::Start start;
        start.node            = node->id;
        start.input           = port;
        start.kind = stage == PortStage::Init ? RoiTargetKind::Initialization : stage == PortStage::Train ? RoiTargetKind::TrainingParameter : RoiTargetKind::Input;
        start.type            = declaration->type;
        start.singleRectangle = declaration->editor == EditorKind::Rect;
        start.original        = binding;
        start.background      = imageValue;
        start.draft           = copyProjectLiteral(roiValue);
        start.displayIndex    = 0;
        start.resultIndex     = presentation.imageIndex();
        roiSession.begin(std::move(start));
        if (declaration->editor == EditorKind::Rect)
            viewport->setDrawShape(shapeRegions(roiSession.draft())->empty() ? ImageViewport::DrawRectangle : ImageViewport::DrawNone);
        updateDisplay();
        emit q->resultsChanged();
        return true;
    }
    catch (const std::exception &error)
    {
        report(error, QStringLiteral("ROI 编辑启动失败"));
        return false;
    }
}

bool AppController::startParameterRoi(int parameter, int stage, int shape, bool recordScoped)
{
    const auto owner = static_cast<PortStage>(stage);
    if (recordScoped) return owner == PortStage::Train && startTrainingRoi(parameter, shape);
    if (owner != PortStage::Inputs && owner != PortStage::Init && owner != PortStage::Train) return false;
    if (!impl_->beginParameterRoi(static_cast<PortId>(parameter), owner, true)) return false;
    setDrawShape(shape);
    return true;
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
    return impl_->roiSession.active();
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
    if (shape != ImageViewport::DrawNone && shape != ImageViewport::drawShapeForType(impl_->roiSession.type().type))
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
    if (!impl_->idle() || !impl_->ensureTrainingSession() || !impl_->training->selectRoiParameter(static_cast<PortId>(parameter))) return false;
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
    setDrawShape(shape);
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
    if (!isShapeType(declaration->type.type))
        return false;
    PortValue    emptyVal = defaultValue(declaration->type);
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
