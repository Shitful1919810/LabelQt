#include "ui/PreferenceDialogWidgets.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QWidget>

namespace labelqt::ui::PreferenceDialogWidgets {

QDoubleSpinBox* makePositiveDoubleSpinBox(double value, double minimum, double maximum)
{
    auto* spinBox = new QDoubleSpinBox;
    spinBox->setRange(minimum, maximum);
    spinBox->setDecimals(2);
    spinBox->setSingleStep(1.0);
    spinBox->setValue(value);
    return spinBox;
}

QWidget* makeFontSelectorWidget(QWidget* parent, QLabel*& label, QPushButton*& chooseButton, QPushButton*& resetButton)
{
    auto* widget = new QWidget(parent);
    auto* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    label = new QLabel(widget);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    chooseButton = new QPushButton(QCoreApplication::translate("PreferenceDialog", "Choose Font..."), widget);
    resetButton = new QPushButton(QCoreApplication::translate("PreferenceDialog", "Use Default"), widget);
    layout->addWidget(label, 1);
    layout->addWidget(chooseButton);
    layout->addWidget(resetButton);
    return widget;
}

QWidget* makePercentScrollBarWidget(QWidget* parent, QScrollBar*& scrollBar, QLabel*& label, int value)
{
    auto* widget = new QWidget(parent);
    auto* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    scrollBar = new QScrollBar(Qt::Horizontal, widget);
    scrollBar->setRange(0, 100);
    scrollBar->setSingleStep(5);
    scrollBar->setPageStep(10);
    scrollBar->setValue(value);
    label = new QLabel(widget);
    label->setMinimumWidth(label->fontMetrics().horizontalAdvance(QStringLiteral("100%")));
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(scrollBar, 1);
    layout->addWidget(label);
    return widget;
}

QComboBox* makeModifierComboBox(QWidget* parent)
{
    auto* comboBox = new QComboBox(parent);
    comboBox->setEditable(true);
    comboBox->addItems({QStringLiteral("ctrl"), QStringLiteral("shift"), QStringLiteral("alt"), QStringLiteral("meta"),
                        QStringLiteral("ctrl+shift"), QStringLiteral("none")});
    return comboBox;
}

} // namespace labelqt::ui::PreferenceDialogWidgets
