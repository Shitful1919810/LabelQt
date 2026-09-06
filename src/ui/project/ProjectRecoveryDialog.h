#pragma once

#include "services/project/ProjectRecoveryService.h"
#include <QDialog>
#include <functional>

class QListWidget;
class QTextBrowser;
class QPushButton;

class ProjectRecoveryDialog final : public QDialog {
    Q_OBJECT
public:
    using ReplaceBackup = std::function<std::expected<void, QString>(const labelqt::services::ProjectRecoveryEntry&)>;
    ProjectRecoveryDialog(QString projectPath, labelqt::core::AppPreferences preferences, ReplaceBackup replaceBackup,
                          std::function<bool()> prepareRecovery, QWidget* parent = nullptr);
    ~ProjectRecoveryDialog() override;
signals:
    void projectRecovered(const QString& path);

private:
    void reload();
    void showDetails();
    void preview();
    void restoreCopy();
    void recoverFiles(bool complete);
    const labelqt::services::ProjectRecoveryEntry* selected() const;
    QString m_projectPath;
    labelqt::core::AppPreferences m_preferences;
    ReplaceBackup m_replaceBackup;
    std::function<bool()> m_prepareRecovery;
    QVector<labelqt::services::ProjectRecoveryEntry> m_entries;
    QListWidget* m_list;
    QTextBrowser* m_details;
    QPushButton* m_preview;
    QPushButton* m_copy;
    QPushButton* m_replace;
    QPushButton* m_rollback;
    QPushButton* m_complete;
};
