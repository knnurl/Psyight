// Depth of control (SPEC 9.1): Operate (scenes, master controls, safety), Tune (scene, layer,
// modulation, palette and output settings), Engineer (everything, including the flash limiter
// switch and the rig). A global switch in the top bar; panels show or enable controls at or
// below the current tier. GUI thread only.
#pragma once

namespace psyight {

enum class Tier { operate, tune, engineer };

Tier current_tier();
void set_current_tier(Tier tier);
inline bool tier_at_least(Tier tier) { return current_tier() >= tier; }
const char *tier_name(Tier tier);

} // namespace psyight
