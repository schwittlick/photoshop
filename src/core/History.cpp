#include "core/History.h"

namespace re {

void History::reset(const EditParams& initial) {
    states_.clear();
    states_.push_back(initial);
    index_ = 0;
}

void History::commit(const EditParams& p) {
    if (states_.empty()) { reset(p); return; }
    if (states_[index_] == p) return;
    states_.erase(states_.begin() + long(index_) + 1, states_.end());
    states_.push_back(p);
    if (states_.size() > cap_) states_.pop_front();
    index_ = states_.size() - 1;
}

const EditParams& History::undo() {
    if (canUndo()) --index_;
    return states_[index_];
}

const EditParams& History::redo() {
    if (canRedo()) ++index_;
    return states_[index_];
}

}  // namespace re
