#include "AppController_p.h"
#include "core/ExampleProject.h"
#include "core/InputNormalizer.h"
#include "core/NodeContracts.h"
#include "core/ToolPaths.h"
#include "core/ToolValueValidation.h"
#include "core/ValueJson.h"
#include "log/LogService.h"
#include "ui/DisplayConverter.h"
#include "ui/TrainingAssets.h"

#include <QImage>
#include <algorithm>
#include <string>

namespace visionrt {
namespace {

// A node's published revisions. A named project owns them beside its file, so copying the project
// copies what it runs; an unnamed project or an installed example has no such home and publishes
// into the workspace until the user saves a copy of its own.
std::filesystem::path trainingBase(const ProjectDefinition &project, const NodeDefinition &node,
                                   const ToolDefinition &tool, const std::filesystem::path &applicationDirectory)
{
    if (!project.path.empty() && !ExampleProject::owns(applicationDirectory, project.path))
        return training::projectBase(project.path, project.name, node.id);
    const auto name = QStringLiteral("training/%1-%2-%3")
                          .arg(QString::fromStdString(training::assetName(project.name)))
                          .arg(node.id)
                          .arg(QString::fromStdString(tool.nameEn));
    return softwareDataDir() / std::filesystem::path(name.toStdWString());
}

std::optional<TrainingFieldValue> trainingValue(const TypeSpec &type, const QVariant &value)
{
    if (type.type == Type::String)
        return value.toString().toStdString();
    try
    {
        return trainingFieldValueOf(ValueJson::parse(value.toString().toUtf8(), type));
    }
    catch (const ContractError &)
    {
        return std::nullopt;
    }
}
} // namespace

const TrainingSpec *AppController::Impl::trainingSpec() const
{
    const auto *tool = effectiveTool();
    return tool && tool->training ? &*tool->training : nullptr;
}

bool AppController::Impl::ensureTrainingSession()
{
    const auto *spec = trainingSpec();
    const auto *node = currentNode();
    if (!spec || !node)
        return false;
    auto      &view  = trainingViews[node->id];
    const auto asset = node->trainingAsset.empty() ? std::filesystem::path{} : node->trainingAsset / L"manifest.json";
    // An unsaved draft may already name the asset it will be written to; only a project that points
    // somewhere else forces a rebind.
    const bool load
        = !view.session.active() || view.session.spec() != *spec || (!asset.empty() && view.session.asset() != asset);
    if (training != &view.session || load)
        ++trainingSerial;
    training = &view.session;
    if (load)
    {
        // A project saved under another name moves its asset reference with it. The records the user
        // has not applied yet are still theirs and are in no file to read back, so the session
        // follows the new path instead of starting over.
        if (!asset.empty() && view.session.active() && view.session.spec() == *spec && view.session.dirty())
            view.session.setAsset(asset);
        else
            training->begin({node->id, *spec, asset, effectiveTool()->trainParams, node->trainParams});
        if (!training->error().isEmpty())
        {
            LogService::write(LogLevel::Error, LogSource::Ui, QStringLiteral("训练数据：") + training->error());
            return false;
        }
    }
    return true;
}

bool AppController::Impl::requestTraining(const ToolDefinition &tool, const TrainingSpec &spec)
{
    const auto *node = currentNode();
    if (!node)
        return false;
    if (!training->validate())
    {
        LogService::write(LogLevel::Error, LogSource::Ui, QStringLiteral("训练数据：") + training->error());
        return false;
    }

    try
    {
        Values inputs;
        for (const auto &[id, value] : training->parameters()) inputs.emplace(id, portValueOf(value));
        AssistRequest request;
        request.context = {projectEpoch, generation, flow.toStdString(), node->id, node->executionRevision};
        request.context.trainingSerial = trainingSerial;
        request.training               = training->trainingSet();
        request.learn                  = true;
        request.inputs                 = std::move(inputs);
        if (!assist->request(request, std::make_shared<const ToolRegistry>(registry), tool))
            throw ContractError("已有计算尚未结束，请稍后重试");
        pendingTraining = request.context;
        editor.setBusy(true);
        assistEnabled = false;
        if (auto *viewport = presentation.viewport())
            viewport->setAssistActive(false);
        emit q->runtimeChanged();
        emit q->assistChanged();
        return true;
    }
    catch (const std::exception &error)
    {
        LogService::write(LogLevel::Error, LogSource::Ui,
                          QStringLiteral("训练失败：") + QString::fromUtf8(error.what()));
        return false;
    }
}

void AppController::Impl::completeTraining(const AssistOutcome &outcome)
{
    const auto expected = *pendingTraining;
    pendingTraining.reset();
    editor.setBusy(state != RuntimeState::Idle);
    const auto *node    = currentNode();
    const auto *definition = effectiveTool();
    const auto tool = definition ? std::optional<ToolDefinition>{*definition} : std::nullopt;
    const bool  current = outcome.context == expected && projectEpoch == expected.projectEpoch && node
                      && node->id == expected.node && node->executionRevision == expected.executionRevision
                      && trainingSerial == expected.trainingSerial && training && tool && tool->training;
    if (current)
    {
        auto result = ToolResult{outcome.outputs, outcome.status.detail, outcome.artifacts};
        if (outcome.status) publishTraining(*tool, *tool->training, result);
        if (result.outputs.empty()) result = toolResult({}, outcome.status);
        auto snapshot = std::make_shared<NodeSnapshot>();
        snapshot->node = expected.node;
        snapshot->toolId = tool->nameEn;
        snapshot->generation = generation;
        snapshot->executionRevision = currentNode()->executionRevision;
        snapshot->invocation = ProducedBy::Train;
        snapshot->outputs = std::move(result.outputs);
        snapshot->status = validateToolExecution(tool->outputs, snapshot->outputs, result.detail, ProducedBy::Train);
        snapshot->toolResultPublished = true;
        presentation.present(expected.node, std::move(snapshot));
        updateResults();
        showTrainingFit();
    }
    assistToolId.clear();
    assist->releaseSession();
    emit q->trainingChanged();
    emit q->runtimeChanged();
    refreshAssist();
}

bool AppController::Impl::publishTraining(const ToolDefinition &tool, const TrainingSpec &spec, ToolResult &result)
{
    const auto *node = currentNode();
    const auto fail = [&](const QString &detail)
    {
        result = toolResult({}, makeStatus(ErrorCode::Internal, "训练模型保存失败", detail.toStdString()));
        LogService::write(LogLevel::Error, LogSource::Ui, detail);
        return false;
    };
    try
    {
        if (result.artifacts.size() != spec.artifacts.size()) throw ContractError("训练模型产物数量不匹配");
        std::map<std::string, std::filesystem::path> destinations;
        const auto &project = editor.project();
        const auto base = project.path.empty() ? trainingBase(project, *node, tool, applicationDirectory)
                                                : project.path.parent_path();
        for (const auto &artifact : spec.artifacts)
        {
            const auto found = result.artifacts.find(artifact.id);
            if (found == result.artifacts.end() || found->second.bytes.empty())
                throw ContractError("训练模型不能为空：" + artifact.id);
            const auto value = training->parameter(artifact.target.id);
            const auto path = pathFromUtf8(std::get<std::string>(*value));
            if (!path.empty()) destinations.emplace(artifact.id, path.is_absolute() ? path : base / path);
        }
        std::optional<training::Revision> publication;
        // Prepare the complete revision before committing model references to the project.
        publication.emplace(trainingBase(project, *node, tool, applicationDirectory), *training, result.artifacts, destinations);
        std::vector<LiteralWrite> writes;
        for (const auto &[id, value] : training->parameters()) writes.push_back({PortStage::Train, id, portValueOf(value)});
        for (const auto &artifact : spec.artifacts)
        {
            auto path = destinations.contains(artifact.id) ? destinations.at(artifact.id)
                : publication->directory() / L"models" / pathFromUtf8(artifact.id + ".bin");
            if (!project.path.empty()) path = path.lexically_relative(project.path.parent_path());
            const auto text = workbench::pathText(path).toStdString();
            std::ranges::find(writes, artifact.target.id, &LiteralWrite::port)->value = text;
            result.outputs.insert_or_assign(artifact.sourcePort, text);
        }
        if (!editor.setLiteralWrites(flow.toStdString(), node->id, writes, [&] { return publication->directory(); }))
            throw ContractError("训练引用提交失败");
        publication->accept();
        for (const auto &write : writes) training->setParameter(write.port, trainingFieldValueOf(write.value));
        training->adoptAsset(publication->directory() / L"manifest.json");
        emit q->projectChanged();
        emit q->parametersChanged();
        return true;
    }
    catch (const std::filesystem::filesystem_error &failure)
    {
        return fail(QStringLiteral("%1：%2").arg(workbench::pathText(failure.path1()),
            QString::fromLocal8Bit(failure.code().message().c_str())));
    }
    catch (const std::exception &failure)
    {
        return fail(QString::fromUtf8(failure.what()));
    }
}

void AppController::Impl::showTrainingFit()
{
    if (!trainingMode) return;
    const auto *node = currentNode();
    const auto *spec = trainingSpec();
    const auto snapshot = node ? presentation.displaySnapshot(*node) : ResultSlot::Snapshot{};
    RegionGroup fit;
    if (snapshot && snapshot->invocation == ProducedBy::Train && snapshot->status && spec && spec->overlayOutput)
    {
        const auto found = snapshot->outputs.find(spec->overlayOutput);
        if (found != snapshot->outputs.end())
            if (const auto *roi = std::get_if<DataRef<ROI>>(&found->second); roi && !roi->get().empty())
                fit = roi->get().front();
    }
    if (auto *viewport = presentation.viewport()) viewport->setOverlays({}, std::move(fit));
}

void AppController::Impl::clearTrainingFit()
{
    if (trainingMode)
        if (auto *viewport = presentation.viewport()) viewport->setOverlays({}, {});
}

void AppController::Impl::showTrainingDraftRecord()
{
    if (pendingTraining)
        return;
    ++trainingSerial;
    clearAssistPreview();
    assist->invalidate();
    auto *viewport = presentation.viewport();
    if (!viewport)
        return;
    // The picture on the canvas is about to change: a line measured from the previous record would
    // now describe an input the user is no longer looking at.
    clearTrainingFit();
    const auto &records = training->records();
    const int   index   = training->selected();
    if (index < 0 || index >= static_cast<int>(records.size()))
    {
        viewport->endRoiEdit();
        viewport->setHostFrame({});
        trainingAssistImage = {};
        return;
    }
    const auto &record = records[index];
    const auto  stored = std::find_if(training->images().begin(), training->images().end(),
                                      [&](const TrainingDraftImage &image) { return image.id == record.image; });
    const auto  frame  = stored == training->images().end()
                           ? DisplayFrame{}
                           : DisplayConverter::convert(stored->pixels, stored->colorOrder);
    if (frame.image.isNull())
    {
        LogService::write(LogLevel::Error, LogSource::Ui,
                          QStringLiteral("无法显示训练图：%1")
                              .arg(frame.error.isEmpty() ? QStringLiteral("草稿中没有这张原图") : frame.error));
        viewport->endRoiEdit();
        viewport->setHostFrame({});
        trainingAssistImage = {};
        return;
    }
    // The assistant reads the same picture the user is pointing at; the stored frame stays the
    // trainer's source and this conversion only feeds the canvas.
    trainingAssistImage = stored->pixels;
    viewport->setHostFrame(frame);
    viewport->fitContain();
    viewport->setDrawShape(0);
    // The record's rectangles are edited through the same ROI mechanism the processing view uses;
    // only the owner in the target differs.
    seatTrainingRegions();
    showTrainingFit();
    refreshAssist();
}

void AppController::Impl::seatTrainingRegions()
{
    auto     *viewport = presentation.viewport();
    const int index    = training->selected();
    if (!viewport || index < 0 || index >= static_cast<int>(training->records().size()))
        return;
    RoiEditSession::Start start;
    start.node       = training->node();
    start.kind       = RoiTargetKind::Training;
    start.input      = training->roiParameter();
    start.flow       = flow;
    start.type       = TypeSpec{Type::ROI};
    start.background = DataRef<Image>(Image{trainingAssistImage});
    start.draft      = DataRef<ROI>(ROI{training->regions(index)});
    start.original   = {BindingMode::Literal, start.draft, {}};
    start.view       = viewport->viewState();
    roiSession.begin(std::move(start));
    viewport->beginRoiEdit(roiSession.target(), training->regions(index), false, false);
}

void AppController::Impl::applyTrainingAssist(const AssistOutcome &outcome)
{
    if (!trainingMode || outcome.context.trainingSerial != trainingSerial || !training->active())
    {
        clearAssistPreview();
        LogService::write(LogLevel::Warn, LogSource::Ui, QStringLiteral("智能点选：训练数据已变化，请重新点选"));
        return;
    }
    // A preview started on one record must not land on another after the user selects a different
    // picture; the serial still matches because the session did not change, the record did.
    if (outcome.context.trainingRecord >= 0 && outcome.context.trainingRecord != training->selected())
    {
        clearAssistPreview();
        LogService::write(LogLevel::Warn, LogSource::Ui, QStringLiteral("智能点选：训练记录已切换，请重新点选"));
        return;
    }
    const int index = training->selected();
    if (index < 0 || index >= static_cast<int>(training->records().size()))
        return;
    if (!outcome.valid || outcome.proposal.empty() || outcome.proposal.front().empty())
    {
        clearAssistPreview();
        LogService::write(LogLevel::Warn, LogSource::Ui,
                          QStringLiteral("智能点选失败：")
                              + (outcome.quality.isEmpty() ? QStringLiteral("未找到有效目标") : outcome.quality));
        return;
    }
    auto regions = training->regions(index);
    if (outcome.append)
    {
        // A declaration may cap the canvas field, so a click that would exceed the budget is
        // refused with the same rule the manual draw obeys; the user deletes one explicitly.
        const int budget = training->regionBudget();
        if (budget > 0 && static_cast<int>(regions.size() + outcome.proposal.front().size()) > budget)
        {
            clearAssistPreview();
            LogService::write(LogLevel::Warn, LogSource::Ui,
                              QStringLiteral("智能点选：训练记录最多 %1 个矩形，请先删除多余的矩形").arg(budget));
            return;
        }
        regions.insert(regions.end(), outcome.proposal.front().begin(), outcome.proposal.front().end());
    }
    else
        regions = outcome.proposal.front();
    auto draft = *training;
    draft.setRegions(index, regions);
    std::vector<LiteralWrite> writes;
    for (const auto &mapping : parameterLocator()->parameters)
    {
        const auto &value  = outcome.outputs.at(mapping.sourcePort);
        const auto *target = workbench::port(effectiveTool()->trainParams, mapping.target.id);
        try
        {
            if (isMissing(value))
                throw ContractError("定位算法未提供参数：" + target->nameZh);
            validateToolValue(*target, value);
        }
        catch (const ContractError &error)
        {
            LogService::write(LogLevel::Warn, LogSource::Ui, QString::fromUtf8(error.what()));
            return;
        }
        draft.setParameter(mapping.target.id, trainingFieldValueOf(value));
        writes.push_back({PortStage::Train, mapping.target.id, value});
    }
    try
    {
        const bool edited = editor.setLiteralWrites(flow.toStdString(), currentNode()->id, writes);
        *training         = std::move(draft);
        if (edited)
            changed();
        else
            trainingEdited();
    }
    catch (const ContractError &error)
    {
        clearAssistPreview();
        LogService::write(LogLevel::Warn, LogSource::Ui, QString::fromUtf8(error.what()));
    }
}

void AppController::Impl::trainingEdited()
{
    ++trainingSerial;
    clearTrainingFit();
    clearAssistPreview();
    assist->invalidate();
    seatTrainingRegions();
    emit q->trainingChanged();
    emit q->parametersChanged();
}

void AppController::Impl::commitTrainingRoi(const RoiEditTarget &target, const EncodedRegion *value)
{
    if (!trainingMode || target.kind != RoiTargetKind::Training || target.session != roiSession.id()
        || target.nodeSeq != training->node())
        return;
    roiSession.cancelPreview();
    const int index = training->selected();
    if (index < 0 || index >= static_cast<int>(training->records().size()))
        return;
    auto regions = training->regions(index);
    if (!value)
    {
        if (target.roiIndex < 0 || target.roiIndex >= static_cast<int>(regions.size()))
            return;
        regions.erase(regions.begin() + target.roiIndex);
    }
    else if (target.roiIndex < 0)
    {
        const int budget = training->regionBudget();
        if (budget > 0 && static_cast<int>(regions.size()) >= budget)
        {
            LogService::write(LogLevel::Warn, LogSource::Ui,
                              QStringLiteral("训练数据：这条记录最多 %1 个矩形，请先删除或调整已有矩形").arg(budget));
            return;
        }
        regions.push_back(*value);
    }
    else if (target.roiIndex < static_cast<int>(regions.size()))
        regions[target.roiIndex] = *value;
    else
        return;
    training->setRegions(index, std::move(regions));
    trainingEdited();
    // A committed rectangle ends the create gesture; drawing another is one explicit click away.
    if (auto *viewport = presentation.viewport())
        viewport->setDrawShape(0);
    emit q->trainingChanged();
}

void AppController::Impl::leaveTraining()
{
    if (!trainingMode)
        return;
    clearTrainingFit();
    roiSession.end();
    if (auto *viewport = presentation.viewport())
        viewport->endRoiEdit();
    presentation.setSuspended(false);
    trainingMode        = false;
    trainingAssistImage = {};
    if (assist)
        assist->invalidate();
    assistEnabled = false;
    if (auto *viewport = presentation.viewport())
        viewport->setAssistActive(false);
    emit q->trainingChanged();
    updateDisplay();
    emit q->resultsChanged();
}

bool AppController::trainingAvailable() const
{
    return impl_->trainingSpec() != nullptr;
}

bool AppController::trainingMode() const
{
    return impl_->trainingMode;
}

void AppController::setTrainingMode(bool value)
{
    if (value == impl_->trainingMode || !impl_->idle())
        return;
    if (!value)
    {
        impl_->trainingViews.at(impl_->training->node()).visible = false;
        impl_->leaveTraining();
        return;
    }
    if (!trainingAvailable())
        return;
    if (!impl_->ensureTrainingSession())
        return;
    impl_->presentation.setSuspended(true);
    impl_->trainingMode                                      = true;
    impl_->trainingViews.at(impl_->training->node()).visible = true;
    ++impl_->trainingSerial;
    impl_->showTrainingDraftRecord();
    impl_->refreshAssist();
    emit trainingChanged();
    emit resultsChanged();
}

QVariantList AppController::trainingRecords() const
{
    return impl_->training ? impl_->training->rows() : QVariantList{};
}

QString AppController::selectedTrainingDraftRecord() const
{
    if (!impl_->training)
        return {};
    const int   index   = impl_->training->selected();
    const auto &records = impl_->training->records();
    if (index < 0 || index >= static_cast<int>(records.size()))
        return {};
    return records[index].id;
}

bool AppController::trainingDirty() const
{
    return impl_->training && impl_->training->dirty();
}

bool AppController::trainingSavable() const
{
    return impl_->training && impl_->training->savable();
}

bool AppController::addTrainingImage()
{
    if (!canAddTrainingImage())
        return false;
    const auto *spec = impl_->trainingSpec();
    const auto *node = impl_->currentNode();
    if (!spec || !node)
        return false;
    const auto *value = impl_->presentation.resolvedInput(*node, spec->image.id);
    const auto *batch = value ? std::get_if<DataRef<Image>>(value) : nullptr;
    const int   index = impl_->presentation.imageIndex();
    if (!batch || batch->get().empty() || index < 0 || static_cast<std::size_t>(index) >= batch->get().size())
    {
        LogService::write(LogLevel::Warn, LogSource::Ui, QStringLiteral("训练数据：声明的图像端口还没有发布可用的帧"));
        emit trainingChanged();
        return false;
    }
    if (!impl_->ensureTrainingSession())
    {
        emit trainingChanged();
        return false;
    }
    const auto &binding = node->inputs.at(spec->image.id);
    const auto snapshot = effectiveSource(binding) ? impl_->presentation.sharedSnapshot(binding.source->node) : nullptr;
    if (!snapshot)
    {
        LogService::write(LogLevel::Warn, LogSource::Ui, QStringLiteral("训练原图缺少发布身份，请运行图像来源"));
        return false;
    }
    const auto source = QStringLiteral("%1:%2:%3:%4:%5:%6:%7")
                            .arg(snapshot->generation)
                            .arg(snapshot->session)
                            .arg(snapshot->round)
                            .arg(snapshot->publication)
                            .arg(binding.source->node)
                            .arg(binding.source->port)
                            .arg(index);
    const auto *producerNode = impl_->findNode(binding.source->node);
    const auto *producer     = producerNode ? impl_->registry.find(producerNode->toolId) : nullptr;
    if (!producer)
    {
        LogService::write(LogLevel::Warn, LogSource::Ui, QStringLiteral("训练原图的工具声明缺失"));
        return false;
    }
    const auto  producerContract = nodeContract(*producerNode, *producer);
    const auto *sourcePort       = workbench::port(producerContract.outputs, binding.source->port);
    if (!sourcePort)
    {
        LogService::write(LogLevel::Warn, LogSource::Ui, QStringLiteral("训练原图的输出声明缺失"));
        return false;
    }
    const auto order = sourcePort->colorOrder;
    const auto added = impl_->training->addImage(batch->get()[static_cast<std::size_t>(index)], order, source);
    if (!added)
    {
        LogService::write(LogLevel::Error, LogSource::Ui, QStringLiteral("训练数据：") + impl_->training->error());
        emit trainingChanged();
        return false;
    }
    impl_->presentation.setSuspended(true);
    impl_->trainingMode                                      = true;
    impl_->trainingViews.at(impl_->training->node()).visible = true;
    ++impl_->trainingSerial;
    impl_->showTrainingDraftRecord();
    emit trainingChanged();
    emit resultsChanged();
    return true;
}

bool AppController::canAddTrainingImage() const
{
    const auto *viewport = impl_->presentation.viewport();
    const auto *spec     = impl_->trainingSpec();
    if (!trainingAvailable() || !spec || impl_->trainingMode || !impl_->editingAllowed() || !viewport
        || !viewport->hasImage())
        return false;
    // "加入训练" copies what the panel is showing, so the selector must be on the declared frame
    // port; another output would train on pixels the declaration never named.
    return impl_->presentation.selectedImageOutput() == QStringLiteral("inputs:%1").arg(spec->image.id);
}

void AppController::selectTrainingDraftRecord(const QString &id)
{
    if (!impl_->trainingMode || !impl_->idle())
        return;
    const auto &records = impl_->training->records();
    for (std::size_t index = 0; index < records.size(); ++index)
        if (records[index].id == id)
        {
            if (!impl_->training->select(static_cast<int>(index)))
                return;
            impl_->showTrainingDraftRecord();
            emit trainingChanged();
            return;
        }
}

void AppController::removeTrainingDraftRecord(const QString &id)
{
    if (!impl_->trainingMode || !impl_->idle())
        return;
    const auto &records = impl_->training->records();
    for (std::size_t index = 0; index < records.size(); ++index)
        if (records[index].id == id)
        {
            if (!impl_->training->remove(static_cast<int>(index)))
                return;
            impl_->showTrainingDraftRecord();
            emit trainingChanged();
            return;
        }
}

bool AppController::setTrainingField(const QString &id, int field, const QVariant &value)
{
    if (!impl_->idle() || !impl_->ensureTrainingSession())
        return false;
    const PortDefinition *declared = nullptr;
    for (const auto &entry : impl_->training->recordParameters())
        if (entry.id == static_cast<PortId>(field))
            declared = &entry;
    if (!declared)
        return false;
    const auto converted = trainingValue(declared->type, value);
    if (!converted)
        return false;
    const auto &records = impl_->training->records();
    for (std::size_t index = 0; index < records.size(); ++index)
        if (records[index].id == id)
        {
            if (impl_->training->setField(static_cast<int>(index), declared->id, *converted))
            {
                impl_->trainingEdited();
                return true;
            }
            return false;
        }
    return false;
}

bool AppController::setTrainingParameter(int id, const QVariant &value)
{
    if (!impl_->idle() || !impl_->ensureTrainingSession())
        return false;
    const PortDefinition *declared = nullptr;
    for (const auto &entry : impl_->training->toolParameters())
        if (entry.id == static_cast<PortId>(id))
            declared = &entry;
    if (!declared)
        return false;
    const auto converted = trainingValue(declared->type, value);
    if (!converted || !impl_->training->setParameter(declared->id, *converted))
        return false;
    impl_->trainingEdited();
    return true;
}

bool AppController::setTrainingParameterPath(int parameter, const QUrl &url)
{
    return url.isLocalFile() && setTrainingParameter(parameter, url.toLocalFile());
}

void AppController::applyTraining()
{
    if (!impl_->trainingMode || !impl_->idle())
        return;
    if (!impl_->ensureTrainingSession())
    {
        emit trainingChanged();
        return;
    }
    const auto *tool = impl_->effectiveTool();
    const auto *spec = impl_->trainingSpec();
    if (!tool || !spec)
    {
        emit trainingChanged();
        return;
    }
    const bool applied = impl_->requestTraining(*tool, *spec);
    if (!applied)
    {
        // A draft that changed the search bands no longer has a measurement to show.
        impl_->clearTrainingFit();
        emit trainingChanged();
        return;
    }

    emit trainingChanged();
}

} // namespace visionrt
