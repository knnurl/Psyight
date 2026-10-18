// An undo history of whole states (snapshots), for data small enough to copy, such as the
// project. Changes closer together than the merge window (a knob drag, a stream of MIDI moves)
// make one step; an undo or redo always starts a new one.
#pragma once

#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace psyight {

template <typename T> class UndoHistory {
  public:
    explicit UndoHistory(size_t max_steps = 100, double merge_window_s = 0.5)
        : max_steps_(max_steps), merge_window_s_(merge_window_s) {}

    // Before a change at now_s: `before` is the state it changes.
    void record(const T &before, double now_s) {
        redo_.clear();
        const bool merge = !undo_.empty() && now_s - last_s_ < merge_window_s_;
        last_s_ = now_s;
        if (merge) {
            return; // The step already holds the state before this run of changes.
        }
        undo_.push_back(before);
        if (undo_.size() > max_steps_) {
            undo_.erase(undo_.begin());
        }
    }

    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }

    // current becomes the previous (or next) state; false when there is none.
    bool undo(T &current) { return step(undo_, redo_, current); }
    bool redo(T &current) { return step(redo_, undo_, current); }

    void clear() {
        undo_.clear();
        redo_.clear();
        last_s_ = kNever;
    }

  private:
    static constexpr double kNever = -std::numeric_limits<double>::infinity();

    bool step(std::vector<T> &from, std::vector<T> &to, T &current) {
        if (from.empty()) {
            return false;
        }
        to.push_back(std::move(current));
        current = std::move(from.back());
        from.pop_back();
        last_s_ = kNever; // The next change is a new step.
        return true;
    }

    size_t max_steps_;
    double merge_window_s_;
    std::vector<T> undo_;
    std::vector<T> redo_;
    double last_s_ = kNever;
};

} // namespace psyight
