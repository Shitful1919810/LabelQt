#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QPushButton;
class QTextEdit;

class AutomationRunDialog final : public QDialog {
    Q_OBJECT

public:
    explicit AutomationRunDialog(const QString& scriptName, QWidget* parent = nullptr);

    void appendStandardOutput(const QString& text);
    void appendStandardError(const QString& text);
    void setFinished(bool success);
    void setCanceling();

signals:
    void cancelRequested();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void appendText(const QString& text, const QColor& color);

    QLabel* m_statusLabel{nullptr};
    QTextEdit* m_logEdit{nullptr};
    QPushButton* m_cancelButton{nullptr};
    QPushButton* m_closeButton{nullptr};
    bool m_finished{false};
};
