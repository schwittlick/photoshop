#pragma once
#include "gpu/RenderBackend.h"
#include "ui/panels/PanelBase.h"

class QComboBox;

namespace re {

class CurveEditor;

class CurvePanel : public PanelBase {
    Q_OBJECT
public:
    explicit CurvePanel(EditorSession* session, QWidget* parent = nullptr);
public slots:
    void setHistogram(const HistogramData& h);
protected:
    void onParamsChanged(const EditParams& p) override;
private:
    CurvePoints& channelCurve(EditParams& p) const;
    QComboBox* channel_;
    CurveEditor* editor_;
    HistogramData hist_;
};

}  // namespace re
