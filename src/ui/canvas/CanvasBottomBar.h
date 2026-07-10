#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QComboBox;
class QSlider;
class QStandardItemModel;
class QToolButton;
class PageSelectorComboBox;

class CanvasBottomBar final : public QWidget {
    Q_OBJECT

public:
    explicit CanvasBottomBar(QWidget* parent = nullptr);

    QSlider* zoomSlider() const noexcept;
    QToolButton* labelModeButton() const noexcept;
    QToolButton* selectionModeButton() const noexcept;
    QComboBox* insertGroupComboBox() const noexcept;
    QPushButton* previousButton() const noexcept;
    QPushButton* nextButton() const noexcept;
    PageSelectorComboBox* imageComboBox() const noexcept;
    QLabel* pageSourceLabel() const noexcept;

signals:
    void addGroupRequested();
    void removeGroupRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void buildUi();
    void updateCompactMode();
    int inlineToolbarMinimumWidth() const;
    int compactToolbarPreferredWidth() const;
    int widgetMinimumWidth(const QWidget* widget) const;
    int widgetPreferredWidth(const QWidget* widget) const;

    QSlider* m_zoomSlider{nullptr};
    QSlider* m_compactZoomSlider{nullptr};
    QToolButton* m_labelModeButton{nullptr};
    QToolButton* m_selectionModeButton{nullptr};
    QComboBox* m_insertGroupComboBox{nullptr};
    QToolButton* m_addGroupButton{nullptr};
    QToolButton* m_removeGroupButton{nullptr};
    QPushButton* m_previousButton{nullptr};
    QPushButton* m_nextButton{nullptr};
    QStandardItemModel* m_pageModel{nullptr};
    PageSelectorComboBox* m_imageComboBox{nullptr};
    PageSelectorComboBox* m_compactImageComboBox{nullptr};
    QLabel* m_pageSourceLabel{nullptr};
    QLabel* m_zoomLabel{nullptr};
    QLabel* m_insertGroupLabel{nullptr};
    QToolButton* m_compactToolsButton{nullptr};
    bool m_compact{false};
};
