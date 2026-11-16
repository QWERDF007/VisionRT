#include "ui/AppController_p.h"

#include "core/ToolPaths.h"
#include "log/LogService.h"

#include <QImage>

#include <string>

namespace visionrt {
namespace {
// The asset path a training tool was configured with; empty when the node never had one.
std::filesystem::path configuredAsset(const NodeDefinition &node, const TrainingSpec &spec)
{
    const auto found = node.initParams.find(spec.assetParam);
    if (found == node.initParams.end())
        return {};
    const auto *text = std::get_if<std::string>(&found->second);
    if (!text || text->empty())
        return {};
    return softwareDataDir() / pathFromUtf8(*text);
}

// The default asset a node receives the first time training data is added. It follows the product
// rule for every path a tool reads: relative to the installation, never a machine path.
std::filesystem::path defaultAsset(const ProjectDefinition &project, const NodeDefinition &node,
                                   const ToolDefinition &tool)
{
    QString stem = QString::fromStdString(project.name).trimmed();
    if (stem.isEmpty())
        stem = QStringLiteral("project");
    for (auto &character : stem)
        if (QStringLiteral("<>:\"/\\|?*").contains(character) || character.unicode() < 0x20)
            character = QChar(u'_');
    const auto name = QStringLiteral("training/%1-%2-%3.json")
                          .arg(stem)
                          .arg(node.id)
                          .arg(QString::fromStdString(tool.nameEn));
    const auto text = name.toStdString();
    return pathFromUtf8(text);
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
    auto &view = trainingViews[node->id];
    const auto asset = configuredAsset(*node, *spec);
    const bool load = !view.session.active() || view.session.kind() != spec->kind || view.session.asset() != asset;
    if (training != &view.session || load)
        ++trainingSerial;
    training = &view.session;
    if (load)
    {
        training->begin({node->id, spec->kind, asset});
        if (!training->error().isEmpty())
        {
            LogService::write(LogLevel::Error, LogSource::Ui, QStringLiteral("训练数据：") + training->error());
            return false;
        }
    }
    return true;
}

bool AppController::Impl::ensureTrainingAsset()
{
    const auto *spec = trainingSpec();
    const auto *node = currentNode();
    const auto *tool = effectiveTool();
    if (!spec || !node || !tool)
        return false;
    const auto current = configuredAsset(*node, *spec);
    if (!current.empty())
    {
        training->setAsset(current);
        return true;
    }
    const auto relative = defaultAsset(editor.project(), *node, *tool);
    try
    {
        const auto path = relative.generic_u8string();
        editor.setInitParam(flow.toStdString(), node->id, spec->assetParam,
                            std::string(reinterpret_cast<const char *>(path.data()), path.size()));
    }
    catch (const std::exception &error)
    {
        LogService::write(LogLevel::Error, LogSource::Ui,
                          QStringLiteral("无法写入训练数据路径：%1").arg(QString::fromUtf8(error.what())));
        return false;
    }
    training->setAsset(softwareDataDir() / relative);
    emit q->projectChanged();
    emit q->parametersChanged();
    return true;
}

void AppController::Impl::showTrainingRecord()
{
    auto *viewport = presentation.viewport();
    if (!viewport)
        return;
    const auto &records = training->records();
    const int   index   = training->selected();
    if (index < 0 || index >= static_cast<int>(records.size()))
    {
        viewport->endRoiEdit();
        viewport->setHostImage(QImage{});
        trainingAssistImage = {};
        return;
    }
    const auto &record = records[index];
    QImage      image;
    if (!image.load(QString::fromStdWString(record.image.wstring())))
    {
        LogService::write(LogLevel::Error, LogSource::Ui,
                          QStringLiteral("无法读取训练图：%1").arg(QString::fromStdWString(record.image.wstring())));
        viewport->endRoiEdit();
        viewport->setHostImage(QImage{});
        trainingAssistImage = {};
        return;
    }
    // The assistant reads the same picture the user is pointing at, decoded once per record.
    trainingAssistImage = grayPlane(image);
    viewport->setHostImage(image);
    viewport->fitContain();
    viewport->setDrawShape(0);
    // The record's rectangles are edited through the same ROI mechanism the processing view uses;
    // only the owner in the target differs.
    seatTrainingRegions();
    refreshAssist();
}

void AppController::Impl::seatTrainingRegions()
{
    auto *viewport = presentation.viewport();
    const int index = training->selected();
    if (!viewport || index < 0 || index >= static_cast<int>(training->records().size()))
        return;
    viewport->beginRoiEdit(RoiEditTarget{training->node(), 0, index, -1, trainingSerial,
                                         RoiTargetKind::Training},
                           training->records()[index].regions, false, false);
}

void AppController::Impl::applyTrainingAssist(const AssistOutcome &outcome)
{
    if (!trainingMode || outcome.context.trainingSerial != trainingSerial || !training->active())
    {
        clearAssistPreview();
        LogService::write(LogLevel::Warn, LogSource::Ui, QStringLiteral("智能点选：训练数据已变化，请重新点选"));
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
    auto regions = training->records()[index].regions;
    if (outcome.append)
    {
        // A straight-edge model is a pair: append only up to the second band, and let the user delete
        // one instead of silently dropping the first.
        if (regions.size() + outcome.proposal.front().size() > static_cast<std::size_t>(2))
        {
            clearAssistPreview();
            LogService::write(LogLevel::Warn, LogSource::Ui,
                              QStringLiteral("智能点选：直边训练只需要两条参考边，请先删除多余的矩形"));
            return;
        }
        regions.insert(regions.end(), outcome.proposal.front().begin(), outcome.proposal.front().end());
    }
    else
        regions = outcome.proposal.front();
    training->setRegions(index, std::move(regions));
    clearAssistPreview();
    seatTrainingRegions();
    emit q->trainingChanged();
}

void AppController::Impl::commitTrainingRoi(const RoiEditTarget &target, const EncodedRegion *value)
{
    if (!trainingMode || target.kind != RoiTargetKind::Training || target.session != trainingSerial
        || target.nodeSeq != training->node())
        return;
    const int index = training->selected();
    if (index < 0 || index >= static_cast<int>(training->records().size()))
        return;
    auto regions = training->records()[index].regions;
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
                              QStringLiteral("训练数据：直边训练只需要两个矩形，请先删除或调整已有矩形"));
            return;
        }
        regions.push_back(*value);
    }
    else if (target.roiIndex < static_cast<int>(regions.size()))
        regions[target.roiIndex] = *value;
    else
        return;
    training->setRegions(index, std::move(regions));
    // A committed rectangle ends the create gesture; drawing another is one explicit click away.
    if (auto *viewport = presentation.viewport())
        viewport->setDrawShape(0);
    emit q->trainingChanged();
}

