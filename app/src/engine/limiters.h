// Restraint and safety limiters (SPEC section 7.7), applied to every frame after the scene and
// before output, in this order: brightness cap, flash limiter, power budget. Then gamma to 8 bits.
//
// Flash limiter (on by default): a flash is a frame in which at least flash_area of the pixels
// rise in luminance by flash_delta or more (Ofcom-style: about 10% of full scale over a quarter
// of the field). At most max_flashes_per_s are let through in any 1 s window; further rises are
// clipped to just under flash_delta. This is a safeguard, not a certification (SPEC 7.7).
// Power budget: the current estimate is current_per_channel_ma at full per colour channel (SPEC 18
// A5: about 20 mA for WS2812B), summed; above budget_a the whole frame is scaled down.
// Stateful (the flash history), deterministic for the same frame sequence and times.
#pragma once

#include "engine/colour.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace psyight {

struct LimiterParams {
    double brightness_cap = 1.0; // Global, 0 to 1.
    bool flash_limiter = true;   // Disabling is an Engineer-tier action (SPEC 7.7).
    double max_flashes_per_s = 3.0;
    double flash_delta = 0.1; // Luminance rise, 0 to 1.
    double flash_area = 0.25; // Share of pixels that must rise for a flash.
    double current_per_channel_ma = 20.0;
    double budget_a = 10.0; // 0 disables the power budget.
    double gamma = 2.2;
};

// Rec. 709 luminance of linear RGB.
double luminance(const Rgb &c);

// Estimated supply current of a frame in amperes.
double estimated_current_a(const Frame &frame, double current_per_channel_ma);

struct LimiterReport {
    bool flash_limited = false; // This frame's rise was clipped.
    double power_scale = 1.0;   // Applied by the power budget.
    double current_a = 0.0;     // After limiting.
};

class Limiters {
  public:
    explicit Limiters(const LimiterParams &params = {}) : p_(params) {}
    void reset();
    const LimiterParams &params() const { return p_; }
    void set_params(const LimiterParams &params) { p_ = params; }

    // Limits the frame in place for render time t_s (ascending between calls).
    LimiterReport process(Frame &frame, double t_s);

  private:
    LimiterParams p_;
    std::vector<double> previous_luminance_;
    std::deque<double> flash_times_s_;
};

// Gamma-corrected 8-bit RGB triples, pixel by pixel.
std::vector<std::uint8_t> to_bytes(const Frame &frame, double gamma);

} // namespace psyight
