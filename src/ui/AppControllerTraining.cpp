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

std::optional<TrainingFieldValue> trainingValue(const TypeSpec &type, const QVariant &value)
{
    if (type.type == Type::String)
        return value.toString().toStdString();
    try
    {
        return ValueJson::parse(value.toString().toUtf8(), type);
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
    static const TrainingSpec parametersOnly;
    return tool && tool->training ? &*tool->training : tool && !tool->trainParams.empty() ? &parametersOnly : nullptr;
}

bool AppController::Impl::ensureTrainingSession()
{
    const auto *spec = trainingSpec();
    const auto *node = currentNode();
    const auto *tool = effectiveTool();
    if (!spec || !node || !tool)
        return false;
    auto      &view = trainingViews[node->id];
    const bool load = !view.session.active() || view.session.spec() != *spec;
    if (training != &view.session || load)
        ++trainingSerial;
    training = &view.session;
    if (load)
    {
        std::string hydrateError;
        auto        hydrated = hydrateNodeTrainingSets(*node, *tool, applicationDirectory, &hydrateError);
        if (!hydrateError.empty())
            LogService::write(LogLevel::Warn, LogSource::Ui,
                              QString::fromUtf8(hydrateError.c_str(), static_cast<qsizetype>(hydrateError.size())));
        training->begin({node->id, *spec, tool->trainParams, std::move(hydrated)});
    }
    else
        for (const auto &artifact : spec->artifacts)
            training->synchronizeParameter(artifact.target.id, node->trainParams.at(artifact.target.id));
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
        auto                                           inputs = training->parameters();
        std::map<std::string, PendingTrainingArtifact> plannedArtifacts;
        for (const auto &artifact : spec.artifacts)
        {
            const auto found = inputs.find(artifact.target.id);
            if (found == inputs.end())
                throw ContractError("训练参数缺少模型路径端口：" + portLabel(artifact.target.id));
            const auto configuredText = std::get<std::string>(found->second);
            const auto configured     = pathFromUtf8(configuredText);
            const auto absolutePath   = configured.empty()
                                          ? training::newTrainingArchivePath(applicationDirectory,
                                                                             pathFromUtf8(artifact.id + ".zip"))
                                          : (configured.is_absolute()
                                                 ? configured.lexically_normal()
                                                 : (applicationDirectory / configured).lexically_normal());
            const auto storedText     = configured.empty()
                                          ? workbench::pathText(absolutePath.lexically_relative(applicationDirectory))
                                                .toStdString()
                                          : configuredText;
            found->second = workbench::pathText(absolutePath).toStdString();
            plannedArtifacts.emplace(artifact.id,
                                     PendingTrainingArtifact{artifact.target.id, artifact.sourcePort, absolutePath,
                                                             storedText});
        }
        AssistRequest request;
        request.context                = {projectEpoch, generation, flow.toStdString(), node->id, node->executionRevision};
        request.context.trainingSerial = trainingSerial;
        request.training               = true;
        request.learn                  = true;
        request.inputs                 = std::move(inputs);
        if (!assist->request(request, std::make_shared<const ToolRegistry>(registry), tool))
            throw ContractError("已有计算尚未结束，请稍后重试");
        pendingTraining          = request.context;
        pendingTrainingArtifacts = std::move(plannedArtifacts);
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
    const auto expected  = *pendingTraining;
    const auto artifacts = std::move(pendingTrainingArtifacts);
    pendingTraining.reset();
    pendingTrainingArtifacts.clear();
    editor.setBusy(state != RuntimeState::Idle);
    const auto *node       = currentNode();
    const auto *definition = effectiveTool();
    const auto  tool       = definition ? std::optional<ToolDefinition>{*definition} : std::nullopt;
    const bool  current    = outcome.context == expected && projectEpoch == expected.projectEpoch && node
                          && node->id == expected.node && node->executionRevision == expected.executionRevision
                          && trainingSerial == expected.trainingSerial && training && tool;
    if (current)
    {
        auto result = ToolResult{outcome.outputs, outcome.status.detail};
        if (outcome.status)
            publishTraining(training->spec(), artifacts, result);
        if (result.outputs.empty())
            result = toolResult({}, outcome.status);
        auto snapshot               = std::make_shared<NodeSnapshot>();
        snapshot->node              = expected.node;
        snapshot->toolId            = tool->nameEn;
        snapshot->generation        = generation;
        snapshot->executionRevision = currentNode()->executionRevision;
        snapshot->invocation        = ProducedBy::Train;
        snapshot->outputs           = std::move(result.outputs);
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

bool AppController::Impl::publishTraining(const TrainingSpec &spec,
                                          const std::map<std::string, PendingTrainingArtifact> &artifacts,
                                          ToolResult &result)
{
    const auto *node = currentNode();
    const auto  fail = [&](const QString &detail)
    {
        result = toolResult({}, makeStatus(ErrorCode::Internal, "训练模型保存失败", detail.toStdString()));
        LogService::write(LogLevel::Error, LogSource::Ui, detail);
        return false;
    };
    try
    {
        std::vector<LiteralWrite> writes;
        for (const auto &[id, value] : training->parameters())
            writes.push_back({PortStage::Train, id, value});
        for (const auto &artifact : spec.artifacts)
        {
            const auto found = artifacts.find(artifact.id);
            if (found == artifacts.end())
                throw ContractError("训练模型产物配置缺失：" + artifact.id);
            if (!std::filesystem::is_regular_file(found->second.absolutePath))
                throw ContractError("训练工具未生成模型资源包："
                                    + workbench::pathText(found->second.absolutePath).toStdString());
            std::ranges::find(writes, found->second.targetPort, &LiteralWrite::port)->value = found->second.storedPath;
            result.outputs.insert_or_assign(found->second.sourcePort, found->second.storedPath);
        }
        // Pass a non-empty prepare callback when training artifacts were written so ProjectEditor
        // advances initializationRevision even when retraining overwrote the same model path.
        const auto bumpInitialization
            = spec.artifacts.empty() ? std::function<void()>{} : std::function<void()>([] {});
        editor.setLiteralWrites(flow.toStdString(), node->id, writes, bumpInitialization);
        for (const auto &write : writes)
            training->setParameter(write.port, write.value);
        training->accept();
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
    const auto *tool = effectiveTool();
    const auto snapshot = node ? presentation.displaySnapshot(*node) : ResultSlot::Snapshot{};
    RegionGroup fit;
    std::vector<GeometryOverlay> geometry;
    if (snapshot && snapshot->invocation == ProducedBy::Train && snapshot->status && tool)
    {
        for (const auto &port : tool->outputs)
        {
            if (port.image && isShapeType(port.type.type))
                if (const auto found = snapshot->outputs.find(port.id); found != snapshot->outputs.end())
                    if (const auto *roi = shapeRegions(found->second); roi && !(*roi).empty())
                        fit.insert(fit.end(), (*roi).front().begin(), (*roi).front().end());
            if (const auto found = snapshot->outputs.find(port.id); found != snapshot->outputs.end())
                if (auto value = GeometryOverlay::fromValue(found->second, 0, false)) geometry.push_back(std::move(*value));
        }
    }
    if (auto *viewport = presentation.viewport())
    {
        viewport->setOverlays({}, std::move(fit));
        viewport->setGeometryOverlays(std::move(geometry));
    }
}

void AppController::Impl::clearTrainingFit()
{
    if (trainingMode)
        if (auto *viewport = presentation.viewport())
        {
            viewport->setOverlays({}, {});
            viewport->setGeometryOverlays({});
        }
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
    if (!trainingMode) return;
    auto     *viewport = presentation.viewport();
    const int index    = training->selected();
    if (!viewport || !training->roiParameter() || index < 0 || index >= static_cast<int>(training->records().size()))
        return;
    RoiEditSession::Start start;
    start.node       = training->node();
    start.kind       = RoiTargetKind::Training;
    start.input      = *training->roiParameter();
    start.type       = workbench::port(training->recordParameters(), *training->roiParameter())->type;
    start.background = DataRef<Image>(Image{trainingAssistImage});
    start.draft      = shapeValue(start.type.type, ROI{training->regions(index)});
    start.original   = {BindingMode::Literal, start.draft, {}};
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
        draft.setParameter(mapping.target.id, value);
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

void AppController::Impl::refreshTrainingModel()
{
    if (!training || !training->active() || !training->datasetParameter())
    {
        trainingRecordModel.clear();
        if (!trainingThumbSignature.empty() || trainingThumbNode != 0 || trainingThumbDataset.has_value())
        {
            trainingThumbSignature.clear();
            trainingThumbNode = 0;
            trainingThumbDataset.reset();
            if (thumbnailStore)
                thumbnailStore->clearNamespace(QStringLiteral("training/"));
        }
        return;
    }

    const auto nodeId    = training->node();
    const auto datasetId = *training->datasetParameter();
    const auto &images   = training->images();

    std::vector<std::pair<TrainingImageId, quint64>> signature;
    signature.reserve(images.size());
    for (const auto &img : images)
        signature.emplace_back(img.id, img.thumbnailRevision);

    if (trainingThumbNode != nodeId || trainingThumbDataset != datasetId || trainingThumbSignature != signature)
    {
        trainingThumbNode      = nodeId;
        trainingThumbDataset   = datasetId;
        trainingThumbSignature = std::move(signature);
        if (thumbnailStore)
        {
            std::vector<std::pair<QString, QImage>> entries;
            entries.reserve(images.size());
            for (const auto &img : images)
                if (!img.thumbnailImage.isNull())
                    entries.emplace_back(QStringLiteral("%1/%2/%3").arg(nodeId).arg(datasetId).arg(img.id),
                                         img.thumbnailImage);
            thumbnailStore->replaceNamespace(QStringLiteral("training/"), std::move(entries));
        }
    }

    const auto                      &records = training->records();
    std::vector<TrainingRecordModel::Row> rows;
    rows.reserve(records.size());
    for (std::size_t i = 0; i < records.size(); ++i)
    {
        const auto &rec   = records[i];
        QString     url;
        if (rec.image)
        {
            const auto found = std::ranges::find(images, *rec.image, &TrainingDraftImage::id);
            if (found != images.end() && !found->thumbnailImage.isNull())
                url = QStringLiteral("image://visionrt_training/%1/%2/%3?v=%4")
                          .arg(nodeId)
                          .arg(datasetId)
                          .arg(found->id)
                          .arg(found->thumbnailRevision);
        }
        rows.push_back({
            rec.id(),
            rec.image ? QVariant(*rec.image) : QVariant{},
            std::move(url),
            static_cast<int>(training->regions(static_cast<int>(i)).size()),
            static_cast<int>(i) == training->selected(),
            training->fields(static_cast<int>(i))
        });
    }
    trainingRecordModel.sync(std::move(rows));
}

QAbstractListModel *AppController::trainingRecordModel() const
{
    return &impl_->trainingRecordModel;
}

QVariantList AppController::trainingRecords() const
{
    impl_->refreshTrainingModel();
    return impl_->trainingRecordModel.snapshot();
}

QString AppController::selectedTrainingDraftRecord() const
{
    if (!impl_->training)
        return {};
    const int   index   = impl_->training->selected();
    const auto &records = impl_->training->records();
    if (index < 0 || index >= static_cast<int>(records.size()))
        return {};
    return records[index].id();
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
    if (!canAddTrainingImage() || !impl_->ensureTrainingSession()) return false;
    const auto *spec = impl_->training->datasetSpec();
    const auto ref = *spec->image;
    const auto *node = impl_->currentNode();
    const auto value = impl_->presentation.resolvedValue(*node, ref);
    const auto *batch = std::get_if<DataRef<Image>>(&value);
    const int index = impl_->presentation.imageIndex();
    if (!batch || index < 0 || static_cast<std::size_t>(index) >= batch->get().size()) return false;
    const auto *tool = impl_->effectiveTool();
    const auto &ports = ref.stage == PortStage::Inputs ? tool->inputs : ref.stage == PortStage::Outputs ? tool->outputs
                      : ref.stage == PortStage::Init ? tool->initParams : tool->trainParams;
    const auto *port = workbench::port(ports, ref.id);
    ColorOrder order = port->colorOrder;
    auto source = QStringLiteral("%1:%2:%3:%4:%5").arg(node->id).arg(node->executionRevision)
                      .arg(static_cast<int>(ref.stage)).arg(ref.id).arg(index);
    if (ref.stage == PortStage::Inputs)
    {
        const auto &binding = node->inputs.at(ref.id);
        if (effectiveSource(binding))
        {
            const auto snapshot = impl_->presentation.sharedSnapshot(binding.source->node);
            if (!snapshot) return false;
            source = QStringLiteral("%1:%2:%3:%4:%5:%6:%7").arg(snapshot->generation).arg(snapshot->session)
                         .arg(snapshot->round).arg(snapshot->publication).arg(binding.source->node).arg(binding.source->port).arg(index);
            const auto *producerNode = impl_->findNode(binding.source->node);
            const auto contract = nodeContract(*producerNode, *impl_->registry.find(producerNode->toolId));
            order = workbench::port(contract.outputs, binding.source->port)->colorOrder;
        }
    }
    const auto added = impl_->training->addRecord(batch->get()[index], order, source);
    if (!added)
    {
        LogService::write(LogLevel::Error, LogSource::Ui, impl_->training->error());
        return false;
    }
    impl_->presentation.setSuspended(true);
    impl_->trainingMode = true;
    impl_->trainingViews.at(impl_->training->node()).visible = true;
    impl_->showTrainingDraftRecord();
    emit trainingChanged();
    emit resultsChanged();
    return true;
}

bool AppController::canAddTrainingImage() const
{
    const auto *viewport = impl_->presentation.viewport();
    const auto *spec = impl_->training ? impl_->training->datasetSpec() : nullptr;
    if (!spec || !spec->image || impl_->trainingMode || !impl_->editingAllowed() || !viewport || !viewport->hasImage())
        return false;
    return impl_->presentation.selectedImageOutput()
        == QStringLiteral("%1:%2").arg(QString::fromLatin1(stageName(spec->image->stage))).arg(spec->image->id);
}

void AppController::selectTrainingDraftRecord(const QString &id)
{
    if (!impl_->trainingMode || !impl_->idle())
        return;
    const auto &records = impl_->training->records();
    for (std::size_t index = 0; index < records.size(); ++index)
        if (records[index].id() == id)
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
        if (records[index].id() == id)
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
    return impl_->applyTrainingValue(declared->id, *converted, id);
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
    return converted && impl_->applyTrainingValue(declared->id, *converted);
}

bool AppController::Impl::applyTrainingValue(PortId id, PortValue value, const QString &record)
{
    bool changed = false;
    if (record.isEmpty()) changed = training->setParameter(id, std::move(value));
    else
    {
        const auto &records = training->records();
        const auto found = std::find_if(records.begin(), records.end(), [&](const auto &entry) { return entry.id() == record; });
        if (found != records.end()) changed = training->setField(static_cast<int>(found - records.begin()), id, std::move(value));
    }
    if (changed) trainingEdited();
    return changed;
}

bool AppController::selectTrainingDataset(int parameter)
{
    if (!impl_->idle() || !impl_->ensureTrainingSession() || !impl_->training->selectDataset(static_cast<PortId>(parameter))) return false;
    impl_->finishRoi();
    ++impl_->trainingSerial;
    impl_->clearTrainingFit();
    if (impl_->trainingMode) impl_->showTrainingDraftRecord();
    emit trainingChanged();
    emit parametersChanged();
    emit resultsChanged();
    return true;
}

bool AppController::setTrainingParameterPath(int parameter, const QUrl &url)
{
    return url.isLocalFile() && setTrainingParameter(parameter, url.toLocalFile());
}

bool AppController::addTrainingRecord(int parameter)
{
    if (!selectTrainingDataset(parameter) || !impl_->training->addRecord()) return false;
    setTrainingMode(true);
    impl_->showTrainingDraftRecord();
    impl_->trainingEdited();
    emit resultsChanged();
    return true;
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
