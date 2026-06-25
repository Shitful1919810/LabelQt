#include "ui/GroupStyleEditorWidget.h"

#include "ui/PreferenceDialogWidgets.h"

#include <QAbstractItemView>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace {
using namespace labelqt::ui::PreferenceDialogWidgets;

constexpr int colorColumn = 0;
constexpr int diameterColumn = 1;
constexpr int fontColumn = 2;
constexpr int shapeColumn = 3;

QColor colorFromStyleObject(const QJsonObject& style)
{
    const QJsonValue value = style.value(QStringLiteral("groupColor"));
    if (value.isString()) {
        const QColor color(value.toString());
        return color.isValid() ? color : QColor(QStringLiteral("#000000"));
    }
    return QColor(QStringLiteral("#000000"));
}

QString markerStyleFromObject(const QJsonObject& style)
{
    const QString markerStyle = style.value(QStringLiteral("markerStyle")).toString(QStringLiteral("circle"));
    return markerStyle == QStringLiteral("square") ? QStringLiteral("square") : QStringLiteral("circle");
}
} // namespace

GroupStyleEditorWidget::GroupStyleEditorWidget(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({tr("Color"), tr("Marker diameter"), tr("Font size"), tr("Marker style")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(m_table, 1);

    auto* buttonLayout = new QHBoxLayout;
    auto* addStyleButton = new QPushButton(tr("Add group style"), this);
    auto* removeStyleButton = new QPushButton(tr("Remove selected styles"), this);
    buttonLayout->addWidget(addStyleButton);
    buttonLayout->addWidget(removeStyleButton);
    buttonLayout->addStretch();
    layout->addLayout(buttonLayout);

    connect(addStyleButton, &QPushButton::clicked, this, [this]() {
        addStyleRow();
        emit changed();
    });
    connect(removeStyleButton, &QPushButton::clicked, this, [this]() {
        removeSelectedRows();
        emit changed();
    });
}

void GroupStyleEditorWidget::setMarkerDefaults(double markerDiameter, double fontPointSize)
{
    m_defaultMarkerDiameter = markerDiameter;
    m_defaultFontPointSize = fontPointSize;
}

void GroupStyleEditorWidget::loadStyles(const QJsonArray& styles)
{
    m_table->setRowCount(0);
    for (const QJsonValue& value : styles) {
        addStyleRow(value.toObject());
    }
}

QJsonArray GroupStyleEditorWidget::styles() const
{
    QJsonArray groupStyles;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto* colorButton = qobject_cast<QPushButton*>(m_table->cellWidget(row, colorColumn));
        auto* diameterSpinBox = qobject_cast<QDoubleSpinBox*>(m_table->cellWidget(row, diameterColumn));
        auto* fontSpinBox = qobject_cast<QDoubleSpinBox*>(m_table->cellWidget(row, fontColumn));
        auto* shapeComboBox = qobject_cast<QComboBox*>(m_table->cellWidget(row, shapeColumn));
        if (colorButton == nullptr || diameterSpinBox == nullptr || fontSpinBox == nullptr ||
            shapeComboBox == nullptr) {
            continue;
        }

        QJsonObject style;
        style.insert(QStringLiteral("groupColor"), colorButton->property("color").toString());
        style.insert(QStringLiteral("markerDiameter"), diameterSpinBox->value());
        style.insert(QStringLiteral("fontPointSize"), fontSpinBox->value());
        style.insert(QStringLiteral("markerStyle"), shapeComboBox->currentData().toString());
        groupStyles.append(style);
    }
    return groupStyles;
}

void GroupStyleEditorWidget::addStyleRow()
{
    addStyleRow(QJsonObject{});
}

void GroupStyleEditorWidget::addStyleRow(const QJsonObject& style)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setVerticalHeaderItem(row, new QTableWidgetItem(QString::number(row + 1)));

    const QColor color = colorFromStyleObject(style);
    auto* colorButton = new QPushButton(color.name(), m_table);
    colorButton->setProperty("color", color.name());
    colorButton->setStyleSheet(QStringLiteral("QPushButton { color: %1; }").arg(color.name()));
    m_table->setCellWidget(row, colorColumn, colorButton);

    auto* diameterSpinBox = makePositiveDoubleSpinBox(
        style.value(QStringLiteral("markerDiameter")).toDouble(m_defaultMarkerDiameter), 1.0, 256.0);
    m_table->setCellWidget(row, diameterColumn, diameterSpinBox);

    auto* fontSpinBox = makePositiveDoubleSpinBox(
        style.value(QStringLiteral("fontPointSize")).toDouble(m_defaultFontPointSize), 0.1, 256.0);
    m_table->setCellWidget(row, fontColumn, fontSpinBox);

    auto* shapeComboBox = new QComboBox(m_table);
    shapeComboBox->addItem(tr("Circle"), QStringLiteral("circle"));
    shapeComboBox->addItem(tr("Square"), QStringLiteral("square"));
    shapeComboBox->setCurrentIndex(markerStyleFromObject(style) == QStringLiteral("square") ? 1 : 0);
    m_table->setCellWidget(row, shapeColumn, shapeComboBox);

    connect(colorButton, &QPushButton::clicked, this, [this, row]() { chooseColor(row); });
    connect(diameterSpinBox, &QDoubleSpinBox::valueChanged, this, &GroupStyleEditorWidget::changed);
    connect(fontSpinBox, &QDoubleSpinBox::valueChanged, this, &GroupStyleEditorWidget::changed);
    connect(shapeComboBox, &QComboBox::currentIndexChanged, this, &GroupStyleEditorWidget::changed);
}

void GroupStyleEditorWidget::removeSelectedRows()
{
    QList<int> rows;
    const QModelIndexList selectedRows = m_table->selectionModel()->selectedRows();
    rows.reserve(selectedRows.size());
    for (const QModelIndex& index : selectedRows) {
        rows.append(index.row());
    }
    std::sort(rows.begin(), rows.end(), std::greater<>());
    for (int row : rows) {
        m_table->removeRow(row);
    }
}

void GroupStyleEditorWidget::chooseColor(int row)
{
    auto* colorButton = qobject_cast<QPushButton*>(m_table->cellWidget(row, colorColumn));
    if (colorButton == nullptr) {
        return;
    }

    const QColor currentColor(colorButton->property("color").toString());
    const QColor color = QColorDialog::getColor(currentColor, this, tr("Choose group color"));
    if (!color.isValid()) {
        return;
    }

    colorButton->setProperty("color", color.name());
    colorButton->setText(color.name());
    colorButton->setStyleSheet(QStringLiteral("QPushButton { color: %1; }").arg(color.name()));
    emit changed();
}
