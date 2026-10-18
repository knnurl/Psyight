// The snapshot undo history (core/undo_history.h).

#include "core/undo_history.h"

#include <doctest/doctest.h>

#include <string>

using namespace psyight;

TEST_CASE("undo history: undo and redo whole states") {
    UndoHistory<std::string> history;
    std::string state = "a";
    history.record(state, 0.0);
    state = "b";
    history.record(state, 1.0);
    state = "c";
    CHECK(history.undo(state));
    CHECK(state == "b");
    CHECK(history.undo(state));
    CHECK(state == "a");
    CHECK_FALSE(history.undo(state));
    CHECK(history.redo(state));
    CHECK(state == "b");
    CHECK(history.redo(state));
    CHECK(state == "c");
    CHECK_FALSE(history.redo(state));
    // A new change after an undo drops the redo steps.
    history.undo(state);
    history.record(state, 5.0);
    state = "d";
    CHECK_FALSE(history.can_redo());
    CHECK(history.undo(state));
    CHECK(state == "b");
}

TEST_CASE("undo history: a quick run of changes is one step") {
    UndoHistory<int> history(100, 0.5);
    int value = 0;
    for (int frame = 0; frame < 60; ++frame) { // A one-second drag, 60 changes.
        history.record(value, frame / 60.0);
        value = frame + 1;
    }
    history.record(value, 3.0); // A separate change later.
    value = 100;
    CHECK(history.undo(value));
    CHECK(value == 60);
    CHECK(history.undo(value));
    CHECK(value == 0); // The whole drag.
    CHECK_FALSE(history.can_undo());
}

TEST_CASE("undo history: at most max_steps, oldest dropped") {
    UndoHistory<int> history(3, 0.0);
    int value = 0;
    for (int i = 0; i < 10; ++i) {
        history.record(value, i);
        value = i + 1;
    }
    int undos = 0;
    while (history.undo(value)) {
        ++undos;
    }
    CHECK(undos == 3);
    CHECK(value == 7);
    history.clear();
    CHECK_FALSE(history.can_undo());
    CHECK_FALSE(history.can_redo());
}
