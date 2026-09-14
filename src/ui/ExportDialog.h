#pragma once
#include "io/Exporter.h"
#include "ui/EditorSession.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;

namespace re {

class SliderRow;

class ExportDialog : public QDialog {
    Q_OBJECT
public:
    // batchCount > 0: settings for exporting that many images into one folder. settings().outputPath is
    // then the folder and every file is named after its raw; the per-image sharpening sliders are hidden.
    ExportDialog(EditorSession* session, int batchCount, QWidget* parent = nullptr, const QString& noun = QStringLiteral("images"));
    const ExportSettings& settings() const { return settings_; }

protected:
    void accept() override;

private:
    void updateEnabled();
    void updateSizeInfo();

    EditorSession* session_;
    int batchCount_;
    QString noun_;
    ExportSettings settings_;
    QComboBox* format_;
    QComboBox* compression_;
    QSpinBox* quality_;
    QComboBox* space_;
    QCheckBox* resize_;
    QSpinBox* longEdge_;
    QComboBox* sampler_;
    QCheckBox* exif_;
    SliderRow* sharpAmount_ = nullptr;
    SliderRow* sharpRadius_ = nullptr;
    QLabel* sizeInfo_;
};

}  // namespace re
