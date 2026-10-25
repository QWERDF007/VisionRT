#pragma once
#include <QAbstractItemModel>
#include <QObject>
#include <QString>
#include <QUrl>
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
    Q_PROPERTY(bool runtimeBusy READ runtimeBusy NOTIFY runtimeChanged)
    Q_PROPERTY(QString runState READ runState NOTIFY runtimeChanged)
    Q_PROPERTY(QString diagnostic READ diagnostic NOTIFY diagnosticChanged)
    Q_PROPERTY(QStringList diagnostics READ diagnostics NOTIFY diagnosticChanged)
    Q_PROPERTY(QVariantList flows READ flows NOTIFY projectChanged)
    Q_PROPERTY(QVariantList nodes READ nodes NOTIFY nodesChanged)
    Q_PROPERTY(QVariantList wires READ wires NOTIFY nodesChanged)
    Q_PROPERTY(QString currentFlow READ currentFlow WRITE selectFlow NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedNode READ selectedNode NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList parameters READ parameters NOTIFY parametersChanged)
    Q_PROPERTY(QVariantList parameterTabs READ parameterTabs CONSTANT)
    Q_PROPERTY(QVariantList imageOutputs READ imageOutputs NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap roiEditor READ roiEditor NOTIFY resultsChanged)
    Q_PROPERTY(QString activeRoiParameter READ activeRoiParameter NOTIFY resultsChanged)
    Q_PROPERTY(int activeRoiShape READ activeRoiShape NOTIFY resultsChanged)
    Q_PROPERTY(int imageIndex READ imageIndex WRITE setImageIndex NOTIFY resultsChanged)
    Q_PROPERTY(int imageCount READ imageCount NOTIFY resultsChanged)
    Q_PROPERTY(QString selectedImageOutput READ selectedImageOutput WRITE setImageOutput NOTIFY resultsChanged)
    Q_PROPERTY(QAbstractItemModel *toolModel READ toolModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *outputModel READ outputModel CONSTANT)
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchChanged)
    Q_PROPERTY(QString timingText READ timingText NOTIFY timingChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY projectChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY pasteChanged)
    Q_PROPERTY(bool closing READ isClosing NOTIFY closingChanged)
    Q_PROPERTY(QString aboutText READ aboutText CONSTANT)
    Q_PROPERTY(QString nodeModel READ nodeModel NOTIFY parametersChanged)
    Q_PROPERTY(QString executionGuardText READ executionGuardText NOTIFY parametersChanged)
    Q_PROPERTY(QString selectTypeText READ selectTypeText NOTIFY parametersChanged)
    Q_PROPERTY(bool assistActive READ assistActive NOTIFY assistChanged)
    Q_PROPERTY(QString assistHint READ assistHint NOTIFY assistChanged)
    Q_PROPERTY(QVariantList assistPreview READ assistPreview NOTIFY assistChanged)
