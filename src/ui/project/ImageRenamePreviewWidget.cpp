#include "ui/project/ImageRenamePreviewWidget.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

class SingleLinePatternEdit final : public QPlainTextEdit {
public:
    explicit SingleLinePatternEdit(QWidget* parent = nullptr) : QPlainTextEdit(parent)
    {
        setLineWrapMode(QPlainTextEdit::NoWrap);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setTabChangesFocus(true);
        setFixedHeight(fontMetrics().lineSpacing() + 14);
    }

    QSize sizeHint() const override
    {
        QSize size = QPlainTextEdit::sizeHint();
        size.setHeight(fontMetrics().lineSpacing() + 14);
        return size;
    }

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event != nullptr && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
            event->accept();
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
        stripLineBreaks();
    }

    void insertFromMimeData(const QMimeData* source) override
    {
        if (source == nullptr) {
            return;
        }
        insertPlainText(source->text().replace(QRegularExpression(QStringLiteral(R"(\s*\R\s*)")), QString()));
        stripLineBreaks();
    }

private:
    void stripLineBreaks()
    {
        const QString text = toPlainText();
        if (!text.contains(QLatin1Char('\n')) && !text.contains(QLatin1Char('\r'))) {
            return;
        }
        const int position = textCursor().position();
        QString singleLineText = text;
        singleLineText.replace(QLatin1Char('\n'), QString());
        singleLineText.replace(QLatin1Char('\r'), QString());
        setPlainText(singleLineText);
        QTextCursor cursor = textCursor();
        cursor.setPosition(std::min(position, static_cast<int>(singleLineText.size())));
        setTextCursor(cursor);
    }
};

class RenamePatternHighlighter final : public QSyntaxHighlighter {
public:
    explicit RenamePatternHighlighter(QTextDocument* document) : QSyntaxHighlighter(document)
    {
        m_validFormat.setForeground(QColor(50, 180, 90));
        m_validFormat.setFontWeight(QFont::DemiBold);
        m_invalidFormat.setForeground(QColor(220, 60, 60));
        m_invalidFormat.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        m_invalidFormat.setUnderlineColor(QColor(220, 60, 60));
    }

protected:
    void highlightBlock(const QString& text) override
    {
        qsizetype cursor = 0;
        while (cursor < text.size()) {
            const qsizetype open = text.indexOf(QLatin1Char('{'), cursor);
            const qsizetype closeBeforeOpen = text.indexOf(QLatin1Char('}'), cursor);
            if (closeBeforeOpen >= 0 && (open < 0 || closeBeforeOpen < open)) {
                setFormat(static_cast<int>(closeBeforeOpen), 1, m_invalidFormat);
                cursor = closeBeforeOpen + 1;
                continue;
            }
            if (open < 0) {
                break;
            }

            const qsizetype close = text.indexOf(QLatin1Char('}'), open + 1);
            if (close < 0) {
                setFormat(static_cast<int>(open), static_cast<int>(text.size() - open), m_invalidFormat);
                break;
            }

            const QString token = text.sliced(open + 1, close - open - 1);
            const bool isIndexPadding = QRegularExpression(QStringLiteral(R"(^index:0[1-9]\d*$)")).match(token).hasMatch();
            const bool isValid = token == QStringLiteral("stem") || token == QStringLiteral("index") || isIndexPadding;
            setFormat(static_cast<int>(open), static_cast<int>(close - open + 1),
                      isValid ? m_validFormat : m_invalidFormat);
            cursor = close + 1;
        }
    }

private:
    QTextCharFormat m_validFormat;
    QTextCharFormat m_invalidFormat;
};

} // namespace

ImageRenamePreviewWidget::ImageRenamePreviewWidget(QWidget* parent) : QWidget(parent)
{
    buildUi();
    updatePreview();
}

void ImageRenamePreviewWidget::setProject(labelqt::core::Project project)
{
    m_project = std::move(project);
    updatePreview();
}

