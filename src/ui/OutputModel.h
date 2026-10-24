#pragma once
#include "runtime/Snapshot.h"

#include <QAbstractTableModel>
#include <QVariantMap>
#include <optional>

namespace visionrt {
class OutputModel final : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Role
    {
        PortIdRole = Qt::UserRole + 1,
        NameRole,
        TypeRole,
        TypeDisplayRole,
        PreviewRole,
        TooltipRole,
        CountRole,
        SystemRole,
        DiagnosticRole,
        RowIndexRole
    };

    struct Captured
    {
        PortValue                             value;
        TypeSpec                              type;
        std::uint64_t                         publication = 0;
        QString                               port;
        std::optional<NodeDiagnostic>         diagnostic;
        std::shared_ptr<const NodeDiagnostic> lifecycle;
    };

    explicit OutputModel(QObject *parent = nullptr);
    void                    clear();
    void                    setResult(const ToolDefinition *definition, ResultSlot::Snapshot snapshot,
                                      const std::map<std::string, std::size_t, std::less<>> &references,
                                      const TypeMap *nodeOutputs = nullptr, std::shared_ptr<const NodeDiagnostic> lifecycle = {});
    std::optional<Captured> capture(int row) const;
    std::optional<Captured> capture(const QString &port) const;
    static QString          fullText(const Captured &captured);
    static QString          diagnosticText(const Captured &captured);
    static std::size_t      estimatedTextBytes(const Captured &captured);
    Q_INVOKABLE QVariantMap get(int row) const;
    int                     rowCount(const QModelIndex &parent = {}) const override;
    int                     columnCount(const QModelIndex &parent = {}) const override;
    QVariant                data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    struct Row
    {
        QString     id, name, type, preview, tooltip;
        std::size_t count  = 0;
        bool        system = false;
        Captured    captured;
        bool        resolved = true;
    };

    std::vector<Row> rows_;
};

QString displayTypeName(const TypeSpec &type);
QString typeText(const TypeSpec &type);
QString valueText(const PortValue &value, const TypeSpec &type);
} // namespace visionrt
