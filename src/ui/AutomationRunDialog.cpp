#include "ui/AutomationRunDialog.h"

#include <QCloseEvent>
#include <QColor>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {
constexpr int automationRunDialogMaxBlocks = 5000;
constexpr int automationRunDialogMaxChunkCharacters = 65536;
} // namespace

AutomationRunDialog::AutomationRunDialog(const QString& scriptName, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Automation Running"));
    setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    resize(760, 460);

    auto* layout = new QVBoxLayout(this);
    m_statusLabel = new QLabel(tr("Running automation script: %1").arg(scriptName), this);
    layout->addWidget(m_statusLabel);

    m_logEdit = new QTextEdit(this);
    m_logEdit->setReadOnly(true);
    m_logEdit->setLineWrapMode(QTextEdit::NoWrap);
    m_logEdit->document()->setMaximumBlockCount(automationRunDialogMaxBlocks);
    layout->addWidget(m_logEdit, 1);

    auto* buttons = new QDialogButtonBox(this);
    m_cancelButton = buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    m_closeButton = buttons->addButton(tr("Close"), QDialogButtonBox::AcceptRole);
    m_closeButton->setEnabled(false);
    layout->addWidget(buttons);

    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        setCanceling();
        emit cancelRequested();
    });
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void AutomationRunDialog::appendStandardOutput(const QString& text)
{
    appendText(text, palette().color(QPalette::Text));
}

void AutomationRunDialog::appendStandardError(const QString& text)
{
    appendText(text, QColor(190, 64, 64));
}

void AutomationRunDialog::setFinished(bool success)
{
    m_finished = true;
    m_statusLabel->setText(success ? tr("Automation script finished.") : tr("Automation script failed."));
    m_cancelButton->setEnabled(false);
    m_closeButton->setEnabled(true);
    m_closeButton->setDefault(true);
}

void AutomationRunDialog::setCanceling()
{
    m_statusLabel->setText(tr("Canceling automation script..."));
    m_cancelButton->setEnabled(false);
}

void AutomationRunDialog::closeEvent(QCloseEvent* event)
{
    if (!m_finished) {
        hide();
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void AutomationRunDialog::appendText(const QString& text, const QColor& color)
{
    if (text.isEmpty()) {
        return;
    }

    QString displayText = text;
    if (displayText.size() > automationRunDialogMaxChunkCharacters) {
        displayText = tr("[Large automation log chunk was truncated.]\n") +
                      displayText.right(automationRunDialogMaxChunkCharacters);
    }

    QTextCursor cursor = m_logEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat format;
    format.setForeground(color);
    cursor.setCharFormat(format);
    cursor.insertText(displayText);
    m_logEdit->setTextCursor(cursor);
    m_logEdit->ensureCursorVisible();
}
