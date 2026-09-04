#include "ui/widgets/HistogramWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace re {

HistogramWidget::HistogramWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(160, 72);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setToolTip(QStringLiteral("Histogram of the rendered image (RGB and luminance). Triangles mark clipped highlights/shadows."));
}

void HistogramWidget::setData(const HistogramData& d) {
    data_ = d;
    update();
}

void HistogramWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = rect().adjusted(1, 1, -1, -1);
    p.fillRect(r, QColor(28, 28, 28));
    p.setPen(QColor(60, 60, 60));
    for (int i = 1; i < 4; ++i) {
        double x = r.left() + r.width() * i / 4.0;
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    }
    if (!data_.valid || data_.total == 0) {
        p.setPen(QColor(120, 120, 120));
        p.drawText(r, Qt::AlignCenter, QStringLiteral("no image"));
        return;
    }
    double mx = 1;
    const std::array<uint32_t, 256>* chans[4] = {&data_.r, &data_.g, &data_.b, &data_.l};
    for (int c = 0; c < 4; ++c)
        for (int i = 1; i < 255; ++i) mx = std::max(mx, double((*chans[c])[i]));
    mx = std::sqrt(mx);
    auto pathFor = [&](const std::array<uint32_t, 256>& bins) {
        QPainterPath path;
        path.moveTo(r.left(), r.bottom());
        for (int i = 0; i < 256; ++i) {
            double h = std::min(1.0, std::sqrt(double(bins[i])) / mx);
            path.lineTo(r.left() + r.width() * (i + 0.5) / 256.0, r.bottom() - h * (r.height() - 8));
        }
        path.lineTo(r.right(), r.bottom());
        path.closeSubpath();
        return path;
    };
    p.setPen(Qt::NoPen);
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    p.fillPath(pathFor(data_.r), QColor(200, 40, 40, 160));
    p.fillPath(pathFor(data_.g), QColor(40, 200, 40, 160));
    p.fillPath(pathFor(data_.b), QColor(40, 90, 230, 160));
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setPen(QPen(QColor(230, 230, 230, 200), 1.0));
    p.setBrush(Qt::NoBrush);
    QPainterPath lum = pathFor(data_.l);
    p.drawPath(lum);
    // clipping indicators
    auto tri = [&](bool right, const QColor& col, bool on) {
        double x = right ? r.right() - 3 : r.left() + 3;
        QPolygonF t;
        t << QPointF(x, r.top() + 2) << QPointF(x + (right ? -7 : 7), r.top() + 2) << QPointF(x, r.top() + 9);
        p.setPen(Qt::NoPen);
        p.setBrush(on ? col : QColor(70, 70, 70));
        p.drawPolygon(t);
    };
    tri(false, QColor(80, 140, 255), data_.clippedLow > data_.total / 2000);
    tri(true, QColor(255, 80, 80), data_.clippedHigh > data_.total / 2000);
}

}  // namespace re
