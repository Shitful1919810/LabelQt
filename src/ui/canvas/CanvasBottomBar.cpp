#include "ui/canvas/CanvasBottomBar.h"

#include "ui/project/PageSelectorComboBox.h"

#include <QAction>
#include <QActionGroup>
#include <QButtonGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QShowEvent>
#include <QSizePolicy>
#include <QSlider>
#include <QStandardItemModel>
#include <QStyle>
#include <QToolButton>
#include <QWidgetAction>

#include <algorithm>

namespace {
constexpr int compactHysteresisWidth = 80;
}

CanvasBottomBar::CanvasBottomBar(QWidget* parent) : QWidget(parent)
{
    buildUi();
}

QSlider* CanvasBottomBar::zoomSlider() const noexcept
{
    return m_zoomSlider;
}

QToolButton* CanvasBottomBar::labelModeButton() const noexcept
{
    return m_labelModeButton;
}

QToolButton* CanvasBottomBar::selectionModeButton() const noexcept
{
    return m_selectionModeButton;
}

QComboBox* CanvasBottomBar::insertGroupComboBox() const noexcept
{
    return m_insertGroupComboBox;
}

QPushButton* CanvasBottomBar::previousButton() const noexcept
{
    return m_previousButton;
}

QPushButton* CanvasBottomBar::nextButton() const noexcept
{
    return m_nextButton;
}

PageSelectorComboBox* CanvasBottomBar::imageComboBox() const noexcept
{
    return m_imageComboBox;
}

QLabel* CanvasBottomBar::pageSourceLabel() const noexcept
{
    return m_pageSourceLabel;
}

