#pragma once

#include "core/Project.h"
#include "services/project/ProjectImageRenameService.h"

#include <QWidget>
#include <QVector>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

class ImageRenamePreviewWidget final : public QWidget {
    Q_OBJECT

public:
    explicit ImageRenamePreviewWidget(QWidget* parent = nullptr);
    ~ImageRenamePreviewWidget() override;

    void setProject(labelqt::core::Project project);
    void setTargetImageIndexes(QVector<int> imageIndexes);
    void setPendingPlan(labelqt::services::ProjectImageRenamePlan pending);
    labelqt::services::ProjectImageRenamePlan renamePlan() const;
    void setRenameQueued(bool queued);

signals:
    void renameQueued(labelqt::services::ProjectImageRenamePlan plan);
    void previewChanged(labelqt::services::ProjectImageRenamePlan plan);

private:
    void buildUi();
    void updatePreview();
    void updateStatus();
    labelqt::services::ProjectImageRenameOptions options() const;

    labelqt::core::Project m_project;
    QVector<int> m_targetImageIndexes;
    labelqt::services::ProjectImageRenamePlan m_plan;
    labelqt::services::ProjectImageRenamePlan m_pending;
    labelqt::services::ProjectImageRenamePlan m_incoming;
    bool m_isRenameQueued{false};
    QPlainTextEdit* m_patternEdit{nullptr};
    QSpinBox* m_startIndexSpinBox{nullptr};
    QLabel* m_statusLabel{nullptr};
    QPushButton* m_actionButton{nullptr};
};
