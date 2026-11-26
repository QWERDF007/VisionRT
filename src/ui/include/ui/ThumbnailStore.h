#pragma once

#include "ui/Export.h"

#include <QImage>
#include <QString>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace visionrt {

class VISIONRT_UI_API ThumbnailStore final
{
public:
    static constexpr int kMaxEdge = 240;

    static QImage scaleForThumbnail(const QImage &image)
    {
        if (image.isNull())
            return {};
        if (image.width() <= kMaxEdge && image.height() <= kMaxEdge)
            return image;
        return image.scaled(kMaxEdge, kMaxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    quint64 set(const QString &key, const QImage &image)
    {
        const auto scaled = scaleForThumbnail(image);
        const auto rev    = version_.fetch_add(1, std::memory_order_relaxed) + 1;
        std::lock_guard lock(mutex_);
        if (scaled.isNull())
            images_.erase(key);
        else
            images_.insert_or_assign(key, scaled);
        return rev;
    }

    quint64 replaceNamespace(const QString &prefix, std::vector<std::pair<QString, QImage>> entries)
    {
        const auto rev = version_.fetch_add(1, std::memory_order_relaxed) + 1;
        std::lock_guard lock(mutex_);
        std::erase_if(images_, [&](const auto &item) { return item.first.startsWith(prefix); });
        for (auto &[key, img] : entries)
        {
            if (!img.isNull())
                images_.insert_or_assign(prefix + key, scaleForThumbnail(img));
        }
        return rev;
    }

    void clearNamespace(const QString &prefix)
    {
        std::lock_guard lock(mutex_);
        std::erase_if(images_, [&](const auto &item) { return item.first.startsWith(prefix); });
    }

    QImage get(const QString &key) const
    {
        std::lock_guard lock(mutex_);
        const auto found = images_.find(key);
        return found == images_.end() ? QImage{} : found->second;
    }

    quint64 bumpVersion() noexcept
    {
        return version_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

private:
    mutable std::mutex                   mutex_;
    std::unordered_map<QString, QImage>  images_;
    std::atomic<quint64>                 version_{0};
};

} // namespace visionrt
