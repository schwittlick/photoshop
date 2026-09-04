#include "ui/panels/PanelBase.h"

namespace re {

PanelBase::PanelBase(EditorSession* session, QWidget* parent) : QWidget(parent), session_(session) {
    QObject::connect(session_, &EditorSession::paramsChanged, this, [this](const EditParams& p, bool) {
        syncAll(p);
        onParamsChanged(p);
    });
    QObject::connect(session_, &EditorSession::imageChanged, this, [this] {
        syncDefaults();
        syncAll(session_->params());
        onImageChanged();
    });
}

SliderRow* PanelBase::addSlider(QVBoxLayout* layout, const QString& label, double min, double max, double step, int decimals,
                                Accessor acc) {
    auto* row = new SliderRow(label, min, max, step, decimals, this);
    layout->addWidget(row);
    EditParams d = session_->defaults();
    row->setDefault(acc(d));
    EditParams cur = session_->params();
    row->setValueSilently(acc(cur));
    QObject::connect(row, &SliderRow::valueChanged, this, [this, acc](double v, bool interactive) {
        EditParams p = session_->params();
        if (acc(p) == float(v) && !interactive) { session_->commit(); return; }
        acc(p) = float(v);
        session_->setParams(p, interactive);
    });
    bindings_.push_back({row, std::move(acc)});
    return row;
}

void PanelBase::syncAll(const EditParams& p) {
    EditParams tmp = p;
    for (auto& b : bindings_) b.row->setValueSilently(b.acc(tmp));
}

void PanelBase::syncDefaults() {
    EditParams d = session_->defaults();
    for (auto& b : bindings_) b.row->setDefault(b.acc(d));
}

}  // namespace re
