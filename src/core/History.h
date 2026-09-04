#pragma once
// Bounded undo/redo of EditParams snapshots. Continuous slider drags are
// coalesced by the caller: commit() is called on release, not per change.
#include "core/EditParams.h"
#include <deque>

namespace re {

class History {
public:
    explicit History(size_t capacity = 100) : cap_(capacity) {}

    void reset(const EditParams& initial);
    // Records `p` as the new current state. No-op if identical to the current state.
    void commit(const EditParams& p);
    bool canUndo() const { return index_ > 0; }
    bool canRedo() const { return index_ + 1 < states_.size(); }
    const EditParams& undo();
    const EditParams& redo();
    const EditParams& current() const { return states_[index_]; }
    size_t size() const { return states_.size(); }

private:
    std::deque<EditParams> states_;
    size_t index_ = 0;
    size_t cap_;
};

}  // namespace re
