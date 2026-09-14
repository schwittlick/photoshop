#include "ui/SyncDialog.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace re {

SyncDialog::SyncDialog(const QString& sourceName, int targetCount, QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Sync settings"));
    auto* lay = new QVBoxLayout(this);
    auto* intro = new QLabel(QStringLiteral("Copy these settings of <b>%1</b> to the %2 other loaded image%3:")
                                 .arg(sourceName.toHtmlEscaped()).arg(targetCount).arg(targetCount == 1 ? QString() : QStringLiteral("s")), this);
    intro->setWordWrap(true);
    lay->addWidget(intro);

    const SyncMask m = lastMask();
    auto box = [&](const QString& text, const QString& tip, bool on) {
        auto* cb = new QCheckBox(text, this);
        cb->setToolTip(tip);
        cb->setChecked(on);
        lay->addWidget(cb);
        return cb;
    };
    wb_ = box(QStringLiteral("White balance"), QStringLiteral("Temperature and tint"), m.whiteBalance);
    tone_ = box(QStringLiteral("Tone"), QStringLiteral("Exposure, contrast, highlights, shadows, whites, blacks, vibrance, saturation"), m.tone);
    curves_ = box(QStringLiteral("Curves"), QStringLiteral("Point curves (RGB and per channel) and the parametric regions"), m.curves);
    lens_ = box(QStringLiteral("Lens corrections"), QStringLiteral("Profile switches and the manual distortion, chromatic aberration and vignetting sliders"), m.lens);
    rotation_ = box(QStringLiteral("Rotation / straighten"), QStringLiteral("The rotation angle"), m.rotation);
    perspective_ = box(QStringLiteral("Perspective"), QStringLiteral("Corner handles and the vertical/horizontal keystone sliders"), m.perspective);
    crop_ = box(QStringLiteral("Crop"), QStringLiteral("The crop rectangle, in normalised frame coordinates"), m.crop);
    sharpen_ = box(QStringLiteral("Output sharpening"), QStringLiteral("Amount and radius of the export unsharp mask"), m.outputSharpen);

    auto* note = new QLabel(QStringLiteral("Each image gets one undo step; switch to it to undo."), this);
    note->setStyleSheet("color: palette(mid);");
    note->setWordWrap(true);
    lay->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* all = buttons->addButton(QStringLiteral("All"), QDialogButtonBox::ResetRole);
    auto* none = buttons->addButton(QStringLiteral("None"), QDialogButtonBox::ResetRole);
    auto* sync = buttons->addButton(QStringLiteral("Sync"), QDialogButtonBox::AcceptRole);
    sync->setDefault(true);
    connect(all, &QPushButton::clicked, this, [this] { for (auto* cb : {wb_, tone_, curves_, lens_, rotation_, perspective_, crop_, sharpen_}) cb->setChecked(true); });
    connect(none, &QPushButton::clicked, this, [this] { for (auto* cb : {wb_, tone_, curves_, lens_, rotation_, perspective_, crop_, sharpen_}) cb->setChecked(false); });
    connect(buttons, &QDialogButtonBox::accepted, this, &SyncDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SyncDialog::reject);
    lay->addWidget(buttons);
}

SyncMask SyncDialog::mask() const {
    SyncMask m;
    m.whiteBalance = wb_->isChecked();
    m.tone = tone_->isChecked();
    m.curves = curves_->isChecked();
    m.lens = lens_->isChecked();
    m.rotation = rotation_->isChecked();
    m.perspective = perspective_->isChecked();
    m.crop = crop_->isChecked();
    m.outputSharpen = sharpen_->isChecked();
    return m;
}

void SyncDialog::accept() {
    saveMask(mask());
    QDialog::accept();
}

SyncMask SyncDialog::lastMask() {
    QSettings st;
    SyncMask d, m;
    st.beginGroup(QStringLiteral("sync"));
    m.whiteBalance = st.value("whiteBalance", d.whiteBalance).toBool();
    m.tone = st.value("tone", d.tone).toBool();
    m.curves = st.value("curves", d.curves).toBool();
    m.lens = st.value("lens", d.lens).toBool();
    m.rotation = st.value("rotation", d.rotation).toBool();
    m.perspective = st.value("perspective", d.perspective).toBool();
    m.crop = st.value("crop", d.crop).toBool();
    m.outputSharpen = st.value("outputSharpen", d.outputSharpen).toBool();
    return m;
}

void SyncDialog::saveMask(const SyncMask& m) {
    QSettings st;
    st.beginGroup(QStringLiteral("sync"));
    st.setValue("whiteBalance", m.whiteBalance);
    st.setValue("tone", m.tone);
    st.setValue("curves", m.curves);
    st.setValue("lens", m.lens);
    st.setValue("rotation", m.rotation);
    st.setValue("perspective", m.perspective);
    st.setValue("crop", m.crop);
    st.setValue("outputSharpen", m.outputSharpen);
}

QString SyncDialog::describe(const SyncMask& m) {
    QStringList parts;
    if (m.whiteBalance) parts << QStringLiteral("white balance");
    if (m.tone) parts << QStringLiteral("tone");
    if (m.curves) parts << QStringLiteral("curves");
    if (m.lens) parts << QStringLiteral("lens");
    if (m.rotation) parts << QStringLiteral("rotation");
    if (m.perspective) parts << QStringLiteral("perspective");
    if (m.crop) parts << QStringLiteral("crop");
    if (m.outputSharpen) parts << QStringLiteral("output sharpening");
    return parts.isEmpty() ? QStringLiteral("nothing") : parts.join(QStringLiteral(", "));
}

}  // namespace re
