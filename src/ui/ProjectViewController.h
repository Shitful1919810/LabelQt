#pragma once

#include "services/PageSourceInfoService.h"

#include <QHash>
#include <QObject>

class QLabel;
class QPushButton;
class PageSelectorComboBox;

namespace labelqt::core {
class Project;
}

class ProjectViewController final : public QObject {
    Q_OBJECT

public:
    explicit ProjectViewController(QObject* parent = nullptr);

    void setWidgets(PageSelectorComboBox* pageComboBox, QPushButton* previousButton, QPushButton* nextButton,
                    QLabel* pageSourceLabel);
    void refreshProject(const labelqt::core::Project& project);
    void refreshCurrentPage(const labelqt::core::Project& project, int imageIndex);
    void clear();

private:
    void updateCurrentPageSourceLabel(const labelqt::core::Project& project, int imageIndex);

    PageSelectorComboBox* m_pageComboBox{nullptr};
    QPushButton* m_previousButton{nullptr};
    QPushButton* m_nextButton{nullptr};
    QLabel* m_pageSourceLabel{nullptr};
    QHash<QString, labelqt::services::PageSourceInfo> m_pageSourcesByImageName;
};
