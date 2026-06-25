#pragma once

#include <QObject>

#include <functional>

class CanvasLabelTextEditController;
class QPlainTextEdit;
class QTableView;

class EditorStateController final : public QObject {
    Q_OBJECT

public:
    enum class Mode {
        None,
        BottomEditor,
        TableTextEditor,
        CanvasTextEditor,
    };

    explicit EditorStateController(QObject* parent = nullptr);

    void setWidgets(QPlainTextEdit* bottomEditor, QTableView* labelView,
                    CanvasLabelTextEditController* canvasTextEditController);
    void setCallbacks(std::function<void()> commitCanvasTextEditor, std::function<void()> commitBottomEditor,
                      std::function<void()> editTableText, std::function<void()> openCanvasTextEditor);

    Mode activeMode() const;
    void commitActive();
    void restoreAfterNavigation(Mode mode, bool hasCurrentLabel);

private:
    QWidget* activeItemEditor() const;

    QPlainTextEdit* m_bottomEditor{nullptr};
    QTableView* m_labelView{nullptr};
    CanvasLabelTextEditController* m_canvasTextEditController{nullptr};
    std::function<void()> m_commitCanvasTextEditor;
    std::function<void()> m_commitBottomEditor;
    std::function<void()> m_editTableText;
    std::function<void()> m_openCanvasTextEditor;
};
