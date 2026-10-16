// Restraint and safety limiters (SPEC section 7.7), applied to every frame after the scene and
// before output, in this order: brightness cap, power budget, flash limiter (last, so it judges
// the frame as shown; it only dims, so the budget still holds). Then gamma to 8 bits.
//
// Flash limiter (on by default). Flashes are tracked per pixel as pairs of opposing changes
// (Ofcom-style, [Verify] the exact guideline wording): a pixel rises when its luminance gets
// flash_delta above its trough, and can rise again only after falling flash_delta below its
// peak, however many frames either change takes. A flash starts when at least flash_area of the
// pixels are rising or rose within the last 0.1 s and are still up, and lasts while that holds
// (a chase stepping a fifth of the rig along is not a flash): a kick ring
// sweeping the rig over four frames is one flash, a 10 Hz strobe is ten a second, and a strobe
// ramped over several frames is still caught (a per-frame rise test would miss it).
// At most max_flashes_per_s flashes start in any 1 s window. While a flash over the limit lasts,
// its rising pixels are held just under flash_delta above their trough; it is let through once
// the window has room. This is a safeguard, not a certification (SPEC 7.7).
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
    double flash_delta = 0.1; // Luminance change, 0 to 1.
    double flash_area = 0.25; // Share of pixels that must rise for a flash.
    double current_per_channel_ma = 20.0;
    double budget_a = 10.0; // 0 disables the power budget.
    double gamma = 2.2;
};

// Rec. 709 luminance of linear RGB.
double luminance(const Rgb &c);

// Estimated supply current of a frame in amperes.
double estimated_current_a(const Frame &frame, double current_per_channel_ma);

// The per-pixel flash tracking shared by the limiter and the meter.
class FlashTracker {
  public:
    explicit FlashTracker(const LimiterParams &params = {}) : p_(params) {}
    void set_params(const LimiterParams &params) { p_ = params; }
    void reset();

    // Finds this frame's rises and whether a flash starts. With `limit`, the rising pixels of a
    // flash over the limit are held just under flash_delta above their trough (the frame is
    // changed). Returns whether it held any. t_s ascending.
    bool detect(Frame &frame, double t_s, bool limit);
    // Records the frame as finally shown (after any later scaling), for the next frame.
    void commit(const Frame &frame);

    bool flash_started() const { return started_; } // At the last detect().
    int flashes() const { return flashes_; }         // All that started.
    int max_in_any_second() const { return max_in_any_second_; }

  private:
    LimiterParams p_;
    std::vector<char> armed_;   // May rise (else waiting to fall).
    std::vector<double> ref_;   // Armed: trough since arming. Disarmed: peak since rising.
    std::vector<double> rose_s_; // When the pixel last rose.
    std::vector<char> candidate_; // Rising at this frame.
    std::deque<double> allowed_starts_s_; // Flashes let through, within the last second.
    std::deque<double> all_starts_s_;     // Every flash, within the last second.
    bool in_flash_ = false;
    bool allowed_ = false;
    bool started_ = false;
    int flashes_ = 0;
    int max_in_any_second_ = 0;
};

struct LimiterReport {
    bool flash_limited = false; // Rising pixels were held this frame.
    double power_scale = 1.0;   // Applied by the power budget.
    double current_a = 0.0;     // After limiting.
};

class Limiters {
  public:
    explicit Limiters(const LimiterParams &params = {}) : p_(params), flashes_(params) {}
    void reset();
    const LimiterParams &params() const { return p_; }
    void set_params(const LimiterParams &params) {
        p_ = params;
        flashes_.set_params(params);
    }

    // Limits the frame in place for render time t_s (ascending between calls).
    LimiterReport process(Frame &frame, double t_s);

  private:
    LimiterParams p_;
    FlashTracker flashes_;
};

// Counts flashes as the flash limiter defines them, and the most in any 1 s window, for checks
// of whole shows (psyight_show_check) and tests.
class FlashMeter {
  public:
    explicit FlashMeter(const LimiterParams &params = {}) : tracker_(params) {}
    // Whether a flash starts at this frame; t_s ascending.
    bool add(const Frame &frame, double t_s);
    int flashes() const { return tracker_.flashes(); }
    int max_in_any_second() const { return tracker_.max_in_any_second(); }

  private:
    FlashTracker tracker_;
};

// Gamma-corrected 8-bit RGB triples, pixel by pixel.
std::vector<std::uint8_t> to_bytes(const Frame &frame, double gamma);

} // namespace psyight
