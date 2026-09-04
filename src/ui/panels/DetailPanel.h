#pragma once
#include "ui/panels/PanelBase.h"

namespace re {

class DetailPanel : public PanelBase {
    Q_OBJECT
public:
    explicit DetailPanel(EditorSession* session, QWidget* parent = nullptr);
};

}  // namespace re
