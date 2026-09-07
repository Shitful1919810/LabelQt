#include "services/io/ImagePageCache.h"

#include <QFutureWatcher>
#include <QImageReader>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <utility>

namespace labelqt::services {

bool ImagePageLoadResult::isValid() const noexcept
{
    return !image.isNull();
}

ImagePageCache::ImagePageCache(QObject* parent)
    : ImagePageCache([](const QString& path, QSize size) { return loadImage(path, size, {}); }, parent)
{
}

ImagePageCache::ImagePageCache(Loader loader, QObject* parent) : QObject(parent), m_loader(std::move(loader)) {}

ImagePageCache::Loader ImagePageCache::previewLoader(qint64 maximumSourcePixels)
{
    return [maximumSourcePixels](const QString& path, QSize size) {
        return loadImage(path, size, LoadOptions{maximumSourcePixels});
    };
}

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
    ImagePageLoadResult result = it.value();
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
    ++m_generation;
    m_pendingLoads.clear();
    m_cache.clear();
    m_lruPaths.clear();
}

ImagePageLoadResult ImagePageCache::loadImage(const QString& path, QSize requestedPreviewSize, LoadOptions options)
{
    ImagePageLoadResult result;
    result.path = path;
    result.requestedPreviewSize = requestedPreviewSize;

    const auto before = FileRevision::read(path);
    if (!before || !before->exists)
        return result;
    result.sourceRevision = *before;

    QImageReader reader(path);
    reader.setAutoTransform(true);
    result.originalSize = reader.size();
    if (options.maximumSourcePixels) {
        // Some codecs scale only after decoding. Bound that transient allocation too,
        // without changing Qt's process-wide allocation limit. Unknown sizes fail closed.
        if (result.originalSize.isEmpty()) {
            result.errorMessage = reader.errorString();
            return result;
        }
        if (requestedPreviewSize.isEmpty() || *options.maximumSourcePixels <= 0 ||
            qint64(result.originalSize.width()) * result.originalSize.height() > *options.maximumSourcePixels)
            return result;
        if (result.originalSize.width() > requestedPreviewSize.width() ||
            result.originalSize.height() > requestedPreviewSize.height())
            reader.setScaledSize(result.originalSize.scaled(requestedPreviewSize, Qt::KeepAspectRatio));
    }
    result.image = reader.read();
    if (result.originalSize.isEmpty() && !result.image.isNull()) {
        result.originalSize = result.image.size();
    }
    if (result.image.isNull()) {
        result.errorMessage = reader.errorString();
    }
    else if (options.maximumSourcePixels) {
        if (result.image.width() > requestedPreviewSize.width() ||
            result.image.height() > requestedPreviewSize.height())
            result.image = result.image.scaled(requestedPreviewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        result.image = result.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    const auto after = FileRevision::read(path);
    if (!after || !after->sameContent(*before)) {
        result.image = {};
        return result;
    }
    return result;
}

bool ImagePageCache::isCacheEntryFresh(const ImagePageLoadResult& entry) const
{
    const auto revision = FileRevision::read(entry.path);
    return revision && revision->sameContent(entry.sourceRevision);
}

void ImagePageCache::startLoadIfNeeded(const QString& path, QSize requestedPreviewSize)
{
    auto* watcher = new QFutureWatcher<ImagePageLoadResult>(this);
    connect(watcher, &QFutureWatcher<ImagePageLoadResult>::finished, this, [this, watcher, generation = m_generation] {
        auto result = watcher->result();
        watcher->deleteLater();
        if (generation == m_generation)
            finishLoad(std::move(result));
    });
    // The worker owns values only. Destroying the cache disconnects its GUI-side watcher.
    watcher->setFuture(QtConcurrent::run([loader = m_loader, path, requestedPreviewSize] {
        try {
            return loader(path, requestedPreviewSize);
        }
        catch (...) {
            ImagePageLoadResult failed;
            failed.path = path;
            failed.requestedPreviewSize = requestedPreviewSize;
            return failed;
        }
    }));
}

void ImagePageCache::finishLoad(ImagePageLoadResult result)
{
    const QString path = result.path;
    auto pending = m_pendingLoads.find(path);
    if (pending == m_pendingLoads.end())
        return;
    const auto revision = FileRevision::read(path);
    if (!revision || !revision->sameContent(result.sourceRevision)) {
        if (pending->retries++ == 0) {
            startLoadIfNeeded(path, pending->requestedPreviewSize);
            return;
        }
        result.image = {};
    }
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
    m_cache.insert(path, std::move(result));
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