public:
    AppController(std::size_t workers, const QString &applicationDirectory, QObject *parent = nullptr);
    ~AppController() override;
    QString             title() const;
    bool                busy() const;
    // State-only view used by editors: drains caused by display conversion must not disable drawing.
    bool                runtimeBusy() const;
    QString             runState() const;
    QString             diagnostic() const;
    QStringList         diagnostics() const;
    Q_INVOKABLE QString diagnosticsJson() const;
    Q_INVOKABLE void    copyDiagnostics() const;
    QVariantList        flows() const;
    QVariantList        nodes() const;
    QVariantList        wires() const;
    QString             currentFlow() const;
    QString             selectedNode() const;
    QVariantList        parameters() const;
    QVariantList        parameterTabs() const;
    QVariantList        imageOutputs() const;
    QVariantMap         roiEditor() const;
    QString             activeRoiParameter() const;
    int                 activeRoiShape() const;
    int                 imageIndex() const;
    int                 imageCount() const;
    QString             selectedImageOutput() const;
    QAbstractItemModel *toolModel() const;
    QAbstractItemModel *outputModel() const;
    QVariantList        searchResults() const;
    QString             timingText() const;
    bool                dirty() const;
    bool                canPaste() const;
    QString             aboutText() const;
    PythonRuntime      *pythonRuntime() const;

    Q_INVOKABLE void         attachViewport(QObject *viewport);
    Q_INVOKABLE void         selectFlow(const QString &id);
    Q_INVOKABLE void         selectNode(const QString &id);
    Q_INVOKABLE QVariantList routeWires(const QVariantList &geometry);
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
    Q_INVOKABLE bool         setParameter(const QString &id, bool initialization, const QString &text, bool jsonInput = false);
    Q_INVOKABLE bool         setRectParameter(const QString &id, bool initialization, double x, double y, double width, double height, double angle = 0.0);
    Q_INVOKABLE bool         setParameterPath(const QString &id, bool initialization, const QUrl &url);
    Q_INVOKABLE bool         stepIntegerParameter(const QString &id, bool initialization, int direction,
                                                  const QString &literal);
    Q_INVOKABLE bool         setParameterMode(const QString &id, bool upstream);
    Q_INVOKABLE QVariantMap  portDeclarationContext(const QString &port, bool output = false) const;
    Q_INVOKABLE QVariantMap  portResultContext(const QString &port) const;
    Q_INVOKABLE QVariantMap  portContext(const QString &port, bool output = false) const;
    Q_INVOKABLE bool         portContextCurrent(const QVariantMap &context) const;
    Q_INVOKABLE QVariantList bindingCandidates(const QVariantMap &context) const;
    Q_INVOKABLE bool         bindInput(const QVariantMap &context, const QString &node, const QString &output);
    Q_INVOKABLE bool         unbindInput(const QVariantMap &context);
    Q_INVOKABLE QVariantList cascadeOutputCandidates(const QVariantMap &context) const;
    Q_INVOKABLE QVariantList relationTargets(const QVariantMap &context) const;
    Q_INVOKABLE bool         bindOutputTo(const QVariantMap &context, const QString &node, const QString &input);
    Q_INVOKABLE bool         unbindOutputFrom(const QVariantMap &context, const QString &node, const QString &input);
    Q_INVOKABLE void         runOnce();
    Q_INVOKABLE void         toggleLoop();
    Q_INVOKABLE void         runNode();
    Q_INVOKABLE void         resetRuntime();
    Q_INVOKABLE void         refreshPython();
    Q_INVOKABLE void         rescanTools();
    Q_INVOKABLE void         requestNew();
    Q_INVOKABLE void         requestOpen(const QString &path);
    Q_INVOKABLE void         requestClose();
    Q_INVOKABLE bool         isCloseReady() const;
    Q_INVOKABLE bool         isClosing() const;
    Q_INVOKABLE void         resolveUnsaved(const QString &decision, const QString &savePath = {});
    Q_INVOKABLE bool         save(const QString &path = {});
    Q_INVOKABLE void         setImageIndex(int index);
    Q_INVOKABLE void         setImageOutput(const QString &output);
    Q_INVOKABLE bool         editRoi(const QString &parameter, bool copyUpstream);
    Q_INVOKABLE bool         startRoiDraw(const QString &parameter, int shape = 2);
    Q_INVOKABLE bool         clearRoi(const QString &parameter);
    Q_INVOKABLE bool         applyRoi();
    Q_INVOKABLE bool         setRoiRectangle(const QString &session, const QVariantList &coordinates);
    Q_INVOKABLE void         setRoiFieldsValid(const QString &session, bool valid);
    Q_INVOKABLE void         endRoiPreview();
    Q_INVOKABLE void         requestOutputText(const QVariantMap &context, bool copy, bool diagnostic = false);
    Q_INVOKABLE QVariantMap  expressionConfig(const QString &node) const;
    Q_INVOKABLE bool         applyExpressionConfig(const QString &node, const QVariantList &variables,
                                                   const QVariantList &program);
    Q_INVOKABLE QVariantList guardCandidates(const QString &node) const;
    Q_INVOKABLE bool         applyExecutionGuard(const QString &node, const QString &sourceNode,
                                                 const QString &port);
    Q_INVOKABLE QVariantMap  selectType(const QString &node) const;
    Q_INVOKABLE bool         applySelectType(const QString &node, const QString &type, const QString &dtype,
                                             int rank);
    // Chinese description of a type row, reusing the parameter panel's display mapping.
    Q_INVOKABLE QString      typeDescription(const QString &type, const QString &dtype, int rank) const;
    QString                  nodeModel() const;
    QString                  executionGuardText() const;
    QString                  selectTypeText() const;
    bool                     assistActive() const;
    QString                  assistHint() const;
    QVariantList             assistPreview() const;
    Q_INVOKABLE QVariantList toolDefinitions() const;
    Q_INVOKABLE QVariantMap  toolMetadata(const QString &toolId) const;
    // Smart point-selection. Hover previews a candidate, click learns and commits a proposal,
    // Ctrl+click appends it to the current group. Esc cancels through assistCancel().
    Q_INVOKABLE bool         assistHover(double x, double y);
    Q_INVOKABLE bool         assistClick(double x, double y, bool append);
    Q_INVOKABLE void         assistCancel();
signals:
    void projectChanged();
    void nodesChanged();
    void selectionChanged();
    void parametersChanged();
    void resultsChanged();
    void roiApplyRequested();
    void runtimeChanged();
    void timingChanged();
    void diagnosticChanged();
    void searchChanged();
    void unsavedDecisionRequired();
    void savePathRequired();
    void closeReady();
    void closingChanged();
    void projectReplaced();
    void nodeLocated(const QString &node);
    void outputTextReady(const QString &text);
    void catalogChanged();
    void pasteChanged();
    void assistChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace visionrt
