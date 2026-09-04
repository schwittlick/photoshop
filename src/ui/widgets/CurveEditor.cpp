#include "ui/widgets/CurveEditor.h"
#include "core/CurveModel.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace re {

CurveEditor::CurveEditor(QWidget* parent) : QWidget(parent) {
    setMinimumSize(200, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(false);
    setToolTip(QStringLiteral("Click to add a point, drag to move, right-click to remove."));
}

void CurveEditor::setCurve(const CurvePoints& pts) {
    pts_ = pts;
    if (pts_.pts.size() < 2) pts_ = CurvePoints();
    update();
}

void CurveEditor::setHistogram(const std::array<uint32_t, 256>& bins) {
    hist_ = bins;
    hasHist_ = true;
    update();
}

QRectF CurveEditor::plotRect() const {
    double s = std::min(width(), height()) - 16.0;
    return QRectF((width() - s) / 2.0, (height() - s) / 2.0, s, s);
}

QPointF CurveEditor::toScreen(QPointF p) const {
    QRectF r = plotRect();
    return {r.left() + p.x() * r.width(), r.bottom() - p.y() * r.height()};
}

QPointF CurveEditor::fromScreen(QPointF s) const {
    QRectF r = plotRect();
    return {std::clamp((s.x() - r.left()) / r.width(), 0.0, 1.0), std::clamp((r.bottom() - s.y()) / r.height(), 0.0, 1.0)};
}

int CurveEditor::hitPoint(QPointF s) const {
    for (size_t i = 0; i < pts_.pts.size(); ++i) {
        QPointF d = toScreen(pts_.pts[i]) - s;
        if (std::hypot(d.x(), d.y()) <= 8.0) return int(i);
    }
    return -1;
}

void CurveEditor::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = plotRect();
    p.fillRect(r, QColor(30, 30, 30));
    if (hasHist_) {
        double mx = 1;
        for (int i = 1; i < 255; ++i) mx = std::max(mx, std::sqrt(double(hist_[i])));
        QPainterPath hp;
        hp.moveTo(r.left(), r.bottom());
        for (int i = 0; i < 256; ++i)
            hp.lineTo(r.left() + r.width() * (i + 0.5) / 256.0, r.bottom() - std::min(1.0, std::sqrt(double(hist_[i])) / mx) * r.height() * 0.9);
        hp.lineTo(r.right(), r.bottom());
        p.fillPath(hp, QColor(90, 90, 90, 120));
    }
    p.setPen(QColor(65, 65, 65));
    for (int i = 1; i < 4; ++i) {
        p.drawLine(QPointF(r.left() + r.width() * i / 4, r.top()), QPointF(r.left() + r.width() * i / 4, r.bottom()));
        p.drawLine(QPointF(r.left(), r.top() + r.height() * i / 4), QPointF(r.right(), r.top() + r.height() * i / 4));
    }
    p.setPen(QPen(QColor(90, 90, 90), 1, Qt::DashLine));
    p.drawLine(r.bottomLeft(), r.topRight());
    p.setPen(QPen(QColor(120, 120, 120), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);

    QPainterPath curve;
    for (int i = 0; i <= 256; ++i) {
        double x = i / 256.0;
        QPointF s = toScreen(QPointF(x, curve::evaluate(pts_, x)));
        if (i == 0) curve.moveTo(s); else curve.lineTo(s);
    }
    p.setPen(QPen(accent_, 2));
    p.drawPath(curve);
    for (size_t i = 0; i < pts_.pts.size(); ++i) {
        QPointF s = toScreen(pts_.pts[i]);
        p.setPen(QPen(Qt::black, 1));
        p.setBrush(int(i) == drag_ ? QColor(255, 200, 60) : accent_);
        p.drawEllipse(s, 4.5, 4.5);
    }
}

void CurveEditor::mousePressEvent(QMouseEvent* e) {
    int hit = hitPoint(e->position());
    if (e->button() == Qt::RightButton) {
        if (hit > 0 && hit + 1 < int(pts_.pts.size())) {
            pts_.pts.erase(pts_.pts.begin() + hit);
            update();
            emit curveChanged(pts_, false);
        }
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    if (hit < 0) {
        if (!plotRect().adjusted(-6, -6, 6, 6).contains(e->position())) return;
        hit = curve::insertPoint(pts_, fromScreen(e->position()));
    }
    drag_ = hit;
    update();
    emit curveChanged(pts_, true);
}

void CurveEditor::mouseMoveEvent(QMouseEvent* e) {
    if (drag_ < 0) return;
    QPointF v = fromScreen(e->position());
    auto& pts = pts_.pts;
    const double gap = 0.004;
    if (drag_ == 0) v.setX(0);
    else if (drag_ + 1 == int(pts.size())) v.setX(1);
    else v.setX(std::clamp(v.x(), pts[drag_ - 1].x() + gap, pts[drag_ + 1].x() - gap));
    pts[drag_] = v;
    update();
    emit curveChanged(pts_, true);
}

void CurveEditor::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || drag_ < 0) return;
    drag_ = -1;
    update();
    emit curveChanged(pts_, false);
}

}  // namespace re
