// The aurora: the Ambient step's look, after WLED's Aurora effect. Soft waves of green, teal,
// blue and violet drift slowly along each strip, each fading in and out over its own lifetime
// (8 to 25 s), brightest at its centre and fading linearly to its edges; overlapping waves add
// (scaled back to keep their hue, not clipped to white).
// A wave leaving one end of a strip comes back in at the other (in WLED they run off the end), so
// short strips stay lit and rings are seamless. Not musical: it runs on its own clock (seconds
// since it started, counted in show frames), so it moves while the transport is stopped, between
// sets or during talks. Pure: a function of the rig, the clock and the seed; every wave's
// parameters are hashed from (seed, strip, wave slot, generation), so no state is kept between
// frames.
#pragma once

#include "engine/colour.h"
#include "engine/rig.h"

#include <cstdint>

namespace psyight {

Frame render_aurora(const Rig &rig, double clock_s, std::uint64_t seed);

} // namespace psyight
