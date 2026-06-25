#include "ui/ProjectViewController.h"

#include "core/Project.h"
#include "ui/PageSelectorComboBox.h"

#include <QFileInfo>
#include <QLabel>
#include <QPushButton>

ProjectViewController::ProjectViewController(QObject* parent) : QObject(parent) {}

void ProjectViewController::setWidgets(PageSelectorComboBox* pageComboBox, QPushButton* previousButton,
                                       QPushButton* nextButton, QLabel* pageSourceLabel)
{
    m_pageComboBox = pageComboBox;
    m_previousButton = previousButton;
    m_nextButton = nextButton;
    m_pageSourceLabel = pageSourceLabel;
}

void ProjectViewController::refreshProject(const labelqt::core::Project& project)
{
    m_pageSourcesByImageName = labelqt::services::PageSourceInfoService::sourcesForProject(project);

    if (m_pageComboBox == nullptr) {
        return;
    }

    m_pageComboBox->clear();
    const QVector<labelqt::core::ImageEntry>& images = project.images();
    for (int i = 0; i < images.size(); ++i) {
        m_pageComboBox->addPage(images.at(i).name, i);
    }
}

void ProjectViewController::refreshCurrentPage(const labelqt::core::Project& project, int imageIndex)
{
    if (m_pageComboBox != nullptr) {
        m_pageComboBox->setCurrentIndex(imageIndex);
    }
    if (m_previousButton != nullptr) {
        m_previousButton->setEnabled(imageIndex > 0);
    }
    if (m_nextButton != nullptr) {
        m_nextButton->setEnabled(imageIndex >= 0 && imageIndex < project.images().size() - 1);
    }
    updateCurrentPageSourceLabel(project, imageIndex);
}

void ProjectViewController::clear()
{
    m_pageSourcesByImageName.clear();
    if (m_pageComboBox != nullptr) {
        m_pageComboBox->clear();
    }
    if (m_previousButton != nullptr) {
        m_previousButton->setEnabled(false);
    }
    if (m_nextButton != nullptr) {
        m_nextButton->setEnabled(false);
    }
    if (m_pageSourceLabel != nullptr) {
        m_pageSourceLabel->clear();
        m_pageSourceLabel->setToolTip({});
        m_pageSourceLabel->setVisible(false);
    }
}

void ProjectViewController::updateCurrentPageSourceLabel(const labelqt::core::Project& project, int imageIndex)
{
    if (m_pageSourceLabel == nullptr) {
        return;
    }
    if (imageIndex < 0 || imageIndex >= project.images().size()) {
        m_pageSourceLabel->clear();
        m_pageSourceLabel->setToolTip({});
        m_pageSourceLabel->setVisible(false);
        return;
    }

    const labelqt::core::ImageEntry& image = project.images().at(imageIndex);
    const auto sourceIt = m_pageSourcesByImageName.constFind(image.name);
    if (sourceIt == m_pageSourcesByImageName.cend()) {
        m_pageSourceLabel->clear();
        m_pageSourceLabel->setToolTip({});
        m_pageSourceLabel->setVisible(false);
        return;
    }

    const QString sourcePath = sourceIt->sourcePath;
    QString sourceText = QFileInfo(sourcePath).fileName();
    if (sourceText.isEmpty() && sourceIt->sourceIndex > 0) {
        sourceText = tr("source #%1").arg(sourceIt->sourceIndex);
    }
    if (sourceText.isEmpty()) {
        sourceText = tr("unknown");
    }

    m_pageSourceLabel->setText(tr("Source: %1").arg(sourceText));
    m_pageSourceLabel->setToolTip(sourcePath);
    m_pageSourceLabel->setVisible(true);
}