void CanvasBottomBar::buildUi()
{
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

    auto* bottomLayout = new QHBoxLayout(this);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(6);

    m_zoomSlider = new QSlider(Qt::Horizontal, this);
    m_zoomSlider->setRange(10, 400);
    m_zoomSlider->setValue(100);
    m_zoomSlider->setMinimumWidth(80);
    m_zoomSlider->setMaximumWidth(240);
    m_zoomSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_zoomLabel = new QLabel(tr("Zoom"), this);
    bottomLayout->addWidget(m_zoomLabel);
    bottomLayout->addWidget(m_zoomSlider, 1);
    bottomLayout->addStretch();

    auto* interactionModeGroup = new QButtonGroup(this);
    interactionModeGroup->setExclusive(true);
    m_labelModeButton = new QToolButton(this);
    m_selectionModeButton = new QToolButton(this);
    m_labelModeButton->setText(tr("Label mode"));
    m_selectionModeButton->setText(tr("Selection mode"));
    m_labelModeButton->setCheckable(true);
    m_selectionModeButton->setCheckable(true);
    m_labelModeButton->setChecked(true);
    m_labelModeButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_selectionModeButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    interactionModeGroup->addButton(m_labelModeButton);
    interactionModeGroup->addButton(m_selectionModeButton);
    bottomLayout->addWidget(m_labelModeButton);
    bottomLayout->addWidget(m_selectionModeButton);
    bottomLayout->addStretch();

    m_insertGroupComboBox = new QComboBox(this);
    m_insertGroupComboBox->setMinimumWidth(90);
    m_insertGroupComboBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_addGroupButton = new QToolButton(this);
    m_removeGroupButton = new QToolButton(this);
    m_addGroupButton->setIcon(QIcon::fromTheme(QStringLiteral("document-new"), style()->standardIcon(QStyle::SP_FileIcon)));
    m_removeGroupButton->setIcon(QIcon::fromTheme(QStringLiteral("user-trash"), style()->standardIcon(QStyle::SP_TrashIcon)));
    m_addGroupButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_removeGroupButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_addGroupButton->setToolTip(tr("Add group"));
    m_removeGroupButton->setToolTip(tr("Remove selected group"));
    m_addGroupButton->setAutoRaise(true);
    m_removeGroupButton->setAutoRaise(true);
    m_addGroupButton->setFixedSize(24, 24);
    m_removeGroupButton->setFixedSize(24, 24);
    m_insertGroupLabel = new QLabel(tr("Insert group"), this);
    bottomLayout->addWidget(m_insertGroupLabel);
    bottomLayout->addWidget(m_insertGroupComboBox);
    bottomLayout->addWidget(m_addGroupButton);
    bottomLayout->addWidget(m_removeGroupButton);
    bottomLayout->addStretch();

    m_previousButton = new QPushButton(this);
    m_nextButton = new QPushButton(this);
    m_previousButton->setIcon(style()->standardIcon(QStyle::SP_ArrowLeft));
    m_nextButton->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));
    m_previousButton->setToolTip(tr("Previous"));
    m_nextButton->setToolTip(tr("Next"));
    m_previousButton->setFixedSize(40, 32);
    m_nextButton->setFixedSize(40, 32);
    m_imageComboBox = new PageSelectorComboBox(this);
    m_pageModel = new QStandardItemModel(this);
    m_imageComboBox->setModel(m_pageModel);
    m_imageComboBox->setMinimumWidth(96);
    m_imageComboBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    m_compactToolsButton = new QToolButton(this);
    m_compactToolsButton->setIcon(QIcon::fromTheme(
        QStringLiteral("view-more-symbolic"), style()->standardIcon(QStyle::SP_ToolBarHorizontalExtensionButton)));
    m_compactToolsButton->setIconSize(QSize(20, 20));
    m_compactToolsButton->setToolTip(tr("More tools"));
    m_compactToolsButton->setAccessibleName(tr("More tools"));
    m_compactToolsButton->setAutoRaise(false);
    m_compactToolsButton->setPopupMode(QToolButton::InstantPopup);
    m_compactToolsButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_compactToolsButton->setFixedSize(40, 32);
    m_compactToolsButton->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; width: 0px; }"));
    auto* compactToolsMenu = new QMenu(m_compactToolsButton);
    m_compactToolsButton->setMenu(compactToolsMenu);

    auto* compactZoomWidget = new QWidget(compactToolsMenu);
    auto* compactZoomLayout = new QHBoxLayout(compactZoomWidget);
    compactZoomLayout->setContentsMargins(8, 4, 8, 4);
    compactZoomLayout->setSpacing(8);
    m_compactZoomSlider = new QSlider(Qt::Horizontal, compactZoomWidget);
    m_compactZoomSlider->setRange(10, 400);
    m_compactZoomSlider->setValue(m_zoomSlider->value());
    m_compactZoomSlider->setMinimumWidth(240);
    m_compactZoomSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    compactZoomLayout->addWidget(new QLabel(tr("Zoom"), compactZoomWidget));
    compactZoomLayout->addWidget(m_compactZoomSlider, 1);
    auto* compactZoomAction = new QWidgetAction(compactToolsMenu);
    compactZoomAction->setDefaultWidget(compactZoomWidget);
    compactToolsMenu->addAction(compactZoomAction);

    m_compactImageComboBox = new PageSelectorComboBox(compactToolsMenu);
    m_compactImageComboBox->setModel(m_imageComboBox->model());
    m_compactImageComboBox->setMinimumWidth(240);
    auto* compactPageWidget = new QWidget(compactToolsMenu);
    auto* compactPageLayout = new QHBoxLayout(compactPageWidget);
    compactPageLayout->setContentsMargins(8, 4, 8, 4);
    compactPageLayout->setSpacing(8);
    compactPageLayout->addWidget(new QLabel(tr("Page"), compactPageWidget));
    compactPageLayout->addWidget(m_compactImageComboBox);
    auto* compactPageAction = new QWidgetAction(compactToolsMenu);
    compactPageAction->setDefaultWidget(compactPageWidget);
    compactToolsMenu->addAction(compactPageAction);

    auto* compactModeGroup = new QActionGroup(compactToolsMenu);
    compactModeGroup->setExclusive(true);
    QAction* compactLabelModeAction = compactToolsMenu->addAction(m_labelModeButton->text());
    compactLabelModeAction->setCheckable(true);
    compactLabelModeAction->setChecked(true);
    compactModeGroup->addAction(compactLabelModeAction);
    QAction* compactSelectionModeAction = compactToolsMenu->addAction(m_selectionModeButton->text());
    compactSelectionModeAction->setCheckable(true);
    compactModeGroup->addAction(compactSelectionModeAction);
    compactToolsMenu->addSeparator();
    QAction* compactAddGroupAction = compactToolsMenu->addAction(m_addGroupButton->toolTip());
    QAction* compactRemoveGroupAction = compactToolsMenu->addAction(m_removeGroupButton->toolTip());
    bottomLayout->addWidget(m_compactToolsButton);
    m_compactToolsButton->setVisible(false);

    m_pageSourceLabel = new QLabel(this);
    m_pageSourceLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pageSourceLabel->setVisible(false);
    bottomLayout->addWidget(m_pageSourceLabel);
    bottomLayout->addWidget(m_previousButton);
    bottomLayout->addWidget(m_imageComboBox);
    bottomLayout->addWidget(m_nextButton);

    connect(m_zoomSlider, &QSlider::valueChanged, m_compactZoomSlider, &QSlider::setValue);
    connect(m_compactZoomSlider, &QSlider::valueChanged, m_zoomSlider, &QSlider::setValue);
    connect(m_imageComboBox, &QComboBox::currentIndexChanged, m_compactImageComboBox, &QComboBox::setCurrentIndex);
    connect(m_compactImageComboBox, &QComboBox::currentIndexChanged, m_imageComboBox, &QComboBox::setCurrentIndex);
    connect(m_labelModeButton, &QToolButton::clicked, compactLabelModeAction,
            [compactLabelModeAction]() { compactLabelModeAction->setChecked(true); });
    connect(m_selectionModeButton, &QToolButton::clicked, compactSelectionModeAction,
            [compactSelectionModeAction]() { compactSelectionModeAction->setChecked(true); });
    connect(compactLabelModeAction, &QAction::triggered, m_labelModeButton, &QToolButton::click);
    connect(compactSelectionModeAction, &QAction::triggered, m_selectionModeButton, &QToolButton::click);
    connect(m_addGroupButton, &QToolButton::clicked, this, &CanvasBottomBar::addGroupRequested);
    connect(m_removeGroupButton, &QToolButton::clicked, this, &CanvasBottomBar::removeGroupRequested);
    connect(compactAddGroupAction, &QAction::triggered, this, &CanvasBottomBar::addGroupRequested);
    connect(compactRemoveGroupAction, &QAction::triggered, this, &CanvasBottomBar::removeGroupRequested);
}

