#include "ui/proofread/ProofreadNotesWidget.h"
#include <QAction>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMenu>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

using namespace labelqt::services;

ProofreadNotesWidget::ProofreadNotesWidget(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_toggle = new QToolButton(this);
    m_toggle->setCheckable(true);
    m_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toggle->setArrowType(Qt::RightArrow);
    layout->addWidget(m_toggle);
    auto* body = new QWidget(this);
    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    m_list = new QListWidget(body);
    m_list->setObjectName(QStringLiteral("proofreadNotes"));
    m_list->setWordWrap(true);
    m_list->setMaximumHeight(160);
    bodyLayout->addWidget(m_list);
    auto* bar = new QHBoxLayout;
    m_all = new QCheckBox(tr("All comments"), body);
    bar->addWidget(m_all);
    bar->addStretch();
    const auto button = [&](const QString& name, const QString& icon, QStyle::StandardPixmap fallback) {
        auto* result = new QToolButton(body);
        result->setIcon(QIcon::fromTheme(icon, style()->standardIcon(fallback)));
        result->setToolTip(name);
        result->setAccessibleName(name);
        bar->addWidget(result);
        return result;
    };
    m_add = button(tr("Add label comment"), QStringLiteral("list-add"), QStyle::SP_FileDialogNewFolder);
    m_edit = button(tr("Edit comment"), QStringLiteral("document-edit"), QStyle::SP_FileDialogContentsView);
    auto* more = button(tr("Comment actions"), QStringLiteral("application-menu"), QStyle::SP_TitleBarMenuButton);
    auto* menu = new QMenu(more);
    m_addPage = menu->addAction(tr("Add page comment"), this, [this] { request(Operation::AddPage); });
    m_resolve = menu->addAction(tr("Resolve / reopen"), this, [this] { request(Operation::Resolve); });
    m_jump = menu->addAction(tr("Jump to label"), this, [this] { request(Operation::Jump); });
    m_relink = menu->addAction(tr("Attach to selected label..."), this, [this] { request(Operation::Relink); });
    m_remove = menu->addAction(tr("Delete comment..."), this, [this] { request(Operation::Remove); });
    more->setMenu(menu);
    more->setPopupMode(QToolButton::InstantPopup);
    bodyLayout->addLayout(bar);
    layout->addWidget(body);
    body->hide();
    connect(m_toggle, &QToolButton::toggled, this, [this, body](bool checked) {
        body->setVisible(checked);
        m_toggle->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
    });
    connect(m_add, &QToolButton::clicked, this, [this] { request(Operation::AddLabel); });
    connect(m_edit, &QToolButton::clicked, this, [this] { request(Operation::Edit); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { request(Operation::Edit); });
    connect(m_all, &QCheckBox::toggled, this, &ProofreadNotesWidget::rebuildList);
}

ProofreadNotesWidget::~ProofreadNotesWidget()
{
    for (auto* child : findChildren<QObject*>())
        disconnect(child, nullptr, this, nullptr);
}

void ProofreadNotesWidget::request(Operation operation)
{
    const auto* item = m_list->currentItem();
    emit requested(operation, item ? item->data(Qt::UserRole).toUuid() : QUuid{});
}

void ProofreadNotesWidget::refresh(const std::optional<ReviewRound>& round, const labelqt::core::Project& project,
                                   int pageIndex, int labelIndex, bool editable)
{
    m_rows.clear();
    setVisible(round.has_value());
    if (!round)
        return;
    QUuid pageId, labelId;
    if (pageIndex >= 0 && pageIndex < project.images().size()) {
        const auto& page = project.images().at(pageIndex);
        pageId = page.id;
        if (labelIndex >= 0 && labelIndex < page.labels.size() && !page.labels.at(labelIndex).isDeleted())
            labelId = page.labels.at(labelIndex).id();
    }
    m_pageId = pageId;
    m_labelId = labelId;
    for (const auto& note : round->notes) {
        const auto [page, label] = ReviewRoundService::locateNote(project, note);
        const bool detached = page < 0 || (!note.labelId.isNull() && label < 0);
        const QString title = (detached ? tr("Needs association") : note.resolved ? tr("Resolved") : tr("Open"));
        m_rows.append({note.id, note.associatedPageId.isNull() ? note.pageId : note.associatedPageId,
                       note.associatedPageId.isNull() ? note.labelId : note.associatedLabelId,
                       QStringLiteral("%1 | %2\n%3").arg(note.pageName, title, note.text), note.quote, detached,
                       note.resolved});
    }
    rebuildList();
    m_toggle->setText(tr("Proofreading comments (%1)").arg(round->notes.size()));
    m_add->setEnabled(editable && !round->after && !labelId.isNull());
    m_addPage->setEnabled(editable && !round->after && !pageId.isNull());
    m_edit->setEnabled(editable && !round->after);
    m_remove->setEnabled(editable && !round->after);
    m_resolve->setEnabled(editable);
    m_relink->setEnabled(editable && !labelId.isNull());
    m_jump->setEnabled(true);
}

void ProofreadNotesWidget::rebuildList()
{
    const QUuid selected = m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toUuid() : QUuid{};
    m_list->clear();
    for (const auto& row : m_rows) {
        if (!m_all->isChecked() && (row.pageId != m_pageId || (!row.labelId.isNull() && row.labelId != m_labelId)))
            continue;
        auto* item = new QListWidgetItem(row.text, m_list);
        item->setData(Qt::UserRole, row.id);
        item->setToolTip(row.quote);
        item->setIcon(style()->standardIcon(row.detached   ? QStyle::SP_MessageBoxWarning
                                            : row.resolved ? QStyle::SP_DialogApplyButton
                                                           : QStyle::SP_MessageBoxInformation));
        if (row.id == selected)
            m_list->setCurrentItem(item);
    }
}
