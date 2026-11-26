#pragma once

#include "ui/Export.h"

#include <QAbstractListModel>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <vector>

namespace visionrt {

class VISIONRT_UI_API TrainingRecordModel final : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role
    {
        RecordIdRole = Qt::UserRole + 1,
        ImageIdRole,
        ImageUrlRole,
        RegionsRole,
        SelectedRole,
        FieldsRole
    };

    struct Row
    {
        QString      recordId;
        QVariant     imageId;
        QString      imageUrl;
        int          regions  = 0;
        bool         selected = false;
        QVariantList fields;
    };

    explicit TrainingRecordModel(QObject *parent = nullptr);

    void         clear();
    void         sync(std::vector<Row> nextRows);
    QVariantList snapshot() const;

    int                    rowCount(const QModelIndex &parent = {}) const override;
    QVariant               data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    void applyRowDiff(int index, Row &&next);

    std::vector<Row> rows_;
};

} // namespace visionrt
