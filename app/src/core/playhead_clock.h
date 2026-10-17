// A smooth playhead from jittery measurements (a first-order delay-locked loop).
//
// The transport reports its position once per audio callback, and callbacks arrive irregularly
// (10 ms periods, sometimes two at once), so a 60 fps render reading it directly steps 10 to
// 30 ms a frame. This clock advances by wall time times the rate and moves `gain` of the way
// toward each measurement, which low-pass filters the measurement jitter while following the
// true rate; it jumps to the measurement on a seek or after a pause (error beyond `snap`) and
// never runs backwards otherwise. Wall time only paces the estimate: the musical clock is still
// the audio sample position it locks to (SPEC 4.1). Deterministic for the same inputs.
#pragma once

#include "core/timebase.h"

#include <algorithm>
#include <cstdint>

namespace psyight {

class PlayheadClock {
  public:
    PlayheadClock(double gain = 0.1, double snap_samples = 4800.0)
        : gain_(gain), snap_(snap_samples) {}

    // now_s: monotonic wall time; measured: the transport's position; rate: samples per second
    // of playback (0 when paused). Returns the smoothed position.
    SampleIndex update(double now_s, SampleIndex measured, double rate) {
        const auto m = static_cast<double>(measured);
        if (!started_ || rate <= 0.0) {
            started_ = rate > 0.0;
            estimate_ = m;
            last_s_ = now_s;
            return measured;
        }
        const double predicted = estimate_ + (now_s - last_s_) * rate;
        last_s_ = now_s;
        const double error = m - predicted;
        if (error > snap_ || error < -snap_) {
            estimate_ = m; // A seek: follow at once.
        } else {
            estimate_ = std::max(estimate_, predicted + gain_ * error); // Never backwards.
        }
        return static_cast<SampleIndex>(estimate_ + 0.5);
    }

  private:
    double gain_;
    double snap_;
    bool started_ = false;
    double estimate_ = 0.0;
    double last_s_ = 0.0;
};

} // namespace psyight
