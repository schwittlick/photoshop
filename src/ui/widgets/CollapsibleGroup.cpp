#include "ui/widgets/CollapsibleGroup.h"
#include <QFrame>
#include <QToolButton>
#include <QVBoxLayout>

namespace re {

CollapsibleGroup::CollapsibleGroup(const QString& title, QWidget* parent, bool expanded) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 2, 0, 6);
    lay->setSpacing(2);
    header_ = new QToolButton(this);
    header_->setText(title);
    header_->setCheckable(true);
    header_->setChecked(expanded);
    header_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header_->setAutoRaise(true);
    header_->setStyleSheet("QToolButton { border: none; font-weight: bold; text-align: left; padding: 3px; }");
    header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    content_ = new QWidget(this);
    contentLayout_ = new QVBoxLayout(content_);
    contentLayout_->setContentsMargins(6, 2, 2, 2);
    contentLayout_->setSpacing(4);
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    lay->addWidget(header_);
    lay->addWidget(content_);
    lay->addWidget(line);
    connect(header_, &QToolButton::toggled, this, &CollapsibleGroup::setExpanded);
    setExpanded(expanded);
}

void CollapsibleGroup::addWidget(QWidget* w) { contentLayout_->addWidget(w); }

void CollapsibleGroup::setExpanded(bool on) {
    header_->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    if (header_->isChecked() != on) header_->setChecked(on);
    content_->setVisible(on);
}

}  // namespace re
