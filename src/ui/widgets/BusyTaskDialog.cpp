#include "ui/widgets/BusyTaskDialog.h"

#include "ui/widgets/DialogWindowUtils.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace labelqt::ui {

BusyTaskDialog::BusyTaskDialog(QString message, QString cancelText, QWidget* parent) : QDialog(parent)
{
    setWindowModality(Qt::ApplicationModal);
    setMinimumWidth(360);
    configureBusyDialogWindow(*this);

    auto* layout = new QVBoxLayout(this);
    m_messageLabel = new QLabel(std::move(message), this);
    auto* progressBar = new QProgressBar(this);
    progressBar->setRange(0, 0);
    auto* buttonLayout = new QHBoxLayout;
    m_cancelButton = new QPushButton(std::move(cancelText), this);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(m_cancelButton);
    layout->addWidget(m_messageLabel);
    layout->addWidget(progressBar);
    layout->addLayout(buttonLayout);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    adjustSize();
    setFixedSize(sizeHint());
}

} // namespace labelqt::ui
