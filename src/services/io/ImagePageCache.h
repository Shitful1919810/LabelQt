#pragma once

#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QVector>

#include <optional>

namespace labelqt::services {

struct ImagePageLoadResult {
    QString path;
    QSize requestedPreviewSize;
    QSize originalSize;
    QImage image;
    QString errorMessage;

    bool isValid() const noexcept;
};

class ImagePageCache final : public QObject {
    Q_OBJECT

public:
    explicit ImagePageCache(QObject* parent = nullptr);

    void setMaxCachedPages(int maxCachedPages);
    std::optional<ImagePageLoadResult> cachedImage(const QString& path, QSize requestedPreviewSize);
    quint64 requestImage(const QString& path, QSize requestedPreviewSize);
    void preloadImages(const QStringList& paths, QSize requestedPreviewSize);
    void clear();

signals:
    void imageLoaded(quint64 requestId, const labelqt::services::ImagePageLoadResult& result);

private:
    struct CacheEntry {
        ImagePageLoadResult result;
        QDateTime lastModified;
        qsizetype fileSize{0};
    };

    struct PendingLoad {
        QVector<quint64> requestIds;
        QSize requestedPreviewSize;
    };

    static ImagePageLoadResult loadImage(const QString& path, QSize requestedPreviewSize);
    static QDateTime lastModified(const QString& path);
    static qsizetype fileSize(const QString& path);

    bool isCacheEntryFresh(const CacheEntry& entry) const;
    void startLoadIfNeeded(const QString& path, QSize requestedPreviewSize);
    void finishLoad(ImagePageLoadResult result);
    void storeInCache(ImagePageLoadResult result);
    void touchLru(const QString& path);
    void trimCache();

    QHash<QString, CacheEntry> m_cache;
    QHash<QString, PendingLoad> m_pendingLoads;
    QVector<QString> m_lruPaths;
    quint64 m_nextRequestId{1};
    int m_maxCachedPages{5};
};

} // namespace labelqt::services
