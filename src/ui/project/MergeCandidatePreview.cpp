#include "ui/project/MergeCandidatePreview.h"

#include "ui/canvas/ImageCanvas.h"
#include "ui/canvas/ImagePageViewController.h"

#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr QSize previewSize{1600, 1600};
constexpr qint64 maximumSourcePixels = 32 * 1024 * 1024;

} // namespace

MergeCandidatePreview::MergeCandidatePreview(const labelqt::core::AppPreferences& preferences,
                                             const QStringList& groups, QWidget* parent,
                                             labelqt::services::ImagePageCache::Loader loader)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_canvas = new ImageCanvas(this);
    m_canvas->setReadOnly(true);
    m_canvas->setPreferences(preferences);
    m_canvas->setGroups(groups);
    m_canvas->setVisibleGroups(groups);
    m_canvas->setMinimumSize(120, 160);
    layout->addWidget(m_canvas);

    m_cache = new labelqt::services::ImagePageCache(
        loader ? std::move(loader) : labelqt::services::ImagePageCache::previewLoader(maximumSourcePixels), this);
    m_cache->setMaxCachedPages(1);
    m_viewController = new ImagePageViewController(this);
    m_viewController->setCanvas(m_canvas);
    m_viewController->setCache(m_cache);
    // A single-page project prevents adjacent-candidate preloading.
    m_viewController->setProjectProvider([this]() -> const labelqt::core::Project& { return m_project; });
    m_viewController->setCurrentImageIndexProvider([this]() { return m_project.isEmpty() ? -1 : 0; });
    m_viewController->setPreviewTargetSizeProvider([]() { return previewSize; });
    connect(m_cache, &labelqt::services::ImagePageCache::imageLoaded, this,
            [this](quint64, const labelqt::services::ImagePageLoadResult& result) {
                // Stay busy until cache/controller delivery unwinds. Never enqueue more
                // decodes while an obsolete request is still running.
                QTimer::singleShot(0, this, [this, imageSize = result.image.size()]() {
                    m_loading = false;
                    if (m_stopped)
                        return;
                    if (m_pending)
                        displayPendingCandidate();
                    else if (m_project.isEmpty())
                        m_cache->clear();
                    else {
                        if (m_zoomPercent <= 0 && !imageSize.isEmpty()) {
                            const QSize available = m_canvas->viewport()->size() - QSize(4, 4);
                            m_zoomPercent =
                                std::clamp(qRound(100.0 * std::min(double(available.width()) / imageSize.width(),
                                                                   double(available.height()) / imageSize.height())),
                                           10, 400);
                        }
                        restoreView(m_zoomPercent, m_normalizedCenter);
                    }
                });
            });
    connect(m_canvas, &ImageCanvas::viewportStateChanged, this, [this](int zoom, QPointF center) {
        if (m_loading || m_pending || m_stopped || m_project.isEmpty())
            return;
        m_zoomPercent = zoom;
        m_normalizedCenter = center;
        emit viewportStateChanged(zoom, center);
    });
}

MergeCandidatePreview::~MergeCandidatePreview()
{
    stop();
}

void MergeCandidatePreview::setCandidate(const labelqt::core::ImageEntry& image)
{
    if (m_stopped)
        return;
    m_pending = true;
    m_viewController->clear();
    m_project.images() = {image};
    if (!m_loading)
        displayPendingCandidate();
}

void MergeCandidatePreview::clear()
{
    m_pending = false;
    m_project.clear();
    const QSignalBlocker blocker(m_canvas);
    m_viewController->clear();
    if (!m_loading)
        m_cache->clear();
}

void MergeCandidatePreview::displayPendingCandidate()
{
    if (m_stopped || m_loading || !m_pending)
        return;
    m_pending = false;
    m_loading = true;
    m_cache->clear();
    m_viewController->displayCurrentImage();
    if (m_zoomPercent > 0)
        m_viewController->restoreViewAfterCurrentImageDisplayed(m_zoomPercent, m_normalizedCenter);
}

void MergeCandidatePreview::restoreView(int zoomPercent, QPointF normalizedCenter)
{
    m_zoomPercent = zoomPercent;
    m_normalizedCenter = normalizedCenter;
    if (!m_stopped && !m_pending && !m_project.isEmpty() && zoomPercent > 0) {
        const QSignalBlocker blocker(m_canvas);
        m_viewController->restoreViewAfterCurrentImageDisplayed(zoomPercent, normalizedCenter);
    }
}

void MergeCandidatePreview::stop()
{
    if (m_stopped)
        return;
    m_stopped = true;
    disconnect(m_cache, nullptr, this, nullptr);
    if (m_canvas)
        disconnect(m_canvas, nullptr, this, nullptr);
    m_viewController->setCache(nullptr);
    m_viewController->clear();
    m_viewController->setCanvas(nullptr);
    m_viewController->setProjectProvider({});
    m_viewController->setCurrentImageIndexProvider({});
    m_canvas.clear();
    m_project.clear();
    m_cache->clear();
}
