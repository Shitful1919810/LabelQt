#pragma once

#include "core/Project.h"
#include "services/io/ImagePageCache.h"

#include <QPointF>
#include <QPointer>
#include <QWidget>

class ImageCanvas;
class ImagePageViewController;
namespace labelqt::core {
class AppPreferences;
}

// One reusable preview lane: one image, one in-flight decode, latest selection wins.
class MergeCandidatePreview final : public QWidget {
    Q_OBJECT

public:
    MergeCandidatePreview(const labelqt::core::AppPreferences& preferences, const QStringList& groups,
                          QWidget* parent = nullptr, labelqt::services::ImagePageCache::Loader loader = {});
    ~MergeCandidatePreview() override;

    void setCandidate(const labelqt::core::ImageEntry& image);
    void clear();
    void restoreView(int zoomPercent, QPointF normalizedCenter);
    void stop();

signals:
    void viewportStateChanged(int zoomPercent, QPointF normalizedCenter);

private:
    void displayPendingCandidate();

    labelqt::core::Project m_project;
    QPointer<ImageCanvas> m_canvas;
    ImagePageViewController* m_viewController{nullptr};
    labelqt::services::ImagePageCache* m_cache{nullptr};
    bool m_loading{false};
    bool m_pending{false};
    bool m_stopped{false};
    int m_zoomPercent{0};
    QPointF m_normalizedCenter{0.5, 0.5};
};
