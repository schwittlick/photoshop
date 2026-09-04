#pragma once
// Common plumbing for the edit panels: binds SliderRows to EditParams fields in
// both directions and keeps defaults in sync for double-click reset.
#include "core/EditParams.h"
#include "ui/EditorSession.h"
#include "ui/widgets/SliderRow.h"
#include <QVBoxLayout>
#include <QWidget>
#include <functional>
#include <vector>

namespace re {

class PanelBase : public QWidget {
public:
    using Accessor = std::function<float&(EditParams&)>;
    PanelBase(EditorSession* session, QWidget* parent = nullptr);

protected:
    SliderRow* addSlider(QVBoxLayout* layout, const QString& label, double min, double max, double step, int decimals,
                         Accessor acc);
    void syncAll(const EditParams& p);
    void syncDefaults();
    // Called after every params change (also mid-drag) for panel-specific widgets.
    virtual void onParamsChanged(const EditParams&) {}
    virtual void onImageChanged() {}

    EditorSession* session_;

private:
    struct Binding { SliderRow* row; Accessor acc; };
    std::vector<Binding> bindings_;
};

}  // namespace re