void CanvasBottomBar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateCompactMode();
}

void CanvasBottomBar::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    updateCompactMode();
}

void CanvasBottomBar::updateCompactMode()
{
    setMinimumWidth(compactToolbarPreferredWidth());

    const int compactThreshold = inlineToolbarMinimumWidth();
    const int expandThreshold = compactThreshold + compactHysteresisWidth;
    const bool shouldCompact = m_compact ? width() < expandThreshold : width() < compactThreshold;
    if (m_compact == shouldCompact) {
        return;
    }

    m_compact = shouldCompact;
    const bool showInlineTools = !shouldCompact;
    m_zoomLabel->setVisible(showInlineTools);
    m_zoomSlider->setVisible(showInlineTools);
    m_labelModeButton->setVisible(showInlineTools);
    m_selectionModeButton->setVisible(showInlineTools);
    m_addGroupButton->setVisible(showInlineTools);
    m_removeGroupButton->setVisible(showInlineTools);
    m_imageComboBox->setVisible(showInlineTools);
    m_compactToolsButton->setVisible(shouldCompact);
}

int CanvasBottomBar::inlineToolbarMinimumWidth() const
{
    const QMargins margins = layout()->contentsMargins();
    return margins.left() + margins.right() + widgetMinimumWidth(m_zoomLabel) + widgetMinimumWidth(m_zoomSlider) +
           widgetMinimumWidth(m_labelModeButton) + widgetMinimumWidth(m_selectionModeButton) +
           widgetMinimumWidth(m_insertGroupLabel) + widgetMinimumWidth(m_insertGroupComboBox) +
           widgetMinimumWidth(m_addGroupButton) + widgetMinimumWidth(m_removeGroupButton) +
           widgetMinimumWidth(m_pageSourceLabel) + widgetMinimumWidth(m_previousButton) +
           widgetMinimumWidth(m_imageComboBox) + widgetMinimumWidth(m_nextButton);
}

int CanvasBottomBar::compactToolbarPreferredWidth() const
{
    const QMargins margins = layout()->contentsMargins();
    return margins.left() + margins.right() + widgetPreferredWidth(m_compactToolsButton) +
           widgetPreferredWidth(m_insertGroupLabel) + widgetPreferredWidth(m_insertGroupComboBox) +
           widgetPreferredWidth(m_pageSourceLabel) + widgetPreferredWidth(m_previousButton) +
           widgetPreferredWidth(m_nextButton);
}

int CanvasBottomBar::widgetMinimumWidth(const QWidget* widget) const
{
    if (widget == nullptr) {
        return 0;
    }
    return std::max(widget->minimumWidth(), widget->minimumSizeHint().width()) + layout()->spacing();
}

int CanvasBottomBar::widgetPreferredWidth(const QWidget* widget) const
{
    return widget == nullptr ? 0 : widget->sizeHint().width() + layout()->spacing();
}
