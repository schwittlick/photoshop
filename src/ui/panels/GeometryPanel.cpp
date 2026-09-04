#include "ui/panels/GeometryPanel.h"
#include "ui/widgets/CollapsibleGroup.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <cmath>

namespace re {

GeometryPanel::GeometryPanel(EditorSession* session, QWidget* parent) : PanelBase(session, parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(4);

    auto* rot = new CollapsibleGroup(QStringLiteral("Rotate"), this);
    addSlider(rot->contentLayout(), QStringLiteral("Angle"), -45, 45, 0.01, 2, [](EditParams& p) -> float& { return p.geom.rotationDeg; })
        ->setSuffix(QStringLiteral("°"));
    auto* rotRow = new QHBoxLayout();
    auto* straighten = new QPushButton(QStringLiteral("Straighten tool"), this);
    straighten->setToolTip(QStringLiteral("Drag a line along a horizon or vertical edge (A)"));
    rotRow->addWidget(straighten);
    rotRow->addStretch();
    rot->contentLayout()->addLayout(rotRow);
    connect(straighten, &QPushButton::clicked, this, [this] { emit toolRequested(Tool::Straighten); });
    lay->addWidget(rot);

    auto* persp = new CollapsibleGroup(QStringLiteral("Perspective"), this);
    addSlider(persp->contentLayout(), QStringLiteral("Vertical"), -100, 100, 1, 0, [](EditParams& p) -> float& { return p.geom.perspVertical; })
        ->setToolTip(QStringLiteral("Keystone correction for converging verticals"));
    addSlider(persp->contentLayout(), QStringLiteral("Horizontal"), -100, 100, 1, 0, [](EditParams& p) -> float& { return p.geom.perspHorizontal; });
    auto* pRow = new QHBoxLayout();
    auto* handles = new QPushButton(QStringLiteral("Corner handles"), this);
    handles->setToolTip(QStringLiteral("Drag the four corners of the frame freely (P)"));
    auto* resetP = new QPushButton(QStringLiteral("Reset"), this);
    pRow->addWidget(handles);
    pRow->addWidget(resetP);
    pRow->addStretch();
    persp->contentLayout()->addLayout(pRow);
    connect(handles, &QPushButton::clicked, this, [this] { emit toolRequested(Tool::Perspective); });
    connect(resetP, &QPushButton::clicked, this, [this] {
        EditParams p = session_->params();
        p.geom.corners = {};
        p.geom.perspVertical = p.geom.perspHorizontal = 0;
        session_->setParams(p, false);
    });
    lay->addWidget(persp);

    auto* crop = new CollapsibleGroup(QStringLiteral("Crop"), this);
    auto* aRow = new QHBoxLayout();
    auto* aLabel = new QLabel(QStringLiteral("Aspect"), this);
    aLabel->setFixedWidth(SliderRow::labelWidth());
    aspect_ = new QComboBox(this);
    aspect_->addItems({QStringLiteral("Free"), QStringLiteral("Original"), QStringLiteral("1:1"), QStringLiteral("4:3"),
                       QStringLiteral("3:2"), QStringLiteral("16:9")});
    aRow->addWidget(aLabel);
    aRow->addWidget(aspect_, 1);
    crop->contentLayout()->addLayout(aRow);
    connect(aspect_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) { emit cropAspectChanged(CropAspect(i)); });
    cropInfo_ = new QLabel(this);
    cropInfo_->setStyleSheet("color: palette(mid);");
    crop->addWidget(cropInfo_);
    auto* cRow = new QHBoxLayout();
    auto* cropTool = new QPushButton(QStringLiteral("Crop tool"), this);
    cropTool->setToolTip(QStringLiteral("Drag the crop rectangle and its handles (C)"));
    auto* resetC = new QPushButton(QStringLiteral("Reset"), this);
    cRow->addWidget(cropTool);
    cRow->addWidget(resetC);
    cRow->addStretch();
    crop->contentLayout()->addLayout(cRow);
    connect(cropTool, &QPushButton::clicked, this, [this] { emit toolRequested(Tool::Crop); });
    connect(resetC, &QPushButton::clicked, this, [this] {
        EditParams p = session_->params();
        p.geom.cropNorm = QRectF(0, 0, 1, 1);
        session_->setParams(p, false);
    });
    auto* fRow = new QHBoxLayout();
    auto* fitFree = new QPushButton(QStringLiteral("Auto-crop (largest)"), this);
    fitFree->setToolTip(QStringLiteral("Largest rectangle without empty corners, any aspect"));
    auto* fitKeep = new QPushButton(QStringLiteral("Auto-crop (keep aspect)"), this);
    fRow->addWidget(fitFree);
    fRow->addWidget(fitKeep);
    crop->contentLayout()->addLayout(fRow);
    connect(fitFree, &QPushButton::clicked, this, [this] { emit autoCropRequested(false); });
    connect(fitKeep, &QPushButton::clicked, this, [this] { emit autoCropRequested(true); });
    lay->addWidget(crop);
    lay->addStretch();
    onParamsChanged(session_->params());
}

void GeometryPanel::setCropAspect(CropAspect a) {
    aspect_->blockSignals(true);
    aspect_->setCurrentIndex(int(a));
    aspect_->blockSignals(false);
}

void GeometryPanel::onParamsChanged(const EditParams& p) {
    if (!session_->hasImage()) { cropInfo_->clear(); return; }
    auto img = session_->image();
    int w = int(std::lround(p.geom.cropNorm.width() * img->width));
    int h = int(std::lround(p.geom.cropNorm.height() * img->height));
    cropInfo_->setText(QStringLiteral("%1 × %2 px  (%3 MP)").arg(w).arg(h).arg(w * double(h) / 1e6, 0, 'f', 1));
}

}  // namespace re
