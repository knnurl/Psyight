// Shared helpers for effects: sizes relative to the rig, times relative to the beat, and hit
// envelopes (docs/PLAN_HEX_VISUALS.md section 4.1). Pure functions.
//
// Sizes: an effect with size_units 1 ("Rig") takes its metre settings per metre of the rig's
// reach (the largest distance of a pixel from the bounding box's centre), so a burst 0.25 m wide
// on a 1 m rig is 0.05 m on the 0.2 m hexagon and 0.75 m on a 3 m stage; speeds scale alike.
// Times: an effect with time_units 1 ("Beats") takes its second settings per beat at 150 BPM
// (0.4 s), so they are unchanged at 150 BPM and shorter at a faster tempo.
#pragma once

#include "engine/inputs.h"
#include "engine/rig.h"

namespace psyight {

inline constexpr double kReferenceReachM = 1.0; // Sizes in "rig" units are per metre of reach.
inline constexpr double kReferenceBeatS = 0.4;  // 150 BPM: beats-mode times equal seconds there.
// Option names of the units settings (EffectParamInfo::options).
inline constexpr const char *kSizeUnitsOptions = "Metres|Rig";
inline constexpr const char *kTimeUnitsOptions = "Seconds|Beats";

// The rig's reach: the largest distance of a pixel from its bounding box's centre. At least 1e-3.
double rig_reach_m(const Rig &rig);

// value_m in metres: as it is (0, "Metres"), or per metre of the rig's reach (1, "Rig").
double size_in_m(double value_m, int size_units, const Rig &rig);
// value_s in seconds: as it is (0, "Seconds"), or scaled by the beat against 150 BPM's
// (1, "Beats"; the reference beat without a tempo).
double time_in_s(double value_s, int time_units, const EngineInputs &in);

// A hit's envelope: 1 for hold_s, then exp(-(age - hold_s) / decay_s); 0 before the hit.
double hit_envelope(double age_s, double hold_s, double decay_s);

// easeOutBack on 0..1 (1 beyond): 1 + (s + 1)(x - 1)^3 + s (x - 1)^2, s = 17.0158 * overshoot.
// overshoot 0.1 passes 1 by about 10%; 0 is a cubic ease-out.
double overshoot_ease(double x, double overshoot);

} // namespace psyight
