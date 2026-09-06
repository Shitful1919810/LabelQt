#include "ui/search/ProjectSearchPanel.h"
#include "ui/search/ProjectTextSearchModel.h"
#include "ui/widgets/CheckableFilterButton.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMap>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QTableView>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using namespace labelqt::services;

ProjectSearchPanel::ProjectSearchPanel(Callbacks callbacks, QWidget* shortcutOwner, QWidget* parent)
    : QWidget(parent), m_callbacks(std::move(callbacks))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 6, 0, 0);
    auto* bar = new QHBoxLayout;
    m_query = new QPlainTextEdit(this);
    m_query->setPlaceholderText(tr("Find text"));
    m_query->setFixedHeight(m_query->fontMetrics().lineSpacing() * 2 + 16);
    m_query->setTabChangesFocus(true);
    bar->addWidget(m_query, 1);
    auto* previous = new QToolButton(this);
    auto* next = new QToolButton(this);
    auto* close = new QToolButton(this);
    previous->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    next->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    close->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    previous->setToolTip(tr("Previous match"));
    next->setToolTip(tr("Next match"));
    close->setToolTip(tr("Close search"));
    for (auto* button : {previous, next, close})
        bar->addWidget(button);
    layout->addLayout(bar);
    auto* filters = new QHBoxLayout;
    m_scope = new QComboBox(this);
    m_scope->addItems({tr("Whole project"), tr("Current page"), tr("Page range")});
    m_first = new QSpinBox(this);
    m_last = new QSpinBox(this);
    m_first->setRange(1, 1);
    m_last->setRange(1, 1);
    m_first->setToolTip(tr("First page"));
    m_last->setToolTip(tr("Last page"));
    m_first->hide();
    m_last->hide();
    m_groups = new CheckableFilterButton(this);
    m_groups->setTexts(tr("All groups"), tr("No groups"), tr("Groups: %1"));
    m_caseSensitive = new QCheckBox(tr("Match case"), this);
    filters->addWidget(m_scope);
    filters->addWidget(m_first);
    filters->addWidget(m_last);
    filters->addWidget(m_groups);
    filters->addWidget(m_caseSensitive);
    layout->addLayout(filters);
    m_count = new QLabel(this);
    layout->addWidget(m_count);
    m_model = new ProjectTextSearchModel(this);
    m_table = new QTableView(this);
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    m_table->setColumnWidth(0, 120);
    m_table->setColumnWidth(1, 45);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setMinimumHeight(100);
    m_table->setMaximumHeight(230);
    layout->addWidget(m_table);
    auto* expand = new QCheckBox(tr("Replace"), this);
    layout->addWidget(expand);
    auto* replacementPanel = new QWidget(this);
    auto* replacementLayout = new QHBoxLayout(replacementPanel);
    replacementLayout->setContentsMargins(0, 0, 0, 0);
    m_replacement = new QPlainTextEdit(replacementPanel);
    m_replacement->setPlaceholderText(tr("Replacement text"));
    m_replacement->setFixedHeight(m_query->height());
    m_replacement->setTabChangesFocus(true);
    auto* replaceCurrent = new QPushButton(tr("Replace match"), replacementPanel);
    auto* replaceAll = new QPushButton(tr("Preview replacements..."), replacementPanel);
    replacementLayout->addWidget(m_replacement, 1);
    replacementLayout->addWidget(replaceCurrent);
    replacementLayout->addWidget(replaceAll);
    layout->addWidget(replacementPanel);
    replacementPanel->hide();
    connect(expand, &QCheckBox::toggled, replacementPanel, &QWidget::setVisible);
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(180);
    connect(m_timer, &QTimer::timeout, this, &ProjectSearchPanel::search);
    connect(m_query, &QPlainTextEdit::textChanged, this, &ProjectSearchPanel::invalidate);
    connect(m_caseSensitive, &QCheckBox::toggled, this, &ProjectSearchPanel::invalidate);
    connect(m_groups, &CheckableFilterButton::selectionChanged, this, &ProjectSearchPanel::invalidate);
    connect(m_first, &QSpinBox::valueChanged, this, &ProjectSearchPanel::invalidate);
    connect(m_last, &QSpinBox::valueChanged, this, &ProjectSearchPanel::invalidate);
    connect(m_scope, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_first->setVisible(index == 2);
        m_last->setVisible(index == 2);
        invalidate();
    });
    connect(m_table, &QTableView::clicked, this, [this](const QModelIndex& index) { navigate(index.row()); });
    connect(m_table, &QTableView::activated, this, [this](const QModelIndex& index) { navigate(index.row()); });
    connect(previous, &QToolButton::clicked, this, [this] { step(-1); });
    connect(next, &QToolButton::clicked, this, [this] { step(1); });
    connect(close, &QToolButton::clicked, this, [this] {
        hide();
        m_timer->stop();
        m_callbacks.clearHighlight();
    });
    connect(replaceCurrent, &QPushButton::clicked, this, [this] { replace(false); });
    connect(replaceAll, &QPushButton::clicked, this, [this] { replace(true); });
    m_findAction = new QAction(tr("Find in project..."), this);
    m_nextAction = new QAction(tr("Next search match"), this);
    m_previousAction = new QAction(tr("Previous search match"), this);
    for (auto* action : actions())
        shortcutOwner->addAction(action);
    connect(m_findAction, &QAction::triggered, this, &ProjectSearchPanel::showSearch);
    connect(m_nextAction, &QAction::triggered, this, [this] { step(1); });
    connect(m_previousAction, &QAction::triggered, this, [this] { step(-1); });
    hide();
}

