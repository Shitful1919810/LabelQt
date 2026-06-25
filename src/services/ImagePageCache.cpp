#include "services/ImagePageCache.h"

#include <QFileInfo>
#include <QImageReader>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>

#include <algorithm>
#include <utility>

namespace labelqt::services {

bool ImagePageLoadResult::isValid() const noexcept
{
    return !image.isNull();
}

ImagePageCache::ImagePageCache(QObject* parent) : QObject(parent) {}

void ImagePageCache::setMaxCachedPages(int maxCachedPages)
{
    m_maxCachedPages = std::max(1, maxCachedPages);
    trimCache();
}

std::optional<ImagePageLoadResult> ImagePageCache::cachedImage(const QString& path, QSize requestedPreviewSize)
{
    auto it = m_cache.find(path);
    if (it == m_cache.end() || !isCacheEntryFresh(it.value())) {
        if (it != m_cache.end()) {
            m_cache.erase(it);
            m_lruPaths.removeAll(path);
        }
        return std::nullopt;
    }

    touchLru(path);
    ImagePageLoadResult result = it.value().result;
    result.requestedPreviewSize = requestedPreviewSize;
    return result;
}

quint64 ImagePageCache::requestImage(const QString& path, QSize requestedPreviewSize)
{
    const quint64 requestId = m_nextRequestId++;
    if (auto cached = cachedImage(path, requestedPreviewSize)) {
        emit imageLoaded(requestId, *cached);
        return requestId;
    }

    const bool wasAlreadyPending = m_pendingLoads.contains(path);
    PendingLoad& pendingLoad = m_pendingLoads[path];
    pendingLoad.requestIds.append(requestId);
    pendingLoad.requestedPreviewSize = requestedPreviewSize;
    if (!wasAlreadyPending) {
        startLoadIfNeeded(path, requestedPreviewSize);
    }
    return requestId;
}

void ImagePageCache::preloadImages(const QStringList& paths, QSize requestedPreviewSize)
{
    for (const QString& path : paths) {
        if (path.isEmpty() || m_pendingLoads.contains(path) || cachedImage(path, requestedPreviewSize).has_value()) {
            continue;
        }
        m_pendingLoads.insert(path, PendingLoad{{}, requestedPreviewSize});
        startLoadIfNeeded(path, requestedPreviewSize);
    }
}

void ImagePageCache::clear()
{
    m_cache.clear();
    m_lruPaths.clear();
}

ImagePageLoadResult ImagePageCache::loadImage(const QString& path, QSize requestedPreviewSize)
{
    ImagePageLoadResult result;
    result.path = path;
    result.requestedPreviewSize = requestedPreviewSize;

    QImageReader reader(path);
    reader.setAutoTransform(true);
    result.originalSize = reader.size();
    result.image = reader.read();
    if (result.originalSize.isEmpty() && !result.image.isNull()) {
        result.originalSize = result.image.size();
    }
    if (result.image.isNull()) {
        result.errorMessage = reader.errorString();
    }
    return result;
}

QDateTime ImagePageCache::lastModified(const QString& path)
{
    return QFileInfo(path).lastModified();
}

qsizetype ImagePageCache::fileSize(const QString& path)
{
    return static_cast<qsizetype>(QFileInfo(path).size());
}

bool ImagePageCache::isCacheEntryFresh(const CacheEntry& entry) const
{
    return entry.lastModified == lastModified(entry.result.path) && entry.fileSize == fileSize(entry.result.path);
}

void ImagePageCache::startLoadIfNeeded(const QString& path, QSize requestedPreviewSize)
{
    const QPointer<ImagePageCache> self(this);
    QThreadPool::globalInstance()->start(QRunnable::create([self, path, requestedPreviewSize]() {
        ImagePageLoadResult result = ImagePageCache::loadImage(path, requestedPreviewSize);
        if (self == nullptr) {
            return;
        }
        QMetaObject::invokeMethod(
            self.data(),
            [self, result = std::move(result)]() mutable {
                if (self != nullptr) {
                    self->finishLoad(std::move(result));
                }
            },
            Qt::QueuedConnection);
    }));
}

void ImagePageCache::finishLoad(ImagePageLoadResult result)
{
    const QString path = result.path;
    const PendingLoad pendingLoad = m_pendingLoads.take(path);
    if (result.isValid()) {
        storeInCache(result);
    }

    for (quint64 requestId : pendingLoad.requestIds) {
        emit imageLoaded(requestId, result);
    }
}

void ImagePageCache::storeInCache(ImagePageLoadResult result)
{
    const QString path = result.path;
    m_cache.insert(path, CacheEntry{std::move(result), lastModified(path), fileSize(path)});
    touchLru(path);
    trimCache();
}

void ImagePageCache::touchLru(const QString& path)
{
    m_lruPaths.removeAll(path);
    m_lruPaths.append(path);
}

void ImagePageCache::trimCache()
{
    while (m_lruPaths.size() > m_maxCachedPages) {
        const QString oldestPath = m_lruPaths.takeFirst();
        m_cache.remove(oldestPath);
    }
}

} // namespace labelqt::services
