#pragma once

#include "ui/Export.h"
#include "ui/ThumbnailStore.h"

#include <QQuickImageProvider>
#include <QString>
#include <memory>

namespace visionrt {

class VISIONRT_UI_API ThumbnailImageProvider final : public QQuickImageProvider
{
public:
    ThumbnailImageProvider(std::shared_ptr<ThumbnailStore> store, QString namespacePrefix);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    std::shared_ptr<ThumbnailStore> store_;
    QString                         prefix_;
};

} // namespace visionrt
