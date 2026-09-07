#pragma once

class QLabel;
class QPushButton;
class QComboBox;
class QDoubleSpinBox;
class QSlider;
class QWidget;

namespace labelqt::ui::PreferenceDialogWidgets {

QDoubleSpinBox* makePositiveDoubleSpinBox(double value, double minimum, double maximum);
QWidget* makeFontSelectorWidget(QWidget* parent, QLabel*& label, QPushButton*& chooseButton, QPushButton*& resetButton);
QWidget* makePercentSliderWidget(QWidget* parent, QSlider*& slider, QLabel*& label, int value);
QComboBox* makeModifierComboBox(QWidget* parent);

} // namespace labelqt::ui::PreferenceDialogWidgets
