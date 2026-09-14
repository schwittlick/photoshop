#pragma once
#include "ui/panels/PanelBase.h"

class QPushButton;

namespace re {

class BasicPanel : public PanelBase {
    Q_OBJECT
public:
    explicit BasicPanel(EditorSession* session, QWidget* parent = nullptr);
signals:
    void whiteBalancePickerRequested();
    void autoToneRequested();
};

}  // namespace re
