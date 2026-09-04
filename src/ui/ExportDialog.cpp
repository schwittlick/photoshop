#include "ui/ExportDialog.h"
#include "ui/widgets/SliderRow.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QPushButton>
#include <QLabel>
#include <QSettings>
#include <QStandardItemModel>
#include <QDir>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>

namespace re {

ExportDialog::ExportDialog(EditorSession* session, QWidget* parent) : QDialog(parent), session_(session) {
    setWindowTitle(QStringLiteral("Export"));
    QSettings st;
    auto* lay = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    format_ = new QComboBox(this);
    for (int i = 0; i <= int(ExportFormat::Png16); ++i) format_->addItem(Exporter::formatName(ExportFormat(i)));
    format_->setCurrentIndex(st.value("export/format", 0).toInt());
    form->addRow(QStringLiteral("Format"), format_);

    compression_ = new QComboBox(this);
    compression_->addItems({QStringLiteral("None"), QStringLiteral("LZW"), QStringLiteral("Deflate")});
    compression_->setCurrentIndex(st.value("export/tiffCompression", 1).toInt());
    form->addRow(QStringLiteral("TIFF compression"), compression_);

    quality_ = new QSpinBox(this);
    quality_->setRange(1, 100);
    quality_->setValue(st.value("export/jpegQuality", 92).toInt());
    quality_->setToolTip(QStringLiteral("JPEG quality, always 4:4:4 chroma"));
    form->addRow(QStringLiteral("JPEG quality"), quality_);

    space_ = new QComboBox(this);
    for (int i = 0; i < icc::kSpaceCount; ++i) space_->addItem(icc::spaceName(icc::Space(i)));
    space_->setCurrentIndex(st.value("export/space", 0).toInt());
    form->addRow(QStringLiteral("Colour space"), space_);

    resize_ = new QCheckBox(QStringLiteral("Resize long edge to"), this);
    resize_->setChecked(st.value("export/resize", false).toBool());
    longEdge_ = new QSpinBox(this);
    longEdge_->setRange(16, 30000);
    longEdge_->setValue(st.value("export/longEdge", 2048).toInt());
    longEdge_->setSuffix(QStringLiteral(" px"));
    form->addRow(resize_, longEdge_);

    sampler_ = new QComboBox(this);
    sampler_->addItems({QStringLiteral("Catmull-Rom (as preview)"), QStringLiteral("Lanczos 3")});
    sampler_->setCurrentIndex(st.value("export/sampler", 0).toInt());
    sampler_->setToolTip(QStringLiteral("Resampling filter used by the geometry warp at full resolution"));
    form->addRow(QStringLiteral("Warp filter"), sampler_);

    exif_ = new QCheckBox(QStringLiteral("Copy EXIF metadata from the raw file"), this);
    exif_->setChecked(st.value("export/exif", true).toBool());
    form->addRow(QString(), exif_);
    lay->addLayout(form);

    sharpAmount_ = new SliderRow(QStringLiteral("Sharpen"), 0, 100, 1, 0, this);
    sharpRadius_ = new SliderRow(QStringLiteral("Radius"), 0.3, 3.0, 0.1, 1, this);
    sharpRadius_->setSuffix(QStringLiteral(" px"));
    sharpAmount_->setValueSilently(session_->params().outputSharpenAmount);
    sharpRadius_->setValueSilently(session_->params().outputSharpenRadius);
    sharpAmount_->setDefault(0);
    sharpRadius_->setDefault(0.8);
    auto* sharpNote = new QLabel(QStringLiteral("Output sharpening (unsharp mask after resize):"), this);
    lay->addWidget(sharpNote);
    lay->addWidget(sharpAmount_);
    lay->addWidget(sharpRadius_);
    connect(sharpAmount_, &SliderRow::valueChanged, this, [this](double v, bool interactive) {
        EditParams p = session_->params(); p.outputSharpenAmount = float(v); session_->setParams(p, interactive);
    });
    connect(sharpRadius_, &SliderRow::valueChanged, this, [this](double v, bool interactive) {
        EditParams p = session_->params(); p.outputSharpenRadius = float(v); session_->setParams(p, interactive);
    });

    sizeInfo_ = new QLabel(this);
    sizeInfo_->setStyleSheet("color: palette(mid);");
    lay->addWidget(sizeInfo_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* exportBtn = buttons->addButton(QStringLiteral("Export…"), QDialogButtonBox::AcceptRole);
    exportBtn->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &ExportDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ExportDialog::reject);
    lay->addWidget(buttons);

    connect(format_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { updateEnabled(); });
    connect(resize_, &QCheckBox::toggled, this, [this](bool) { updateEnabled(); updateSizeInfo(); });
    connect(longEdge_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { updateSizeInfo(); });
    updateEnabled();
    updateSizeInfo();
}

void ExportDialog::updateEnabled() {
    auto f = ExportFormat(format_->currentIndex());
    compression_->setEnabled(f == ExportFormat::Tiff16 || f == ExportFormat::Tiff8);
    quality_->setEnabled(f == ExportFormat::Jpeg);
    longEdge_->setEnabled(resize_->isChecked());
    // Rec.2020 linear only makes sense at 16 bit
    bool eight = Exporter::isEightBit(f);
    auto* model = qobject_cast<QStandardItemModel*>(space_->model());
    Q_UNUSED(model);
    if (eight && space_->currentIndex() == int(icc::Space::Rec2020Linear)) space_->setCurrentIndex(0);
}

void ExportDialog::updateSizeInfo() {
    if (!session_->hasImage()) { sizeInfo_->clear(); return; }
    auto img = session_->image();
    QRectF c = session_->params().geom.cropNorm;
    int w = int(std::lround(c.width() * img->width)), h = int(std::lround(c.height() * img->height));
    if (resize_->isChecked()) {
        double s = double(longEdge_->value()) / std::max(w, h);
        w = std::max(1, int(std::lround(w * s)));
        h = std::max(1, int(std::lround(h * s)));
    }
    sizeInfo_->setText(QStringLiteral("Output size: %1 × %2 px").arg(w).arg(h));
}

void ExportDialog::accept() {
    settings_.format = ExportFormat(format_->currentIndex());
    settings_.tiffCompression = TiffCompression(compression_->currentIndex());
    settings_.jpegQuality = quality_->value();
    settings_.colourSpace = icc::Space(space_->currentIndex());
    settings_.resize = resize_->isChecked();
    settings_.longEdge = longEdge_->value();
    settings_.sampler = sampler_->currentIndex() == 1 ? Sampler::Lanczos3 : Sampler::CatmullRom;
    settings_.copyExif = exif_->isChecked();
    if (Exporter::isEightBit(settings_.format) && settings_.colourSpace == icc::Space::Rec2020Linear) settings_.colourSpace = icc::Space::SRGB;

    QSettings st;
    st.setValue("export/format", format_->currentIndex());
    st.setValue("export/tiffCompression", compression_->currentIndex());
    st.setValue("export/jpegQuality", quality_->value());
    st.setValue("export/space", space_->currentIndex());
    st.setValue("export/resize", resize_->isChecked());
    st.setValue("export/longEdge", longEdge_->value());
    st.setValue("export/sampler", sampler_->currentIndex());
    st.setValue("export/exif", exif_->isChecked());

    QString ext = Exporter::defaultExtension(settings_.format);
    QFileInfo src(session_->filePath());
    QString dir = st.value("export/dir", src.absolutePath()).toString();
    QString suggested = QDir(dir).filePath(src.completeBaseName() + "." + ext);
    QString filter = QStringLiteral("%1 (*.%2)").arg(Exporter::formatName(settings_.format), ext);
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export image"), suggested, filter);
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += "." + ext;
    settings_.outputPath = path;
    st.setValue("export/dir", QFileInfo(path).absolutePath());
    QDialog::accept();
}

}  // namespace re
