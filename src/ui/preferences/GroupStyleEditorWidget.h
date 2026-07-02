#pragma once

#include <QJsonArray>
#include <QWidget>

class QTableWidget;

class GroupStyleEditorWidget final : public QWidget {
    Q_OBJECT

public:
    explicit GroupStyleEditorWidget(QWidget* parent = nullptr);

    void setMarkerDefaults(double markerDiameter, double fontPointSize);
    void loadStyles(const QJsonArray& styles);
    QJsonArray styles() const;

signals:
    void changed();

private:
    void addStyleRow();
    void addStyleRow(const QJsonObject& style);
    void removeSelectedRows();
    void chooseColor(int row);

    QTableWidget* m_table{nullptr};
    double m_defaultMarkerDiameter{20.0};
    double m_defaultFontPointSize{10.0};
};
