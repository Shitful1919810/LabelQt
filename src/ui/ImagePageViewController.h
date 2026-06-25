#pragma once

#include "services/ImagePageCache.h"

#include <QObject>
#include <QPointF>
#include <QSize>

#include <functional>
#include <optional>

class ImageCanvas;

namespace labelqt::core {
class Project;
}

class ImagePageViewController final : public QObject {
    Q_OBJECT

public:
    explicit ImagePageViewController(QObject* parent = nullptr);

    void setCanvas(ImageCanvas* canvas);
    void setCache(labelqt::services::ImagePageCache* cache);
    void setProjectProvider(std::function<const labelqt::core::Project&()> projectProvider);
    void setCurrentImageIndexProvider(std::function<int()> currentImageIndexProvider);
    void setPreviewTargetSizeProvider(std::function<QSize()> previewTargetSizeProvider);

    void clear();
    void displayCurrentImage();
    void refreshCurrentLabels();
    void preloadAdjacentImages();
    void restoreViewAfterCurrentImageDisplayed(int zoomPercent, QPointF normalizedCenter);

signals:
    void viewRestored(int zoomPercent);

private:
    struct PendingViewRestore {
        int zoomPercent{100};
        QPointF normalizedCenter{0.5, 0.5};
    };

    void handleImageLoaded(quint64 requestId, const labelqt::services::ImagePageLoadResult& result);
    void applyPendingViewRestore();
    QSize previewTargetSize() const;

    ImageCanvas* m_canvas{nullptr};
    labelqt::services::ImagePageCache* m_cache{nullptr};
    std::function<const labelqt::core::Project&()> m_projectProvider;
    std::function<int()> m_currentImageIndexProvider;
    std::function<QSize()> m_previewTargetSizeProvider;
    quint64 m_pendingImageRequestId{0};
    QString m_pendingImagePath;
    std::optional<PendingViewRestore> m_pendingViewRestore;
};
