#pragma once
// Label + slider + numeric field. Double-click resets to the default, the wheel
// nudges, typing commits on Enter. valueChanged(v, interactive=true) fires while a
// drag is in progress; a final valueChanged(v, false) marks the history commit point.
#include <QTimer>
#include <QWidget>
#include <functional>

class QLabel;
class QSlider;
class QDoubleSpinBox;

namespace re {

class SliderRow : public QWidget {
    Q_OBJECT
public:
    SliderRow(const QString& label, double min, double max, double step, int decimals, QWidget* parent = nullptr);

    void setValueSilently(double v);
    double value() const { return value_; }
    void setDefault(double d) { default_ = d; }
    double defaultValue() const { return default_; }
    void setSuffix(const QString& s);
    void setMiredScale(bool on) { mired_ = on; syncWidgets(); }
    static int labelWidth() { return 82; }

signals:
    void valueChanged(double value, bool interactive);

private:
    double sliderToValue(int i) const;
    int valueToSlider(double v) const;
    void syncWidgets();
    void setValueInternal(double v, bool fromSpinTyping);

    QLabel* label_;
    QSlider* slider_;
    QDoubleSpinBox* spin_;
    QTimer debounce_;
    double min_, max_, step_;
    double value_ = 0, default_ = 0;
    int steps_ = 1000;
    bool mired_ = false;
    bool dragging_ = false;
    bool updating_ = false;
};

}  // namespace re
