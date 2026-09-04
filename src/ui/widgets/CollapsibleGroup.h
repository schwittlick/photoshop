#pragma once
#include <QWidget>

class QToolButton;
class QVBoxLayout;

namespace re {

// A titled section with a disclosure arrow, used for the panel groups.
class CollapsibleGroup : public QWidget {
    Q_OBJECT
public:
    explicit CollapsibleGroup(const QString& title, QWidget* parent = nullptr, bool expanded = true);
    QVBoxLayout* contentLayout() const { return contentLayout_; }
    void addWidget(QWidget* w);
    void setExpanded(bool on);

private:
    QToolButton* header_;
    QWidget* content_;
    QVBoxLayout* contentLayout_;
};

}  // namespace re
