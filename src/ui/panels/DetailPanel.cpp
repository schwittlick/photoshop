#include "ui/panels/DetailPanel.h"
#include "ui/widgets/CollapsibleGroup.h"
#include <QLabel>

namespace re {

DetailPanel::DetailPanel(EditorSession* session, QWidget* parent) : PanelBase(session, parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(4);
    auto* sharpen = new CollapsibleGroup(QStringLiteral("Output sharpening"), this);
    auto* note = new QLabel(QStringLiteral("Unsharp mask applied on export, after any resize. Not shown in the preview."), this);
    note->setWordWrap(true);
    note->setStyleSheet("color: palette(mid);");
    sharpen->addWidget(note);
    addSlider(sharpen->contentLayout(), QStringLiteral("Amount"), 0, 100, 1, 0, [](EditParams& p) -> float& { return p.outputSharpenAmount; });
    addSlider(sharpen->contentLayout(), QStringLiteral("Radius"), 0.3, 3.0, 0.1, 1, [](EditParams& p) -> float& { return p.outputSharpenRadius; })
        ->setSuffix(QStringLiteral(" px"));
    lay->addWidget(sharpen);
    auto* nr = new CollapsibleGroup(QStringLiteral("Noise reduction"), this);
    auto* nrNote = new QLabel(QStringLiteral("Out of scope for this build: only what the AHD demosaic gives for free."), this);
    nrNote->setWordWrap(true);
    nrNote->setStyleSheet("color: palette(mid);");
    nr->addWidget(nrNote);
    lay->addWidget(nr);
    lay->addStretch();
}

}  // namespace re
