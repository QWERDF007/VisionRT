#pragma once

#include "core/ToolDefinition.h"

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <optional>

namespace visionrt {

class ToolPortListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles
    {
        IdRole = Qt::UserRole + 1,
        NameZhRole,
        NameEnRole,
        TypeRole,
        DisplayTypeRole,
        DefaultValueRole,
        IsFixedRole,
        MinRole,
        MaxRole,
        StepRole,
        DecimalsRole,
        ImageRefRole,
        AlignmentRefRole,
        PathModeRefRole,
        SameGeometryRefRole,
        OptionsTextRole,
        TabRole,
        GroupRole,
        EditorRole,
        // The author's explicit choice to offer this Image port in the workbench image selector.
        ShowImageRole
    };
    Q_ENUM(Roles)

    explicit ToolPortListModel(QObject *parent = nullptr);

    // Binds the active draft and stage. A read-only draft (an installed tool the user does not own)
    // shows its ports but rejects every mutation, so the list can never become a second editor for
    // a definition that is not ours.
    void setDraft(ToolDefinition *draft, PortStage stage, bool editable);

    PortStage stage() const noexcept
    {
        return stage_;
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Checks if the row belongs to the fixed prefix
    Q_INVOKABLE bool isFixedRow(int row) const noexcept;

    // Number of fixed rows for the active stage
    Q_INVOKABLE int fixedRowCount() const noexcept;

    // Adds a new business port, assigning nextPortId and bumping high-water counter
    Q_INVOKABLE bool addPort(const QString &nameZh, const QString &typeToken = QStringLiteral("int"), const QString &defaultValue = {});

    // Removes a business port; rejects fixed rows. Does not recycle port IDs.
    Q_INVOKABLE bool removePort(int row);

    // Moves a business row; rejects moving into or before fixed prefix
    Q_INVOKABLE bool movePort(int fromRow, int toRow);

    // Updates properties of a business row; rejects fixed rows. A type change goes through
    // changeType() because it has to reconcile the whole draft.
    Q_INVOKABLE bool updatePort(int row, const QVariantMap &properties);

    // What changing this row to `typeToken` would clear, without touching the draft. Empty means the
    // change is lossless and can be applied directly.
    Q_INVOKABLE QStringList typeChangeImpact(int row, const QString &typeToken) const;

    // Applies the type change and reconciles the draft (values, relations, other ports' references).
    Q_INVOKABLE bool changeType(int row, const QString &typeToken);

    // Ports a relation of this row may point at, as {ref, label} maps. The list is filtered by the
    // relation's own rule, so the editor can never create a reference the declaration rejects.
    Q_INVOKABLE QVariantList relationCandidates(int row, const QString &kind) const;

    // Editor kinds offered for this row's type, as {token, label} maps with a leading default entry.
    Q_INVOKABLE QVariantList editorOptions(int row) const;

    // Finds row by PortId (-1 if not found)
    Q_INVOKABLE int rowForPortId(quint32 portId) const;

    // Returns all properties for row as QVariantMap
    Q_INVOKABLE QVariantMap portAt(int row) const;

signals:
    void draftModified();
    // A rejected edit leaves the draft untouched; the message says which value the declaration
    // cannot hold, so the editor can show it instead of pretending the edit succeeded.
    void rejected(const QString &message);
    // An accepted edit that had to drop authored fields: the editor reports the exact loss rather
    // than letting the draft change silently.
    void cleared(const QString &message);

private:
    std::vector<PortDefinition> *portVector() noexcept;
    const std::vector<PortDefinition> *portVector() const noexcept;
    bool                            acceptsEdits() const noexcept;
    const PortDefinition           *findPort(PortStage stage, PortId id) const;
    void                            requireRelationTarget(const PortDefinition &port, const std::string &kind,
                                                          const PortRef &reference) const;

    ToolDefinition *draft_ = nullptr;
    PortStage       stage_ = PortStage::Inputs;
    bool            editable_ = false;
};

} // namespace visionrt