ProjectSearchPanel::~ProjectSearchPanel()
{
    for (QObject* child : findChildren<QObject*>())
        disconnect(child, nullptr, this, nullptr);
}
QList<QAction*> ProjectSearchPanel::actions() const
{
    return {m_findAction, m_nextAction, m_previousAction};
}
void ProjectSearchPanel::setPreferences(const labelqt::core::AppPreferences& preferences)
{
    const auto conflicts = preferences.conflictingSearchShortcuts();
    m_findAction->setShortcut(
        conflicts.contains(QStringLiteral("findProjectShortcut")) ? QKeySequence{} : preferences.findProjectShortcut());
    m_nextAction->setShortcut(conflicts.contains(QStringLiteral("nextSearchMatchShortcut"))
                                  ? QKeySequence{}
                                  : preferences.nextSearchMatchShortcut());
    m_previousAction->setShortcut(conflicts.contains(QStringLiteral("previousSearchMatchShortcut"))
                                      ? QKeySequence{}
                                      : preferences.previousSearchMatchShortcut());
}
void ProjectSearchPanel::showSearch()
{
    m_callbacks.commitEditor();
    show();
    search();
    m_query->setFocus();
}
void ProjectSearchPanel::invalidate()
{
    m_callbacks.clearHighlight();
    if (isVisible())
        m_timer->start();
}
void ProjectSearchPanel::currentPageChanged()
{
    if (m_scope->currentIndex() == 1 && m_searchPage != m_callbacks.currentPage())
        invalidate();
}
void ProjectSearchPanel::search()
{
    m_timer->stop();
    const auto& project = m_callbacks.project();
    if (m_knownGroups != project.groups()) {
        m_knownGroups = project.groups();
        QVector<CheckableFilterOption> options;
        for (const auto& group : m_knownGroups)
            options.append({group, group});
        m_groups->setOptions(options);
    }
    m_first->setMaximum(std::max(1, static_cast<int>(project.images().size())));
    m_last->setMaximum(m_first->maximum());
    ProjectTextQuery query;
    query.text = m_query->toPlainText();
    query.caseSensitive = m_caseSensitive->isChecked();
    query.allGroups = m_groups->isAllSelected();
    query.groups = m_groups->selectedKeys();
    m_searchPage = m_callbacks.currentPage();
    if (m_scope->currentIndex() == 1) {
        query.firstPage = query.lastPage = m_searchPage;
        if (m_searchPage < 0)
            query.text.clear();
    }
    if (m_scope->currentIndex() == 2) {
        query.firstPage = m_first->value() - 1;
        query.lastPage = m_last->value() - 1;
    }
    m_model->setResult(ProjectTextSearchService::search(project, query));
    m_count->setText(tr("Matches: %1").arg(m_model->rowCount()));
}
void ProjectSearchPanel::navigate(int row)
{
    if (m_callbacks.blocked())
        return;
    m_callbacks.commitEditor();
    if (m_model->result().revision != ProjectTextSearchService::revision(m_callbacks.project())) {
        search();
        return;
    }
    if (row < 0 || row >= m_model->rowCount())
        return;
    const auto match = m_model->result().matches.at(row);
    m_table->selectRow(row);
    m_callbacks.navigate(match);
}
void ProjectSearchPanel::step(int delta)
{
    if (!isVisible())
        showSearch();
    if (m_timer->isActive())
        search();
    const int count = m_model->rowCount();
    if (!count)
        return;
    const int row = m_table->currentIndex().row();
    navigate(row < 0 ? (delta > 0 ? 0 : count - 1) : (row + delta + count) % count);
}
void ProjectSearchPanel::replace(bool all)
{
    if (m_callbacks.blocked())
        return;
    m_callbacks.commitEditor();
    if (m_timer->isActive()) {
        search();
        if (!all)
            return;
    }
    const auto result = m_model->result();
    if (result.matches.isEmpty())
        return;
    QSet<int> chosen;
    const QString replacement = m_replacement->toPlainText();
    if (all) {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Replacement preview"));
        dialog.resize(900, 540);
        QVBoxLayout layout(&dialog);
        QTableWidget table(static_cast<int>(result.matches.size()), 4, &dialog);
        table.setHorizontalHeaderLabels({tr("Apply"), tr("Page / label"), tr("Before"), tr("After")});
        table.setEditTriggers(QAbstractItemView::NoEditTriggers);
        table.horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        table.horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
        for (int row = 0; row < result.matches.size(); ++row) {
            const auto& match = result.matches.at(row);
            auto* check = new QTableWidgetItem;
            check->setCheckState(Qt::Checked);
            table.setItem(row, 0, check);
            table.setItem(
                row, 1, new QTableWidgetItem(QStringLiteral("%1 / %2").arg(match.imageName).arg(match.labelIndex + 1)));
            table.setItem(row, 2, new QTableWidgetItem(match.text));
            table.setItem(row, 3, new QTableWidgetItem);
        }
        layout.addWidget(&table);
        QLabel summary(&dialog);
        layout.addWidget(&summary);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout.addWidget(&buttons);
        auto refreshPreview = [&] {
            const QSignalBlocker blocker(&table);
            chosen.clear();
            for (int row = 0; row < result.matches.size(); ++row)
                if (table.item(row, 0)->checkState() == Qt::Checked)
                    chosen.insert(row);
            const auto edits = ProjectTextSearchService::replacements(result, chosen, replacement);
            QMap<QPair<int, int>, QString> finalTexts;
            for (const auto& edit : edits)
                finalTexts.insert({edit.imageIndex, edit.labelIndex}, edit.after);
            for (int row = 0; row < result.matches.size(); ++row) {
                const auto& match = result.matches.at(row);
                table.item(row, 3)->setText(finalTexts.value({match.imageIndex, match.labelIndex}, match.text));
            }
            summary.setText(tr("Selected matches: %1; labels to change: %2").arg(chosen.size()).arg(edits.size()));
            buttons.button(QDialogButtonBox::Ok)->setEnabled(!edits.isEmpty());
        };
        const auto previewConnection =
            connect(&table, &QTableWidget::itemChanged, &dialog, [&](QTableWidgetItem* item) {
                if (item->column() == 0)
                    refreshPreview();
            });
        refreshPreview();
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        const int accepted = dialog.exec();
        disconnect(previewConnection);
        if (accepted != QDialog::Accepted)
            return;
    }
    else {
        if (!m_table->currentIndex().isValid())
            return;
        chosen.insert(m_table->currentIndex().row());
    }
    const auto edits = ProjectTextSearchService::replacements(result, chosen, replacement);
    const auto applied = m_callbacks.apply(result.revision, edits, tr("Replace project text"));
    if (!applied)
        QMessageBox::warning(this, tr("Replace"), applied.error());
    search();
}
