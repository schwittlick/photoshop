#pragma once
// Point-curve editor: click to add, drag to move, right-click to remove.
#include "core/EditParams.h"
#include <QWidget>
#include <array>
#include <cstdint>

namespace re {

class CurveEditor : public QWidget {
    Q_OBJECT
public:
    explicit CurveEditor(QWidget* parent = nullptr);
    QSize sizeHint() const override { return {260, 260}; }
    void setCurve(const CurvePoints& pts);
    const CurvePoints& curve() const { return pts_; }
    void setHistogram(const std::array<uint32_t, 256>& bins);
    void setAccent(const QColor& c) { accent_ = c; update(); }

signals:
    void curveChanged(const CurvePoints& pts, bool interactive);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    QRectF plotRect() const;
    QPointF toScreen(QPointF p) const;
    QPointF fromScreen(QPointF s) const;
    int hitPoint(QPointF s) const;

    CurvePoints pts_;
    std::array<uint32_t, 256> hist_{};
    bool hasHist_ = false;
    int drag_ = -1;
    QColor accent_{230, 230, 230};
};

}  // namespace re
