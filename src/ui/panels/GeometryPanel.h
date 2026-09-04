#pragma once
#include "ui/Tools.h"
#include "ui/panels/PanelBase.h"

class QComboBox;
class QLabel;

namespace re {

class GeometryPanel : public PanelBase {
    Q_OBJECT
public:
    explicit GeometryPanel(EditorSession* session, QWidget* parent = nullptr);
    void setCropAspect(CropAspect a);
signals:
    void toolRequested(Tool tool);
    void cropAspectChanged(CropAspect aspect);
    void autoCropRequested(bool keepAspect);
protected:
    void onParamsChanged(const EditParams& p) override;
private:
    QComboBox* aspect_;
    QLabel* cropInfo_;
};

}  // namespace re
