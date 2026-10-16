#include "gui/tier.h"

namespace psyight {

namespace {

Tier g_tier = Tier::tune;

} // namespace

Tier current_tier() { return g_tier; }

void set_current_tier(Tier tier) { g_tier = tier; }

const char *tier_name(Tier tier) {
    switch (tier) {
    case Tier::operate:
        return "Operate";
    case Tier::tune:
        return "Tune";
    case Tier::engineer:
        return "Engineer";
    }
    return "Tune";
}

} // namespace psyight
