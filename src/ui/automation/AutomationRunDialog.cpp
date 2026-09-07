#include "ui/automation/AutomationRunDialog.h"

#include <QCloseEvent>
#include <QColor>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {
constexpr int automationRunDialogMaxBlocks = 5000;
constexpr int automationRunDialogMaxChunkCharacters = 65536;
constexpr int automationRunDialogMaxCharacters = 262144;
constexpr int automationRunDialogMaxLineCharacters = 4096;
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
    m_logEdit->setUndoRedoEnabled(false);
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

AutomationRunDialog::~AutomationRunDialog()
{
    disconnect(m_cancelButton, nullptr, this, nullptr);
    disconnect(m_closeButton, nullptr, this, nullptr);
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

void AutomationRunDialog::reject()
{
    if (!m_finished) {
        hide();
        return;
    }
    QDialog::reject();
}

void AutomationRunDialog::appendText(const QString& text, const QColor& color)
{
    if (text.isEmpty()) {
        return;
    }

    QString displayText = text;
    if (displayText.size() > automationRunDialogMaxChunkCharacters) {
        QString tail = displayText.right(automationRunDialogMaxChunkCharacters);
        if (tail.front().isLowSurrogate())
            tail.remove(0, 1);
        displayText = tr("[Large automation log chunk was truncated.]\n") + tail;
    }

    auto* document = m_logEdit->document();
    auto* verticalScrollBar = m_logEdit->verticalScrollBar();
    const bool followTail = verticalScrollBar->value() == verticalScrollBar->maximum();
    const int horizontalPosition = m_logEdit->horizontalScrollBar()->value();
    QTextCursor scrollAnchor = m_logEdit->cursorForPosition(QPoint(0, 0));
    scrollAnchor.setKeepPositionOnInsert(true);
    const int anchorTop = m_logEdit->cursorRect(scrollAnchor).top();

    // Bound individual layout blocks as well as total text. Only scan this bounded
    // chunk, never flatten or re-layout an ever-growing no-newline stream.
    QString boundedText;
    boundedText.reserve(displayText.size() + displayText.size() / automationRunDialogMaxLineCharacters + 1);
    int lineLength = document->lastBlock().length() - 1;
    for (const QChar character : std::as_const(displayText)) {
        const bool afterCarriageReturn = m_previousLogCharacterWasCarriageReturn;
        m_previousLogCharacterWasCarriageReturn = character == QLatin1Char('\r');
        if (afterCarriageReturn && character == QLatin1Char('\n'))
            continue;
        const bool lineBreak = character == QLatin1Char('\n') || character == QLatin1Char('\r') ||
                               character == QChar::ParagraphSeparator || character == QChar::LineSeparator;
        if (!lineBreak && (lineLength >= automationRunDialogMaxLineCharacters ||
                           (lineLength == automationRunDialogMaxLineCharacters - 1 && character.isHighSurrogate()))) {
            boundedText += QLatin1Char('\n');
            lineLength = 0;
        }
        if (lineBreak) {
            boundedText += QLatin1Char('\n');
            lineLength = 0;
        }
        else {
            boundedText += character;
            ++lineLength;
        }
    }

    const int excess =
        document->characterCount() - 1 + static_cast<int>(boundedText.size()) - automationRunDialogMaxCharacters;
    if (excess > 0) {
        // Evict whole bounded blocks in one edit, preserving surviving cursors.
        QTextCursor trim(document);
        const QTextBlock endBlock = document->findBlock(excess).next();
        trim.setPosition(endBlock.isValid() ? endBlock.position() : document->characterCount() - 1,
                         QTextCursor::KeepAnchor);
        trim.removeSelectedText();
    }

    QTextCursor cursor(document);
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat format;
    format.setForeground(color);
    cursor.setCharFormat(format);
    cursor.insertText(boundedText);
    if (followTail) {
        verticalScrollBar->setValue(verticalScrollBar->maximum());
    }
    else {
        verticalScrollBar->setValue(verticalScrollBar->value() + m_logEdit->cursorRect(scrollAnchor).top() - anchorTop);
    }
    m_logEdit->horizontalScrollBar()->setValue(horizontalPosition);
}
