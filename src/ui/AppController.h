#pragma once
#include <QAbstractItemModel>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

namespace visionrt {
class ImageViewport;
class PythonRuntime;

class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY projectChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY runtimeChanged)
    Q_PROPERTY(QString runState READ runState NOTIFY runtimeChanged)
    Q_PROPERTY(QString diagnostic READ diagnostic NOTIFY diagnosticChanged)
    Q_PROPERTY(QVariantList flows READ flows NOTIFY projectChanged)
    Q_PROPERTY(QVariantList nodes READ nodes NOTIFY nodesChanged)
    Q_PROPERTY(QVariantList wires READ wires NOTIFY nodesChanged)
    Q_PROPERTY(QString currentFlow READ currentFlow WRITE selectFlow NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedNode READ selectedNode NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList parameters READ parameters NOTIFY parametersChanged)
    Q_PROPERTY(QVariantList imageOutputs READ imageOutputs NOTIFY resultsChanged)
    Q_PROPERTY(int imageIndex READ imageIndex WRITE setImageIndex NOTIFY resultsChanged)
    Q_PROPERTY(int imageCount READ imageCount NOTIFY resultsChanged)
    Q_PROPERTY(QString selectedImageOutput READ selectedImageOutput WRITE setImageOutput NOTIFY resultsChanged)
    Q_PROPERTY(QAbstractItemModel *toolModel READ toolModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *outputModel READ outputModel CONSTANT)
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchChanged)
    Q_PROPERTY(QString timingText READ timingText NOTIFY runtimeChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY projectChanged)
    Q_PROPERTY(QString aboutText READ aboutText CONSTANT)
public:
    AppController(std::size_t workers, const QString &applicationDirectory, QObject *parent = nullptr);
    ~AppController() override;
    QString             title() const;
    bool                busy() const;
    QString             runState() const;
    QString             diagnostic() const;
    QStringList         diagnostics() const;
    QVariantList        flows() const;
    QVariantList        nodes() const;
    QVariantList        wires() const;
    QString             currentFlow() const;
    QString             selectedNode() const;
    QVariantList        parameters() const;
    QVariantList        imageOutputs() const;
    int                 imageIndex() const;
    int                 imageCount() const;
    QString             selectedImageOutput() const;
    QAbstractItemModel *toolModel() const;
    QAbstractItemModel *outputModel() const;
    QVariantList        searchResults() const;
    QString             timingText() const;
    bool                dirty() const;
    QString             aboutText() const;
    PythonRuntime      *pythonRuntime() const;

    Q_INVOKABLE void         attachViewport(QObject *viewport);
    Q_INVOKABLE void         selectFlow(const QString &id);
    Q_INVOKABLE void         selectNode(const QString &id);
    Q_INVOKABLE void         filterTools(const QString &text);
    Q_INVOKABLE void         searchNodes(const QString &text);
    Q_INVOKABLE void         locateNode(const QString &flow, const QString &node);
    Q_INVOKABLE void         addNode(const QString &toolId, int x, int y);
    Q_INVOKABLE bool         moveNodes(const QVariantList &ids, int dx, int dy);
    Q_INVOKABLE void         deleteNodes(const QVariantList &ids);
    Q_INVOKABLE void         copyNodes(const QVariantList &ids);
    Q_INVOKABLE void         pasteNodes(int x, int y);
    Q_INVOKABLE void         renameNode(const QString &id, const QString &alias);
    Q_INVOKABLE void         addFlow(const QString &name);
    Q_INVOKABLE void         renameFlow(const QString &id, const QString &name);
    Q_INVOKABLE void         deleteFlow(const QString &id);
    Q_INVOKABLE void         copyFlow(const QString &id);
    Q_INVOKABLE void         setFlowSelected(const QString &id, bool selected);
    Q_INVOKABLE bool         setParameter(const QString &id, bool initialization, const QString &yamlText);
    Q_INVOKABLE bool         setParameterMode(const QString &id, bool upstream);
    Q_INVOKABLE QVariantList bindingCandidates(const QString &input) const;
    Q_INVOKABLE bool         bindInput(const QString &input, const QString &node, const QString &output);
    Q_INVOKABLE void         unbindInput(const QString &input);
    Q_INVOKABLE QVariantList relationTargets(const QString &output) const;
    Q_INVOKABLE bool         bindOutputTo(const QString &output, const QString &node, const QString &input);
    Q_INVOKABLE void         runOnce();
    Q_INVOKABLE void         toggleLoop();
    Q_INVOKABLE void         runNode();
    Q_INVOKABLE void         resetRuntime();
    Q_INVOKABLE void         refreshPython();
    Q_INVOKABLE void         rescanTools();
    Q_INVOKABLE void         requestNew();
    Q_INVOKABLE void         requestOpen(const QString &path);
    Q_INVOKABLE void         requestClose();
    Q_INVOKABLE void         resolveUnsaved(const QString &decision, const QString &savePath = {});
    Q_INVOKABLE bool         save(const QString &path = {});
    Q_INVOKABLE void         setImageIndex(int index);
    Q_INVOKABLE void         setImageOutput(const QString &output);
    Q_INVOKABLE bool         editRoi(const QString &parameter, bool copyUpstream);
    Q_INVOKABLE void         endRoiPreview();
    Q_INVOKABLE void         requestOutputText(int row, bool copy);
    Q_INVOKABLE QVariantList toolDefinitions() const;
    Q_INVOKABLE QVariantMap  toolMetadata(const QString &toolId) const;
    Q_INVOKABLE QVariantList cppImplementations() const;
    Q_INVOKABLE QVariantMap  previewToolImpact(const QVariantMap &metadata) const;
    Q_INVOKABLE bool         saveToolMetadata(const QVariantMap &metadata);
signals:
    void projectChanged();
    void nodesChanged();
    void selectionChanged();
    void parametersChanged();
    void resultsChanged();
    void runtimeChanged();
    void diagnosticChanged();
    void searchChanged();
    void unsavedDecisionRequired();
    void savePathRequired();
    void closeReady();
    void projectReplaced();
    void nodeLocated(const QString &node);
    void outputTextReady(const QString &text);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace visionrt
