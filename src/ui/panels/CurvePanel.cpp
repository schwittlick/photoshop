#include "ui/panels/CurvePanel.h"
#include "ui/widgets/CollapsibleGroup.h"
#include "ui/widgets/CurveEditor.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace re {

CurvePanel::CurvePanel(EditorSession* session, QWidget* parent) : PanelBase(session, parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(4);

    auto* point = new CollapsibleGroup(QStringLiteral("Point curve"), this);
    auto* row = new QHBoxLayout();
    auto* lbl = new QLabel(QStringLiteral("Channel"), this);
    lbl->setFixedWidth(SliderRow::labelWidth());
    channel_ = new QComboBox(this);
    channel_->addItems({QStringLiteral("RGB"), QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue")});
    auto* reset = new QPushButton(QStringLiteral("Reset"), this);
    row->addWidget(lbl);
    row->addWidget(channel_, 1);
    row->addWidget(reset);
    point->contentLayout()->addLayout(row);
    editor_ = new CurveEditor(this);
    point->addWidget(editor_);
    lay->addWidget(point);

    connect(channel_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        static const QColor cols[4] = {QColor(230, 230, 230), QColor(240, 80, 80), QColor(80, 220, 80), QColor(90, 140, 255)};
        editor_->setAccent(cols[i]);
        onParamsChanged(session_->params());
    });
    connect(editor_, &CurveEditor::curveChanged, this, [this](const CurvePoints& pts, bool interactive) {
        EditParams p = session_->params();
        channelCurve(p) = pts;
        session_->setParams(p, interactive);
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        EditParams p = session_->params();
        channelCurve(p) = CurvePoints();
        session_->setParams(p, false);
    });

    auto* para = new CollapsibleGroup(QStringLiteral("Parametric"), this);
    addSlider(para->contentLayout(), QStringLiteral("Highlights"), -100, 100, 1, 0, [](EditParams& p) -> float& { return p.tone.pHighlights; });
    addSlider(para->contentLayout(), QStringLiteral("Lights"), -100, 100, 1, 0, [](EditParams& p) -> float& { return p.tone.pLights; });
    addSlider(para->contentLayout(), QStringLiteral("Darks"), -100, 100, 1, 0, [](EditParams& p) -> float& { return p.tone.pDarks; });
    addSlider(para->contentLayout(), QStringLiteral("Shadows"), -100, 100, 1, 0, [](EditParams& p) -> float& { return p.tone.pShadows; });
    lay->addWidget(para);
    lay->addStretch();
    onParamsChanged(session_->params());
}

CurvePoints& CurvePanel::channelCurve(EditParams& p) const {
    switch (channel_->currentIndex()) {
        case 1: return p.tone.curveR;
        case 2: return p.tone.curveG;
        case 3: return p.tone.curveB;
        default: return p.tone.curveMaster;
    }
}

void CurvePanel::setHistogram(const HistogramData& h) {
    hist_ = h;
    if (!h.valid) return;
    switch (channel_->currentIndex()) {
        case 1: editor_->setHistogram(h.r); break;
        case 2: editor_->setHistogram(h.g); break;
        case 3: editor_->setHistogram(h.b); break;
        default: editor_->setHistogram(h.l); break;
    }
}

void CurvePanel::onParamsChanged(const EditParams& p) {
    EditParams tmp = p;
    editor_->setCurve(channelCurve(tmp));
}

}  // namespace re
