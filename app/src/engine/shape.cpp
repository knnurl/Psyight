#include "engine/shape.h"

#include <algorithm>
#include <cmath>

namespace psyight {

double rig_reach_m(const Rig &rig) {
    const double cx = 0.5 * (rig.min.x + rig.max.x);
    const double cy = 0.5 * (rig.min.y + rig.max.y);
    const double cz = 0.5 * (rig.min.z + rig.max.z);
    double most = 0.0;
    for (const Pixel &p : rig.pixels) {
        most = std::max(most, std::hypot(p.position.x - cx, p.position.y - cy, p.position.z - cz));
    }
    return std::max(1e-3, most);
}

double size_in_m(double value_m, int size_units, const Rig &rig) {
    return size_units == 1 ? value_m * rig_reach_m(rig) / kReferenceReachM : value_m;
}

double time_in_s(double value_s, int time_units, const EngineInputs &in) {
    if (time_units != 1) {
        return value_s;
    }
    const double beat_s = in.beat_s > 0.0 ? in.beat_s : kReferenceBeatS;
    return value_s * beat_s / kReferenceBeatS;
}

double hit_envelope(double age_s, double hold_s, double decay_s) {
    if (age_s < 0.0) {
        return 0.0;
    }
    if (age_s <= hold_s) {
        return 1.0;
    }
    return std::exp(-(age_s - hold_s) / std::max(1e-6, decay_s));
}

double overshoot_ease(double x, double overshoot) {
    const double t = std::clamp(x, 0.0, 1.0) - 1.0;
    const double s = 17.0158 * overshoot;
    return 1.0 + (s + 1.0) * t * t * t + s * t * t;
}

} // namespace psyight
