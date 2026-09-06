#pragma once
#include "core/AppPreferences.h"
#include "services/label/ProjectTextSearchService.h"
#include <QWidget>
#include <expected>
#include <functional>

class QAction;
class QPlainTextEdit;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QLabel;
class QTableView;
class QTimer;
class ProjectTextSearchModel;
class CheckableFilterButton;

class ProjectSearchPanel final : public QWidget {
    Q_OBJECT
public:
    struct Callbacks {
        std::function<const labelqt::core::Project&()> project;
        std::function<int()> currentPage;
        std::function<bool()> blocked;
        std::function<void()> commitEditor;
        std::function<void(const labelqt::services::ProjectTextMatch&)> navigate;
        std::function<void()> clearHighlight;
        std::function<std::expected<bool, QString>(const QByteArray&, QVector<labelqt::services::LabelTextReplacement>,
                                                   const QString&)>
            apply;
    };
    ProjectSearchPanel(Callbacks callbacks, QWidget* shortcutOwner, QWidget* parent = nullptr);
    ~ProjectSearchPanel() override;
    QList<QAction*> actions() const;
    void setPreferences(const labelqt::core::AppPreferences& preferences);
    void invalidate();
    void currentPageChanged();

private:
    void search();
    void navigate(int row);
    void step(int delta);
    void replace(bool all);
    void showSearch();
    Callbacks m_callbacks;
    QAction* m_findAction;
    QAction* m_nextAction;
    QAction* m_previousAction;
    QPlainTextEdit* m_query;
    QPlainTextEdit* m_replacement;
    QCheckBox* m_caseSensitive;
    QComboBox* m_scope;
    QSpinBox* m_first;
    QSpinBox* m_last;
    CheckableFilterButton* m_groups;
    QLabel* m_count;
    QTableView* m_table;
    ProjectTextSearchModel* m_model;
    QTimer* m_timer;
    QStringList m_knownGroups;
    int m_searchPage = -1;
};