void ImageRenamePreviewWidget::setTargetImageIndexes(QVector<int> imageIndexes)
{
    if (m_targetImageIndexes == imageIndexes) {
        return;
    }

    m_targetImageIndexes = std::move(imageIndexes);
    updatePreview();
}

labelqt::services::ProjectImageRenamePlan ImageRenamePreviewWidget::renamePlan() const
{
    return m_plan;
}

void ImageRenamePreviewWidget::setRenameQueued(bool queued)
{
    if (m_isRenameQueued == queued) {
        return;
    }
    m_isRenameQueued = queued;
    updateStatus();
}

void ImageRenamePreviewWidget::buildUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(8);

    auto* hintLabel = new QLabel(
        tr("Rename image files according to the current page order. The extension is always preserved."), this);
    hintLabel->setWordWrap(true);
    rootLayout->addWidget(hintLabel);

    auto* controlLayout = new QGridLayout;
    controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->setHorizontalSpacing(8);
    controlLayout->setVerticalSpacing(6);

    m_patternEdit = new SingleLinePatternEdit(this);
    new RenamePatternHighlighter(m_patternEdit->document());
    m_patternEdit->setPlainText(QStringLiteral("{index:03}"));
    m_patternEdit->setPlaceholderText(tr("Supported: {index}, {index:03}, {stem}"));
    m_startIndexSpinBox = new QSpinBox(this);
    m_startIndexSpinBox->setRange(0, 999999);
    m_startIndexSpinBox->setValue(1);
    m_startIndexSpinBox->setMaximumWidth(130);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);

    m_actionButton = new QPushButton(tr("Queue Rename..."), this);
    m_actionButton->setMinimumWidth(160);
    connect(m_actionButton, &QPushButton::clicked, this, [this]() {
        if (m_plan.isValid() && m_plan.hasChanges()) {
            setRenameQueued(true);
            emit renameQueued(m_plan);
        }
    });

    controlLayout->addWidget(new QLabel(tr("File name pattern"), this), 0, 0);
    controlLayout->addWidget(m_patternEdit, 0, 1);
    controlLayout->addWidget(new QLabel(tr("Start index"), this), 0, 2);
    controlLayout->addWidget(m_startIndexSpinBox, 0, 3);
    controlLayout->addWidget(m_statusLabel, 1, 1, 1, 2);
    controlLayout->addWidget(m_actionButton, 1, 3);
    controlLayout->setColumnStretch(1, 1);
    rootLayout->addLayout(controlLayout);

    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    connect(m_patternEdit, &QPlainTextEdit::textChanged, this, &ImageRenamePreviewWidget::updatePreview);
    connect(m_startIndexSpinBox, &QSpinBox::valueChanged, this, &ImageRenamePreviewWidget::updatePreview);
}

void ImageRenamePreviewWidget::updatePreview()
{
    m_isRenameQueued = false;
    m_plan = labelqt::services::ProjectImageRenameService::createPlan(m_project, options());

    updateStatus();
    if (m_actionButton != nullptr) {
        m_actionButton->setEnabled(m_plan.isValid() && m_plan.hasChanges());
    }
    emit previewChanged(m_plan);
}

void ImageRenamePreviewWidget::updateStatus()
{
    if (m_plan.isValid()) {
        const int changedCount = static_cast<int>(std::count_if(m_plan.items.cbegin(), m_plan.items.cend(),
                                                                [](const auto& item) { return item.isChanged(); }));
        QString status = tr("%n image file(s) will be renamed.", nullptr, changedCount);
        if (m_isRenameQueued) {
            status += QLatin1Char(' ');
            status += tr("Queued until you click OK.");
        }
        m_statusLabel->setText(status);
    } else {
        m_statusLabel->setText(m_plan.error);
    }
}

labelqt::services::ProjectImageRenameOptions ImageRenamePreviewWidget::options() const
{
    return {
        m_patternEdit == nullptr ? QString() : m_patternEdit->toPlainText(),
        m_startIndexSpinBox == nullptr ? 1 : m_startIndexSpinBox->value(),
        m_targetImageIndexes,
    };
}
