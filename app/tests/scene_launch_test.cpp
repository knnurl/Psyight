// Scene launch quantisation (engine/automation.h SceneLauncher).

#include "engine/automation.h"
#include "io/project.h"

#include <doctest/doctest.h>

using namespace psyight;

namespace {

// 60 frames a second, 2 s bars, phrases of 8 bars.
EngineInputs at_frame(int frame) {
    EngineInputs in;
    in.t_s = frame / 60.0;
    in.bar_index = static_cast<long long>(in.t_s / 2.0);
    in.bar_phase = in.t_s / 2.0 - static_cast<double>(in.bar_index);
    in.phrase_index = static_cast<int>(in.bar_index / 8);
    return in;
}

} // namespace

TEST_CASE("scene launch: a scene waits for the next bar") {
    SceneLauncher launcher;
    CHECK(launcher.update(-1, at_frame(0), SceneLaunch::bar) == -1);
    CHECK(launcher.update(-1, at_frame(30), SceneLaunch::bar) == -1);
    // Requested halfway through bar 0: still the rules, queued, until bar 1 starts (frame 120).
    CHECK(launcher.update(2, at_frame(60), SceneLaunch::bar) == -1);
    CHECK(launcher.queued() == 2);
    CHECK(launcher.update(2, at_frame(119), SceneLaunch::bar) == -1);
    CHECK(launcher.update(2, at_frame(120), SceneLaunch::bar) == 2);
    CHECK(launcher.queued() == SceneLauncher::kNoneQueued);
    // The return to the rules waits too.
    CHECK(launcher.update(-1, at_frame(130), SceneLaunch::bar) == 2);
    CHECK(launcher.queued() == -1);
    CHECK(launcher.update(-1, at_frame(240), SceneLaunch::bar) == -1);
}

TEST_CASE("scene launch: next phrase, now, and a stopped transport") {
    SceneLauncher phrase;
    phrase.update(-1, at_frame(0), SceneLaunch::phrase);
    CHECK(phrase.update(1, at_frame(1), SceneLaunch::phrase) == -1);
    CHECK(phrase.update(1, at_frame(8 * 120 - 1), SceneLaunch::phrase) == -1); // Bar 7.
    CHECK(phrase.update(1, at_frame(8 * 120), SceneLaunch::phrase) == 1);      // Phrase 1.

    SceneLauncher now;
    now.update(-1, at_frame(0), SceneLaunch::now);
    CHECK(now.update(3, at_frame(1), SceneLaunch::now) == 3);

    SceneLauncher stopped;
    stopped.update(-1, at_frame(10), SceneLaunch::bar);
    CHECK(stopped.update(4, at_frame(10), SceneLaunch::bar) == 4); // Time stands still.

    // A different request while one waits restarts the wait; after a reset, the next takes over.
    SceneLauncher changed;
    changed.update(-1, at_frame(0), SceneLaunch::bar);
    changed.update(1, at_frame(10), SceneLaunch::bar);
    CHECK(changed.update(2, at_frame(20), SceneLaunch::bar) == -1);
    CHECK(changed.queued() == 2);
    changed.reset();
    CHECK(changed.update(2, at_frame(30), SceneLaunch::bar) == 2);
}

TEST_CASE("scene launch: the choice round-trips in the project") {
    Project p = default_project();
    p.automation.launch = SceneLaunch::phrase;
    const Result<Project> back = parse_project_json(to_project_json(p).value());
    REQUIRE(back.ok());
    CHECK(back.value().automation.launch == SceneLaunch::phrase);
    // Older projects without it get the default (the next bar).
    CHECK(AutomationParams{}.launch == SceneLaunch::bar);
}
