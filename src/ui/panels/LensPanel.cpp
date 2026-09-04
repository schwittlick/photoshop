#include "ui/panels/LensPanel.h"
#include "ui/widgets/CollapsibleGroup.h"
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace re {

LensPanel::LensPanel(EditorSession* session, QWidget* parent) : PanelBase(session, parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(4);

    auto* prof = new CollapsibleGroup(QStringLiteral("Profile (lensfun)"), this);
    status_ = new QLabel(QStringLiteral("no image"), this);
    status_->setWordWrap(true);
    status_->setStyleSheet("color: palette(mid);");
    prof->addWidget(status_);
    enable_ = new QCheckBox(QStringLiteral("Enable profile corrections"), this);
    prof->addWidget(enable_);
    connect(enable_, &QCheckBox::toggled, this, [this](bool on) {
        EditParams p = session_->params();
        p.lens.lensAuto = on;
        session_->setParams(p, false);
    });
    profDist_ = addSlider(prof->contentLayout(), QStringLiteral("Distortion"), 0, 200, 1, 0, [](EditParams& p) -> float& { return p.lens.profileDistortion; });
    profDist_->setSuffix(QStringLiteral(" %"));
    profVig_ = addSlider(prof->contentLayout(), QStringLiteral("Vignetting"), 0, 200, 1, 0, [](EditParams& p) -> float& { return p.lens.profileVignetting; });
    profVig_->setSuffix(QStringLiteral(" %"));
    ca_ = new QCheckBox(QStringLiteral("Remove chromatic aberration"), this);
    prof->addWidget(ca_);
    connect(ca_, &QCheckBox::toggled, this, [this](bool on) {
        EditParams p = session_->params();
        p.lens.profileCA = on;
        session_->setParams(p, false);
    });
    lay->addWidget(prof);

    auto* manual = new CollapsibleGroup(QStringLiteral("Manual distortion"), this);
    auto* distC = addSlider(manual->contentLayout(), QStringLiteral("Distortion"), -100, 100, 0.5, 1, [](EditParams& p) -> float& { return p.lens.distC; });
    distC->setToolTip(QStringLiteral("Barrel / pincushion. Positive corrects barrel distortion (ptlens c term). Applied on top of the profile."));
    auto* adv = new CollapsibleGroup(QStringLiteral("Advanced (ptlens a, b)"), this, false);
    addSlider(adv->contentLayout(), QStringLiteral("a (r³)"), -100, 100, 0.5, 1, [](EditParams& p) -> float& { return p.lens.distA; });
    addSlider(adv->contentLayout(), QStringLiteral("b (r²)"), -100, 100, 0.5, 1, [](EditParams& p) -> float& { return p.lens.distB; });
    manual->addWidget(adv);
    lay->addWidget(manual);

    auto* caGroup = new CollapsibleGroup(QStringLiteral("Manual chromatic aberration"), this);
    addSlider(caGroup->contentLayout(), QStringLiteral("Red / cyan"), -100, 100, 0.5, 1, [](EditParams& p) -> float& { return p.lens.caRed; });
    addSlider(caGroup->contentLayout(), QStringLiteral("Blue / yellow"), -100, 100, 0.5, 1, [](EditParams& p) -> float& { return p.lens.caBlue; });
    lay->addWidget(caGroup);

    auto* vig = new CollapsibleGroup(QStringLiteral("Manual vignetting"), this);
    addSlider(vig->contentLayout(), QStringLiteral("Amount"), -100, 100, 1, 0, [](EditParams& p) -> float& { return p.lens.vignetteAmount; });
    addSlider(vig->contentLayout(), QStringLiteral("Midpoint"), 0, 100, 1, 0, [](EditParams& p) -> float& { return p.lens.vignetteMidpoint; });
    lay->addWidget(vig);

    auto* row = new QHBoxLayout();
    auto* fit = new QPushButton(QStringLiteral("Auto-crop to fit"), this);
    fit->setToolTip(QStringLiteral("Crop to the largest rectangle without empty corners, keeping the current crop aspect"));
    row->addWidget(fit);
    row->addStretch();
    lay->addLayout(row);
    connect(fit, &QPushButton::clicked, this, [this] { emit autoCropRequested(true); });
    lay->addStretch();
    onImageChanged();
}

void LensPanel::onParamsChanged(const EditParams& p) {
    enable_->blockSignals(true);
    enable_->setChecked(p.lens.lensAuto);
    enable_->blockSignals(false);
    ca_->blockSignals(true);
    ca_->setChecked(p.lens.profileCA);
    ca_->blockSignals(false);
}

void LensPanel::onImageChanged() {
    const LensModel& lm = session_->lensModel();
    bool has = session_->hasImage() && lm.hasProfile();
    if (!session_->hasImage()) status_->setText(QStringLiteral("no image"));
    else if (has) {
        QStringList what;
        if (lm.hasDistortion()) what << QStringLiteral("distortion");
        if (lm.hasTCA()) what << QStringLiteral("CA");
        if (lm.hasVignetting()) what << QStringLiteral("vignetting");
        status_->setText(QStringLiteral("%1\n%2").arg(lm.statusText(), what.isEmpty() ? QStringLiteral("no calibration data") : what.join(", ")));
    } else {
        status_->setText(QStringLiteral("%1 — use the manual sliders below").arg(session_->lensStatus()));
    }
    enable_->setEnabled(has);
    profDist_->setEnabled(has && lm.hasDistortion());
    profVig_->setEnabled(has && lm.hasVignetting());
    ca_->setEnabled(has && lm.hasTCA());
    onParamsChanged(session_->params());
}

}  // namespace re
