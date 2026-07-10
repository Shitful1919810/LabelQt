#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QPushButton;

namespace labelqt::ui {

class BusyTaskDialog final : public QDialog {
    Q_OBJECT

public:
    BusyTaskDialog(QString message, QString cancelText, QWidget* parent = nullptr);

private:
    QLabel* m_messageLabel{nullptr};
    QPushButton* m_cancelButton{nullptr};
};

} // namespace labelqt::ui
