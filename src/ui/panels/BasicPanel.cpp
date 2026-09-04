#include "ui/panels/BasicPanel.h"
#include "core/ColourMath.h"
#include "ui/widgets/CollapsibleGroup.h"
#include <QHBoxLayout>
#include <QPushButton>

namespace re {

BasicPanel::BasicPanel(EditorSession* session, QWidget* parent) : PanelBase(session, parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(4);

    auto* wb = new CollapsibleGroup(QStringLiteral("White Balance"), this);
    auto* temp = addSlider(wb->contentLayout(), QStringLiteral("Temperature"), colour::kTempMin, colour::kTempMax, 10, 0,
                           [](EditParams& p) -> float& { return p.wb.temp; });
    temp->setMiredScale(true);
    temp->setSuffix(QStringLiteral(" K"));
    temp->setToolTip(QStringLiteral("Colour temperature of the light the shot was taken in. Higher values make the image warmer."));
    addSlider(wb->contentLayout(), QStringLiteral("Tint"), -150, 150, 1, 0, [](EditParams& p) -> float& { return p.wb.tint; })
        ->setToolTip(QStringLiteral("Negative shifts the image towards green, positive towards magenta."));
    auto* wbRow = new QHBoxLayout();
    auto* pick = new QPushButton(QStringLiteral("Eyedropper"), this);
    pick->setToolTip(QStringLiteral("Click a neutral area in the image to set the white balance (W)"));
    auto* asShot = new QPushButton(QStringLiteral("As shot"), this);
    wbRow->addWidget(pick);
    wbRow->addWidget(asShot);
    wbRow->addStretch();
    wb->contentLayout()->addLayout(wbRow);
    connect(pick, &QPushButton::clicked, this, &BasicPanel::whiteBalancePickerRequested);
    connect(asShot, &QPushButton::clicked, this, [this] { session_->setWhiteBalanceAsShot(); });
    lay->addWidget(wb);

    auto* tone = new CollapsibleGroup(QStringLiteral("Tone"), this);
    addSlider(tone->contentLayout(), QStringLiteral("Exposure"), -5, 5, 0.01, 2, [](EditParams& p) -> float& { return p.tone.exposureEV; })
        ->setSuffix(QStringLiteral(" EV"));
    addSlider(tone->contentLayout(), QStringLiteral("Contrast"), -100, 100, 1, 0, [](EditParams& p) -> float& { return p.tone.contrast; });
    addSlider(tone->contentLayout(), QStringLiteral("Highlights"), -100, 100, 1, 0, [](EditParams& p) -> float& { return p.tone.highlights; });
    addSlider(tone->contentLayout(), QStringLiteral("Shadows"), -100, 100, 1, 0, [](EditParams& p) -> float& { return p.tone.shadows; });
    addSlider(tone->contentLayout(), QStringLiteral("Whites"), -100, 100, 1, 0, [](EditParams& p) -> float& { return p.tone.whites; });
    addSlider(tone->contentLayout(), QStringLiteral("Blacks"), -100, 100, 1, 0, [](EditParams& p) -> float& { return p.tone.blacks; });
    lay->addWidget(tone);

    auto* presence = new CollapsibleGroup(QStringLiteral("Presence"), this);
    addSlider(presence->contentLayout(), QStringLiteral("Vibrance"), -100, 100, 1, 0, [](EditParams& p) -> float& { return p.tone.vibrance; });
    addSlider(presence->contentLayout(), QStringLiteral("Saturation"), -100, 100, 1, 0, [](EditParams& p) -> float& { return p.tone.saturation; });
    lay->addWidget(presence);
    lay->addStretch();
}

}  // namespace re
