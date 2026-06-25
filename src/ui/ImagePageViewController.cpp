#include "ui/ImagePageViewController.h"

#include "core/Project.h"
#include "ui/ImageCanvas.h"

#include <QImage>

ImagePageViewController::ImagePageViewController(QObject* parent) : QObject(parent) {}

void ImagePageViewController::setCanvas(ImageCanvas* canvas)
{
    m_canvas = canvas;
}

void ImagePageViewController::setCache(labelqt::services::ImagePageCache* cache)
{
    if (m_cache == cache) {
        return;
    }
    if (m_cache != nullptr) {
        disconnect(m_cache, nullptr, this, nullptr);
    }
    m_cache = cache;
    if (m_cache != nullptr) {
        connect(m_cache, &labelqt::services::ImagePageCache::imageLoaded, this,
                &ImagePageViewController::handleImageLoaded);
    }
}

void ImagePageViewController::setProjectProvider(std::function<const labelqt::core::Project&()> projectProvider)
{
    m_projectProvider = std::move(projectProvider);
}

void ImagePageViewController::setCurrentImageIndexProvider(std::function<int()> currentImageIndexProvider)
{
    m_currentImageIndexProvider = std::move(currentImageIndexProvider);
}

void ImagePageViewController::setPreviewTargetSizeProvider(std::function<QSize()> previewTargetSizeProvider)
{
    m_previewTargetSizeProvider = std::move(previewTargetSizeProvider);
}

void ImagePageViewController::clear()
{
    m_pendingImageRequestId = 0;
    m_pendingImagePath.clear();
    m_pendingViewRestore.reset();
    if (m_canvas != nullptr) {
        m_canvas->setImage(QString(), QImage(), {});
    }
}

void ImagePageViewController::displayCurrentImage()
{
    if (m_canvas == nullptr || m_cache == nullptr || !m_projectProvider || !m_currentImageIndexProvider) {
        return;
    }

    const labelqt::core::Project& project = m_projectProvider();
    const int imageIndex = m_currentImageIndexProvider();
    if (imageIndex < 0 || imageIndex >= project.images().size()) {
        clear();
        return;
    }

    const labelqt::core::ImageEntry& image = project.images().at(imageIndex);
    const QSize targetSize = previewTargetSize();
    if (auto cachedImage = m_cache->cachedImage(image.path, targetSize)) {
        m_pendingImageRequestId = 0;
        m_pendingImagePath.clear();
        m_canvas->setImage(cachedImage->path, cachedImage->image, image.labels);
        applyPendingViewRestore();
        preloadAdjacentImages();
        return;
    }

    m_canvas->setImageLoading(image.path, image.labels);
    m_pendingImagePath = image.path;
    m_pendingImageRequestId = m_cache->requestImage(image.path, targetSize);
}

void ImagePageViewController::refreshCurrentLabels()
{
    if (m_canvas == nullptr || !m_projectProvider || !m_currentImageIndexProvider) {
        return;
    }

    const labelqt::core::Project& project = m_projectProvider();
    const int imageIndex = m_currentImageIndexProvider();
    if (imageIndex < 0 || imageIndex >= project.images().size()) {
        return;
    }

    m_canvas->setLabels(project.images().at(imageIndex).labels);
}

void ImagePageViewController::preloadAdjacentImages()
{
    if (m_cache == nullptr || !m_projectProvider || !m_currentImageIndexProvider) {
        return;
    }

    const labelqt::core::Project& project = m_projectProvider();
    const int imageIndex = m_currentImageIndexProvider();
    if (project.isEmpty() || imageIndex < 0) {
        return;
    }

    QStringList paths;
    const QVector<labelqt::core::ImageEntry>& images = project.images();
    if (imageIndex > 0) {
        paths.append(images.at(imageIndex - 1).path);
    }
    if (imageIndex + 1 < images.size()) {
        paths.append(images.at(imageIndex + 1).path);
    }
    m_cache->preloadImages(paths, previewTargetSize());
}

void ImagePageViewController::handleImageLoaded(quint64 requestId, const labelqt::services::ImagePageLoadResult& result)
{
    if (m_canvas == nullptr || !m_projectProvider || !m_currentImageIndexProvider) {
        return;
    }
    if (requestId == 0 || requestId != m_pendingImageRequestId || result.path != m_pendingImagePath) {
        return;
    }

    const labelqt::core::Project& project = m_projectProvider();
    const int imageIndex = m_currentImageIndexProvider();
    if (imageIndex < 0 || imageIndex >= project.images().size()) {
        return;
    }

    const labelqt::core::ImageEntry& image = project.images().at(imageIndex);
    if (image.path != result.path) {
        return;
    }

    m_pendingImageRequestId = 0;
    m_pendingImagePath.clear();
    m_canvas->setImage(result.path, result.image, image.labels);
    applyPendingViewRestore();
    preloadAdjacentImages();
}

void ImagePageViewController::restoreViewAfterCurrentImageDisplayed(int zoomPercent, QPointF normalizedCenter)
{
    m_pendingViewRestore = PendingViewRestore{zoomPercent, normalizedCenter};
    if (m_pendingImageRequestId == 0) {
        applyPendingViewRestore();
    }
}

void ImagePageViewController::applyPendingViewRestore()
{
    if (m_canvas == nullptr || !m_pendingViewRestore.has_value()) {
        return;
    }

    const PendingViewRestore restore = *m_pendingViewRestore;
    m_pendingViewRestore.reset();
    m_canvas->restoreView(restore.zoomPercent, restore.normalizedCenter);
    emit viewRestored(m_canvas->zoomPercent());
}

QSize ImagePageViewController::previewTargetSize() const
{
    if (m_previewTargetSizeProvider) {
        return m_previewTargetSizeProvider();
    }
    return {};
}
