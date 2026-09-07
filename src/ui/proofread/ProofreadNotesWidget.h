#pragma once
#include "services/proofread/ReviewRoundService.h"
#include <QWidget>

class QListWidget;
class QCheckBox;
class QToolButton;
class QAction;
class ProofreadNotesWidget final : public QWidget {
    Q_OBJECT
public:
    enum class Operation { AddLabel, AddPage, Edit, Resolve, Relink, Remove, Jump };
    explicit ProofreadNotesWidget(QWidget* parent = nullptr);
    ~ProofreadNotesWidget() override;
    void refresh(const std::optional<labelqt::services::ReviewRound>& round, const labelqt::core::Project& project,
                 int pageIndex, int labelIndex, bool editable);
signals:
    void requested(ProofreadNotesWidget::Operation operation, QUuid noteId);

private:
    void request(Operation operation);
    void rebuildList();
    struct Row {
        QUuid id;
        QUuid pageId;
        QUuid labelId;
        QString text;
        QString quote;
        bool detached;
        bool resolved;
    };
    QListWidget* m_list;
    QCheckBox* m_all;
    QToolButton* m_toggle;
    QToolButton* m_add;
    QToolButton* m_edit;
    QAction* m_addPage;
    QAction* m_remove;
    QAction* m_relink;
    QAction* m_resolve;
    QAction* m_jump;
    QVector<Row> m_rows;
    QUuid m_pageId;
    QUuid m_labelId;
};
