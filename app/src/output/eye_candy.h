// Eye candy (the Preflight test pattern of that name): shows that check nothing, for showing the
// lights off. Written for Psyight; the WLED-like ones take only the idea of WLED's effects of
// the same kind (WLED's code is EUPL, which the app does not take in).
// - Hunt: a little game. A hunter (a bright comet) chases five prey (pulsing dots on the rig),
//   nearest first, each caught in a burst of its colour; a new round every 10 s.
// - Billiards: four balls bouncing round the rig's box with fading trails, flaring white where
//   two meet.
// - Bouncing balls: balls thrown up each strip, falling back under gravity (as WLED's).
// - Meteors: meteors running along each strip, their trails breaking up into sparks.
// - Fireworks: shells rising from the bottom and bursting into rings of rays that droop.
// - Ocean: layers of slow waves in deep blues and teals, white where they crest (as WLED's
//   Pacifica).
// - Lava lamp: warm blobs drifting, merging and parting (metaballs).
// - Spiral: a colour wheel turning about the centre, twisted, with rings breathing out.
// - Aurora: the Ambient step's aurora (engine/aurora.h).
// Pure: a function of the show, the rig and the clock (seconds since the pattern started).
#pragma once

#include "engine/colour.h"
#include "engine/rig.h"

namespace psyight {

inline constexpr int kEyeCandyCount = 9;

Frame render_eye_candy(int show, const Rig &rig, double clock_s, float level);
const char *eye_candy_name(int show);
const char *eye_candy_description(int show);

} // namespace psyight
