#include "PlotWidget.hpp"

#include <QColor>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPolygonF>
#include <QRectF>
#include <algorithm>
#include <utility>

PlotWidget::PlotWidget(QWidget* parent) : QWidget(parent) {
    setAutoFillBackground(false);
}

void PlotWidget::setSeries(const QString& title, const QString& unit, QVector<QPointF> points) {
    title_ = title;
    unit_ = unit;
    points_ = std::move(points);
    update();
}

void PlotWidget::clear() {
    title_.clear();
    unit_.clear();
    points_.clear();
    update();
}

QSize PlotWidget::minimumSizeHint() const {
    return QSize(240, 140);
}

void PlotWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), palette().color(QPalette::Base));

    const QColor textColor = palette().color(QPalette::Text);
    p.setPen(textColor);

    if (points_.isEmpty()) {
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("Load a DBC and a log, then pick a signal to plot"));
        return;
    }

    const double left = 70.0, right = 16.0, top = 28.0, bottom = 28.0;
    const QRectF plot(left, top, width() - left - right, height() - top - bottom);
    if (plot.width() < 20.0 || plot.height() < 20.0) return;

    double xmin = points_.first().x(), xmax = xmin;
    double ymin = points_.first().y(), ymax = ymin;
    for (const QPointF& pt : points_) {
        xmin = std::min(xmin, pt.x());
        xmax = std::max(xmax, pt.x());
        ymin = std::min(ymin, pt.y());
        ymax = std::max(ymax, pt.y());
    }
    if (xmax <= xmin) xmax = xmin + 1.0;
    if (ymax <= ymin) {   // flat line: give it some vertical room
        ymin -= 1.0;
        ymax += 1.0;
    }

    const auto mapX = [&](double x) {
        return plot.left() + (x - xmin) / (xmax - xmin) * plot.width();
    };
    const auto mapY = [&](double y) {
        return plot.bottom() - (y - ymin) / (ymax - ymin) * plot.height();
    };

    // Grid and axis labels (5 ticks on each axis)
    QColor gridColor = textColor;
    gridColor.setAlpha(45);
    const int ticks = 4;
    for (int i = 0; i <= ticks; ++i) {
        const double fraction = static_cast<double>(i) / ticks;

        const double y = ymin + fraction * (ymax - ymin);
        const double py = mapY(y);
        p.setPen(gridColor);
        p.drawLine(QPointF(plot.left(), py), QPointF(plot.right(), py));
        p.setPen(textColor);
        p.drawText(QRectF(0.0, py - 8.0, left - 6.0, 16.0), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(y, 'g', 5));

        const double x = xmin + fraction * (xmax - xmin);
        const double px = mapX(x);
        p.setPen(gridColor);
        p.drawLine(QPointF(px, plot.top()), QPointF(px, plot.bottom()));
        p.setPen(textColor);
        p.drawText(QRectF(px - 30.0, plot.bottom() + 4.0, 60.0, 16.0),
                   Qt::AlignHCenter | Qt::AlignTop, QString::number(x, 'f', 2) + QStringLiteral(" s"));
    }

    // Frame around the plot area
    p.setPen(gridColor);
    p.drawRect(plot);

    // Title with unit
    QString heading = title_;
    if (!unit_.isEmpty()) heading += QStringLiteral("  [") + unit_ + QStringLiteral("]");
    p.setPen(textColor);
    p.drawText(QRectF(left, 4.0, plot.width(), top - 8.0), Qt::AlignLeft | Qt::AlignVCenter,
               heading);

    // The data
    QPolygonF poly;
    poly.reserve(points_.size());
    for (const QPointF& pt : points_) poly << QPointF(mapX(pt.x()), mapY(pt.y()));

    p.setClipRect(plot);
    p.setPen(QPen(palette().color(QPalette::Highlight), 1.6));
    p.drawPolyline(poly);
}
