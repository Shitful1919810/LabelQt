#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QVector>

#include "services/io/FileRevision.h"
#include <functional>
#include <optional>

namespace labelqt::services {

struct ImagePageLoadResult {
    QString path;
    QSize requestedPreviewSize;
    QSize originalSize;
    QImage image;
    QString errorMessage;
    FileRevision sourceRevision;

    bool isValid() const noexcept;
};

class ImagePageCache final : public QObject {
    Q_OBJECT

public:
    explicit ImagePageCache(QObject* parent = nullptr);
    using Loader = std::function<ImagePageLoadResult(const QString&, QSize)>;
    explicit ImagePageCache(Loader loader, QObject* parent = nullptr);

    void setMaxCachedPages(int maxCachedPages);
    std::optional<ImagePageLoadResult> cachedImage(const QString& path, QSize requestedPreviewSize);
    quint64 requestImage(const QString& path, QSize requestedPreviewSize);
    void preloadImages(const QStringList& paths, QSize requestedPreviewSize);
    void clear();

signals:
    void imageLoaded(quint64 requestId, const labelqt::services::ImagePageLoadResult& result);

private:
    struct PendingLoad {
        QVector<quint64> requestIds;
        QSize requestedPreviewSize;
        int retries{0};
    };

    static ImagePageLoadResult loadImage(const QString& path, QSize requestedPreviewSize);

    bool isCacheEntryFresh(const ImagePageLoadResult& entry) const;
    void startLoadIfNeeded(const QString& path, QSize requestedPreviewSize);
    void finishLoad(ImagePageLoadResult result);
    void storeInCache(ImagePageLoadResult result);
    void touchLru(const QString& path);
    void trimCache();

    QHash<QString, ImagePageLoadResult> m_cache;
    QHash<QString, PendingLoad> m_pendingLoads;
    QVector<QString> m_lruPaths;
    quint64 m_nextRequestId{1};
    int m_maxCachedPages{5};
    quint64 m_generation{0};
    Loader m_loader;
};

} // namespace labelqt::services
