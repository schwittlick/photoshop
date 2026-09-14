#pragma once
#include "ui/Tools.h"
#include "ui/panels/PanelBase.h"

class QComboBox;
class QPushButton;
class QLabel;

namespace re {

class GeometryPanel : public PanelBase {
    Q_OBJECT
public:
    explicit GeometryPanel(EditorSession* session, QWidget* parent = nullptr);
    void setCropAspect(CropAspect a);
public slots:
    void setActiveTool(Tool t);
signals:
    void toolRequested(Tool tool);
    void cropAspectChanged(CropAspect aspect);
    void autoCropRequested(bool keepAspect);
protected:
    void onParamsChanged(const EditParams& p) override;
private:
    QComboBox* aspect_;
    QPushButton* straightenBtn_;
    QPushButton* handlesBtn_;
    QPushButton* guidesBtn_;
    QPushButton* cropBtn_;
    QLabel* cropInfo_;
};

}  // namespace re