void AppController::Impl::leaveTraining()
{
    if (!trainingMode)
        return;
    if (auto *viewport = presentation.viewport())
        viewport->endRoiEdit();
    presentation.setSuspended(false);
    trainingMode = false;
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
    impl_->trainingMode = true;
    impl_->trainingViews.at(impl_->training->node()).visible = true;
    ++impl_->trainingSerial;
    impl_->showTrainingRecord();
    impl_->refreshAssist();
    emit trainingChanged();
    emit resultsChanged();
}

QString AppController::trainingKind() const
{
    const auto *spec = impl_->trainingSpec();
    if (!spec)
        return {};
    return spec->kind == TrainingKind::StraightEdges ? QStringLiteral("straight_edges")
                                                     : QStringLiteral("color_samples");
}

QVariantList AppController::trainingRecords() const
{
    return impl_->training ? impl_->training->rows() : QVariantList{};
}

QString AppController::selectedTrainingRecord() const
{
    if (!impl_->training)
        return {};
    const int index = impl_->training->selected();
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
    auto *viewport = impl_->presentation.viewport();
    const QImage image = viewport->displayedImage();
    if (!impl_->ensureTrainingSession() || !impl_->ensureTrainingAsset())
    {
        emit trainingChanged();
        return false;
    }
    const auto added = impl_->training->addImage(image);
    if (!added)
    {
        LogService::write(LogLevel::Error, LogSource::Ui, QStringLiteral("训练数据：") + impl_->training->error());
        emit trainingChanged();
        return false;
    }
    impl_->presentation.setSuspended(true);
    impl_->trainingMode = true;
    impl_->trainingViews.at(impl_->training->node()).visible = true;
    ++impl_->trainingSerial;
    impl_->showTrainingRecord();
    emit trainingChanged();
    emit resultsChanged();
    return true;
}

bool AppController::canAddTrainingImage() const
{
    const auto *viewport = impl_->presentation.viewport();
    return trainingAvailable() && !impl_->trainingMode && impl_->editingAllowed()
        && viewport && viewport->hasImage();
}

void AppController::selectTrainingRecord(const QString &id)
{
    if (!impl_->trainingMode || !impl_->idle())
        return;
    const auto &records = impl_->training->records();
    for (std::size_t index = 0; index < records.size(); ++index)
        if (records[index].id == id)
        {
            if (!impl_->training->select(static_cast<int>(index)))
                return;
            impl_->showTrainingRecord();
            emit trainingChanged();
            return;
        }
}

void AppController::removeTrainingRecord(const QString &id)
{
    if (!impl_->trainingMode || !impl_->idle())
        return;
    const auto &records = impl_->training->records();
    for (std::size_t index = 0; index < records.size(); ++index)
        if (records[index].id == id)
        {
            if (!impl_->training->remove(static_cast<int>(index)))
                return;
            impl_->showTrainingRecord();
            emit trainingChanged();
            return;
        }
}

void AppController::setTrainingLabel(const QString &id, const QString &label)
{
    if (!impl_->trainingMode || !impl_->idle())
        return;
    const auto &records = impl_->training->records();
    for (std::size_t index = 0; index < records.size(); ++index)
        if (records[index].id == id)
        {
            if (impl_->training->setLabel(static_cast<int>(index), label))
                emit trainingChanged();
            return;
        }
}

void AppController::applyTraining()
{
    if (!impl_->trainingMode || !impl_->idle())
        return;
    if (!impl_->ensureTrainingSession() || !impl_->ensureTrainingAsset())
    {
        emit trainingChanged();
        return;
    }
    // The session owns the rules of its own asset format, so its refusal is the message the log
    // receives; the window never invents a second wording for the same condition.
    if (!impl_->training->save())
    {
        LogService::write(LogLevel::Error, LogSource::Ui, QStringLiteral("训练数据：") + impl_->training->error());
        emit trainingChanged();
        return;
    }
    try
    {
        impl_->editor.reloadNode(impl_->flow.toStdString(), impl_->training->node());
    }
    catch (const std::exception &error)
    {
        LogService::write(LogLevel::Error, LogSource::Ui,
                          QStringLiteral("训练数据已写入，但节点重载失败：%1").arg(QString::fromUtf8(error.what())));
        emit trainingChanged();
        return;
    }
    LogService::write(LogLevel::Info, LogSource::Ui,
                      QStringLiteral("训练数据已应用，下次运行按新样本重建模型"));
    emit projectChanged();
    emit parametersChanged();
    emit trainingChanged();
}

} // namespace visionrt
