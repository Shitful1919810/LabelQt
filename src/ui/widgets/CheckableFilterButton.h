#pragma once

#include <QSet>
#include <QString>
#include <QToolButton>
#include <QVector>

struct CheckableFilterOption {
    QString key;
    QString text;
};

class CheckableFilterButton final : public QToolButton {
    Q_OBJECT

public:
    explicit CheckableFilterButton(QWidget* parent = nullptr);

    void setTexts(QString allText, QString noneText, QString countText);
    void setOptions(QVector<CheckableFilterOption> options);
    QSet<QString> selectedKeys() const;
    QStringList selectedTexts() const;
    bool isAllSelected() const;

signals:
    void selectionChanged();

private:
    void rebuildMenu();
    void refreshMenuChecks();
    void selectAll();
    void clearSelection();
    void updateButtonText();

    QVector<CheckableFilterOption> m_options;
    QSet<QString> m_selectedKeys;
    QString m_allText;
    QString m_noneText;
    QString m_countText;
};
