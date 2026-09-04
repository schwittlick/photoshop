#pragma once
#include "gpu/RenderBackend.h"
#include <QWidget>

namespace re {

// RGB + luminance histogram with clipping indicators, fed from the GPU pass.
class HistogramWidget : public QWidget {
    Q_OBJECT
public:
    explicit HistogramWidget(QWidget* parent = nullptr);
    QSize sizeHint() const override { return {256, 96}; }
public slots:
    void setData(const HistogramData& d);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    HistogramData data_;
};

}  // namespace re
