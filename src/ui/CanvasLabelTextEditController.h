#pragma once

#include <QKeySequence>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QString>

class CanvasLabelTextEditor;
class QEvent;
class QFont;
class QWidget;

class CanvasLabelTextEditController final : public QObject {
    Q_OBJECT

public:
    explicit CanvasLabelTextEditController(QObject* parent = nullptr);

    void setCommitShortcut(QKeySequence shortcut);
    void setEditorOpacity(double opacity);
    bool isEditing() const noexcept;
    bool isEditorObject(QObject* object) const noexcept;
    bool hasEditorFocus() const noexcept;
    int imageIndex() const noexcept;
    int labelIndex() const noexcept;

    void open(QWidget* parent, int imageIndex, int labelIndex, const QString& text, const QFont& font,
              const QPoint& globalPosition);
    void commit();
    void cancel();
    void close();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void previewTextChanged(int labelIndex, QString text);
    void textCommitted(int imageIndex, int labelIndex, QString text);
    void closed(int labelIndex);

private:
    QPointer<CanvasLabelTextEditor> m_editor;
    QKeySequence m_commitShortcut{QStringLiteral("Ctrl+Return")};
    double m_editorOpacity{1.0};
    int m_imageIndex{-1};
    int m_labelIndex{-1};
};
