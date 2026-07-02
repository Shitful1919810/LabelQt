#pragma once

#include <QComboBox>
#include <QSize>

class PageSelectorComboBox final : public QComboBox {
    Q_OBJECT

public:
    explicit PageSelectorComboBox(QWidget* parent = nullptr);

    void addPage(const QString& imageName, int pageIndex);
    void clear();
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

protected:
    void changeEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QSize calculatedSizeHint() const;
    void invalidateSizeHint();

    mutable QSize m_cachedSizeHint;
    QString m_longestDisplayText;
};
