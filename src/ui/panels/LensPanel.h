#pragma once
#include "ui/panels/PanelBase.h"

class QCheckBox;
class QLabel;

namespace re {

class LensPanel : public PanelBase {
    Q_OBJECT
public:
    explicit LensPanel(EditorSession* session, QWidget* parent = nullptr);
signals:
    void autoCropRequested(bool keepAspect);
protected:
    void onParamsChanged(const EditParams& p) override;
    void onImageChanged() override;
private:
    QLabel* status_;
    QCheckBox* enable_;
    QCheckBox* ca_;
    SliderRow* profDist_;
    SliderRow* profVig_;
};

}  // namespace re
