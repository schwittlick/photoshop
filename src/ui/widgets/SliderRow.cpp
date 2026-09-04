#include "ui/widgets/SliderRow.h"
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSlider>
#include <cmath>

namespace re {

namespace {
class ResetSlider : public QSlider {
public:
    explicit ResetSlider(QWidget* parent) : QSlider(Qt::Horizontal, parent) {}
    std::function<void()> onDoubleClick;
protected:
    void mouseDoubleClickEvent(QMouseEvent* e) override {
        if (onDoubleClick) onDoubleClick();
        e->accept();
    }
};
}  // namespace

SliderRow::SliderRow(const QString& label, double min, double max, double step, int decimals, QWidget* parent)
    : QWidget(parent), min_(min), max_(max), step_(step) {
    steps_ = std::clamp(int(std::lround((max - min) / step)), 1, 200000);
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    label_ = new QLabel(label, this);
    label_->setFixedWidth(labelWidth());
    auto* slider = new ResetSlider(this);
    slider_ = slider;
    slider_->setRange(0, steps_);
    slider_->setSingleStep(1);
    slider_->setPageStep(std::max(1, steps_ / 20));
    slider_->setFocusPolicy(Qt::ClickFocus);
    spin_ = new QDoubleSpinBox(this);
    spin_->setRange(min, max);
    spin_->setDecimals(decimals);
    spin_->setSingleStep(step);
    spin_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin_->setFixedWidth(64);
    spin_->setKeyboardTracking(true);
    spin_->setAlignment(Qt::AlignRight);
    lay->addWidget(label_);
    lay->addWidget(slider_, 1);
    lay->addWidget(spin_);

    debounce_.setSingleShot(true);
    debounce_.setInterval(350);
    connect(&debounce_, &QTimer::timeout, this, [this] { emit valueChanged(value_, false); });

    connect(slider_, &QSlider::valueChanged, this, [this](int i) {
        if (updating_) return;
        setValueInternal(sliderToValue(i), false);
    });
    connect(slider_, &QSlider::sliderPressed, this, [this] { dragging_ = true; });
    connect(slider_, &QSlider::sliderReleased, this, [this] {
        dragging_ = false;
        debounce_.stop();
        emit valueChanged(value_, false);
    });
    slider->onDoubleClick = [this] {
        debounce_.stop();
        setValueSilently(default_);
        emit valueChanged(value_, false);
    };
    connect(spin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (updating_) return;
        setValueInternal(v, true);
    });
    connect(spin_, &QDoubleSpinBox::editingFinished, this, [this] {
        debounce_.stop();
        emit valueChanged(value_, false);
    });
    syncWidgets();
}

void SliderRow::setSuffix(const QString& s) { spin_->setSuffix(s); }

double SliderRow::sliderToValue(int i) const {
    double t = double(i) / steps_;
    if (mired_ && min_ > 0) {
        double mMax = 1e6 / min_, mMin = 1e6 / max_;
        return 1e6 / (mMax - t * (mMax - mMin));
    }
    return min_ + t * (max_ - min_);
}

int SliderRow::valueToSlider(double v) const {
    double t;
    if (mired_ && min_ > 0) {
        double mMax = 1e6 / min_, mMin = 1e6 / max_;
        double m = 1e6 / std::max(v, 1.0);
        t = (mMax - m) / (mMax - mMin);
    } else {
        t = (v - min_) / (max_ - min_);
    }
    return std::clamp(int(std::lround(t * steps_)), 0, steps_);
}

void SliderRow::syncWidgets() {
    updating_ = true;
    slider_->setValue(valueToSlider(value_));
    spin_->setValue(value_);
    updating_ = false;
}

void SliderRow::setValueSilently(double v) {
    value_ = std::clamp(v, min_, max_);
    syncWidgets();
}

void SliderRow::setValueInternal(double v, bool fromSpinTyping) {
    value_ = std::clamp(v, min_, max_);
    syncWidgets();
    emit valueChanged(value_, true);
    if (!dragging_ && !fromSpinTyping) debounce_.start();
    if (fromSpinTyping) debounce_.start();
}

}  // namespace re
