#include "ui/project/ProjectRecoveryDialog.h"

#include "core/LabelPlusDocument.h"
#include "services/project/ProjectImageValidator.h"
#include "services/proofread/ProjectComparisonService.h"
#include "ui/proofread/ProofreadChangesDialog.h"
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

using namespace labelqt::services;

ProjectRecoveryDialog::ProjectRecoveryDialog(QString projectPath, labelqt::core::AppPreferences preferences,
                                             ReplaceBackup replaceBackup, std::function<bool()> prepareRecovery,
                                             QWidget* parent)
    : QDialog(parent), m_projectPath(std::move(projectPath)), m_preferences(std::move(preferences)),
      m_replaceBackup(std::move(replaceBackup)), m_prepareRecovery(std::move(prepareRecovery))
{
    setWindowTitle(tr("Backup and Recovery"));
    resize(940, 620);
    auto* layout = new QVBoxLayout(this);
    auto* choose = new QPushButton(tr("Choose project..."), this);
    layout->addWidget(choose, 0, Qt::AlignLeft);
    auto* splitter = new QSplitter(this);
    m_list = new QListWidget(splitter);
    m_details = new QTextBrowser(splitter);
    m_details->setOpenExternalLinks(false);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);
    auto* actions = new QHBoxLayout;
    m_preview = new QPushButton(tr("Preview"), this);
    m_copy = new QPushButton(tr("Recover as copy..."), this);
    m_replace = new QPushButton(tr("Replace current project..."), this);
    m_rollback = new QPushButton(tr("Restore original file names..."), this);
    m_complete = new QPushButton(tr("Complete file operation..."), this);
    for (auto* button : {m_preview, m_copy, m_replace, m_rollback, m_complete})
        actions->addWidget(button);
    layout->addLayout(actions);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* folder = buttons->addButton(tr("Open folder"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(choose, &QPushButton::clicked, this, [this] {
        const QString path =
            QFileDialog::getOpenFileName(this, tr("Choose project"), m_projectPath, tr("LabelPlus text (*.txt)"));
        if (!path.isEmpty()) {
            m_projectPath = path;
            reload();
        }
    });
    connect(m_list, &QListWidget::currentRowChanged, this, &ProjectRecoveryDialog::showDetails);
    connect(m_preview, &QPushButton::clicked, this, &ProjectRecoveryDialog::preview);
    connect(m_copy, &QPushButton::clicked, this, &ProjectRecoveryDialog::restoreCopy);
    connect(m_replace, &QPushButton::clicked, this, [this] {
        const auto* entry = selected();
        if (!entry || !m_replaceBackup)
            return;
        if (QMessageBox::warning(
                this, tr("Replace current project"),
                tr("Replace the current project with this backup? The current project will be backed up first and undo history will be cleared."),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
            return;
        const auto result = m_replaceBackup(*entry);
        if (!result)
            QMessageBox::warning(this, windowTitle(), result.error());
        reload();
    });
    connect(m_rollback, &QPushButton::clicked, this, [this] { recoverFiles(false); });
    connect(m_complete, &QPushButton::clicked, this, [this] { recoverFiles(true); });
    connect(folder, &QPushButton::clicked, this, [this] {
        const auto* entry = selected();
        if (entry)
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(entry->recordPath).absolutePath()));
    });
    reload();
}

ProjectRecoveryDialog::~ProjectRecoveryDialog()
{
    for (QObject* child : findChildren<QObject*>())
        disconnect(child, nullptr, this, nullptr);
}

const ProjectRecoveryEntry* ProjectRecoveryDialog::selected() const
{
    const int row = m_list->currentRow();
    return row >= 0 && row < m_entries.size() ? &m_entries.at(row) : nullptr;
}

void ProjectRecoveryDialog::reload()
{
    m_list->clear();
    m_entries = ProjectRecoveryService::entries(m_projectPath, m_preferences);
    for (const auto& entry : m_entries)
        m_list->addItem(
            entry.created.toLocalTime().toString("yyyy-MM-dd HH:mm:ss") + QLatin1Char('\n') +
            (entry.kind == ProjectRecoveryEntry::Kind::Backup ? tr("Backup") : tr("Unfinished file operation")));
    m_list->setCurrentRow(m_entries.isEmpty() ? -1 : 0);
    showDetails();
}

void ProjectRecoveryDialog::showDetails()
{
    const auto* entry = selected();
    const bool backup = entry && entry->kind == ProjectRecoveryEntry::Kind::Backup;
    m_preview->setEnabled(backup);
    m_copy->setEnabled(backup);
    m_replace->setEnabled(backup && bool(m_replaceBackup));
    m_rollback->setVisible(entry && !backup);
    m_complete->setVisible(entry && !backup);
    if (!entry) {
        m_details->setPlainText(tr("No recovery records."));
        return;
    }
    QString details = tr("Project: %1").arg(entry->projectPath) + QLatin1Char('\n');
    if (backup) {
        const auto snapshot = ProjectRecoveryService::preview(*entry);
        if (snapshot) {
            details += tr("Pages: %1\nMissing images: %2")
                           .arg(snapshot->images().size())
                           .arg(ProjectImageValidator::missingImages(*snapshot).size());
            details += QLatin1Char('\n') + entry->snapshotPath;
        }
        else
            details += snapshot.error();
        m_preview->setEnabled(bool(snapshot));
        m_copy->setEnabled(bool(snapshot));
        m_replace->setEnabled(bool(snapshot) && bool(m_replaceBackup));
    }
    else {
        QFile file(entry->recordPath);
        if (file.open(QIODevice::ReadOnly)) {
            const auto object = QJsonDocument::fromJson(file.readAll()).object();
            details += tr("File operations: %1").arg(object.value("moves").toArray().size()) + QLatin1Char('\n');
            for (const auto& value : object.value("moves").toArray()) {
                const auto move = value.toObject();
                details += move.value("source").toString() + QStringLiteral(" -> ") + move.value("target").toString() +
                           QLatin1Char('\n');
            }
            const bool supported = object.value("version").toInt() == 2;
            m_rollback->setEnabled(supported);
            m_complete->setEnabled(supported);
            if (!supported)
                details += tr("This older record is available for manual inspection only.");
        }
    }
    m_details->setPlainText(details);
}

void ProjectRecoveryDialog::preview()
{
    const auto* entry = selected();
    if (!entry)
        return;
    const auto snapshot = ProjectRecoveryService::preview(*entry);
    if (!snapshot) {
        QMessageBox::warning(this, windowTitle(), snapshot.error());
        return;
    }
    labelqt::core::Project original;
    try {
        original = labelqt::core::LabelPlusDocument::loadFromFile(entry->projectPath);
    }
    catch (const std::exception&) {
    }
    const auto plan = ProjectComparisonService::planComparison(original, *snapshot);
    ProofreadChangesDialog dialog(original, *snapshot, m_preferences, plan.baselineMetadata,
                                  ProjectComparisonService::changesBetweenProjects(*snapshot, plan),
                                  {tr("Saved project"), tr("Backup"), false}, this);
    dialog.exec();
}

void ProjectRecoveryDialog::restoreCopy()
{
    const auto* entry = selected();
    if (!entry)
        return;
    const auto snapshot = *entry;
    const auto target = QFileDialog::getSaveFileName(
        this, tr("Recover as copy"), QFileInfo(entry->projectPath).absolutePath() + QStringLiteral("/recovered.txt"),
        tr("LabelPlus text (*.txt)"));
    if (target.isEmpty())
        return;
    const auto result = ProjectRecoveryService::restoreCopy(snapshot, target);
    if (!result)
        QMessageBox::warning(this, windowTitle(), result.error());
    else
        QMessageBox::information(this, windowTitle(), tr("Recovered copy: %1").arg(target));
}

void ProjectRecoveryDialog::recoverFiles(bool complete)
{
    const auto* entry = selected();
    if (!entry)
        return;
    const auto record = *entry;
    if (QMessageBox::warning(
            this, windowTitle(),
            tr("Apply file recovery for %1? Files will only be moved if their contents can be verified.")
                .arg(record.projectPath),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    if (m_prepareRecovery && !m_prepareRecovery())
        return;
    const auto result = ProjectRecoveryService::recoverFiles(record.recordPath, complete);
    if (!result)
        QMessageBox::warning(this, windowTitle(), result.error());
    else
        emit projectRecovered(record.projectPath);
    reload();
}
