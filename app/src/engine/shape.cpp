#include "engine/shape.h"

#include "engine/space.h"

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

HitTrigger resolve_trigger(const EngineInputs &in, int trigger) {
    const double beat_s = in.beat_s > 0.0 ? in.beat_s : kReferenceBeatS;
    if (trigger == 5) { // Kick, else beat.
        const bool groove = in.section == "groove" || in.section == "drop";
        const bool missed = !std::isfinite(in.since_kick_s) || in.since_kick_s > 1.25 * in.beat_s;
        trigger = groove && in.beat_s > 0.0 && missed ? 1 : 0;
    }
    HitTrigger t;
    switch (trigger) {
    case 0: // Kick: earlier kicks taken a beat apart.
        if (in.kicks_so_far > 0 && std::isfinite(in.since_kick_s)) {
            t = {in.kicks_so_far - 1, in.since_kick_s, beat_s};
        }
        break;
    case 1: // Beat.
        t = {in.beat_index, in.beat_phase * beat_s, beat_s};
        break;
    case 2: // Bar.
        t = {in.bar_index, in.bar_phase * 4.0 * beat_s, 4.0 * beat_s};
        break;
    case 3: // Bass note.
        if (in.bass_notes_so_far > 0 && std::isfinite(in.since_bass_onset_s)) {
            t = {in.bass_notes_so_far - 1, in.since_bass_onset_s, 0.0, &in.bass_onset_ages_s};
        }
        break;
    default: // Hat.
        if (in.hats_so_far > 0 && std::isfinite(in.since_hat_s)) {
            t = {in.hats_so_far - 1, in.since_hat_s, 0.0, &in.hat_ages_s};
        }
        break;
    }
    return t;
}

double note_level(const EngineInputs &in, double release_s) {
    if (in.bass_notes_so_far <= 0 || !std::isfinite(in.since_bass_onset_s)) {
        return 0.0;
    }
    const double tau_s = in.bass_decay_tau_ms > 0.0 ? in.bass_decay_tau_ms * 1e-3 : 0.2;
    double level = std::max(0.2, in.bass_velocity) * std::exp(-in.since_bass_onset_s / tau_s);
    if (!in.bass_active) {
        level *= std::exp(-in.since_bass_offset_s / std::max(1e-3, release_s));
    }
    return level;
}

double fbm(double x, double y, double z, int octaves, std::uint64_t seed) {
    double sum = 0.0;
    double weight = 0.0;
    double amplitude = 1.0;
    double scale = 1.0;
    for (int k = 0; k < std::clamp(octaves, 1, 6); ++k) {
        sum += amplitude * value_noise(x * scale, y * scale, z * scale, seed + k);
        weight += amplitude;
        amplitude *= 0.5;
        scale *= 2.0;
    }
    return sum / weight;
}

double turn_distance(double a, double b) {
    const double d = std::fabs(a - b);
    const double f = d - std::floor(d);
    return std::min(f, 1.0 - f);
}

} // namespace psyight
