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
    ExportDialog(EditorSession* session, QWidget* parent = nullptr);
    const ExportSettings& settings() const { return settings_; }

protected:
    void accept() override;

private:
    void updateEnabled();
    void updateSizeInfo();

    EditorSession* session_;
    ExportSettings settings_;
    QComboBox* format_;
    QComboBox* compression_;
    QSpinBox* quality_;
    QComboBox* space_;
    QCheckBox* resize_;
    QSpinBox* longEdge_;
    QComboBox* sampler_;
    QCheckBox* exif_;
    SliderRow* sharpAmount_;
    SliderRow* sharpRadius_;
    QLabel* sizeInfo_;
};

}  // namespace re
