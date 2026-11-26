#include "ui/ThumbnailImageProvider.h"

#include <utility>

namespace visionrt {

ThumbnailImageProvider::ThumbnailImageProvider(std::shared_ptr<ThumbnailStore> store, QString namespacePrefix)
    : QQuickImageProvider(QQuickImageProvider::Image, QQmlImageProviderBase::ForceAsynchronousImageLoading)
    , store_(std::move(store))
    , prefix_(std::move(namespacePrefix))
{
}

QImage ThumbnailImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const auto queryPos = id.indexOf(QLatin1Char('?'));
    const auto cleanId  = queryPos >= 0 ? id.left(queryPos) : id;
    QImage     image    = store_ ? store_->get(prefix_ + cleanId) : QImage{};
    if (image.isNull())
    {
        QImage blank(1, 1, QImage::Format_ARGB32_Premultiplied);
        blank.fill(Qt::transparent);
        if (size)
            *size = blank.size();
        return blank;
    }
    if (size)
        *size = image.size();
    if (requestedSize.isValid() && requestedSize.width() > 0 && requestedSize.height() > 0
        && (image.width() > requestedSize.width() || image.height() > requestedSize.height()))
    {
        return image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

} // namespace visionrt
