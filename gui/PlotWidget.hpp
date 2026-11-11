#pragma once

#include <QPaintEvent>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QVector>
#include <QWidget>

// Minimal line plot drawn with QPainter (x = seconds, y = physical value).
// Deliberately dependency-free: no Qt Charts module needed. You can swap it for
// Qt Charts or QCustomPlot later without touching the rest of the GUI.
class PlotWidget : public QWidget {
public:
    explicit PlotWidget(QWidget* parent = nullptr);

    void setSeries(const QString& title, const QString& unit, QVector<QPointF> points);
    void clear();

    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString title_;
    QString unit_;
    QVector<QPointF> points_;
};
