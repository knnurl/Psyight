// The show: engine render, limiters, patch and output at a fixed rate (SPEC sections 4.2 and 8).
// ShowRenderer does one frame for a render time: scene, limiters, gamma to bytes, patch, packets
// to the virtual node and (when sending) the network. It is deterministic for the same config and
// render-time sequence, and is what the tests drive.
// ShowThread paces ShowRenderer at rate_hz from the wall clock, but the render time always comes
// from the audio sample position (SPEC 4.1); the wall clock only paces frames and measures them.
// v0 runs the engine and output stages on one thread (SPEC 4.2 lists two); split them if a
// measurement shows the network stalling renders.
// The GUI publishes a new ShowConfig when something changes and reads the latest ShowFrame; both
// are immutable snapshots swapped atomically, so neither side blocks the other.
#pragma once

#include "analysis/track_analysis.h"
#include "audio/live_input.h"
#include "core/timebase.h"
#include "engine/automation.h"
#include "engine/limiters.h"
#include "engine/rig.h"
#include "engine/scene.h"
#include "output/patch.h"
#include "output/recorder.h"
#include "output/sender.h"
#include "output/test_patterns.h"
#include "output/udp_socket.h"
#include "output/virtual_node.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace psyight {

struct ShowConfig {
    std::uint64_t version = 0; // Bump on every change, so the renderer picks it up.
    std::shared_ptr<const TrackAnalysis> analysis; // Null: frames stay black.
    // Live mode (SPEC 4.3): when set, the engine reads the live analysis's latest state at the
    // render time instead of `analysis`, and the position source should be the live clock.
    std::shared_ptr<const LiveInput> live;
    Grid grid;
    int sample_rate_hz = 48000;
    double latency_offset_s = 0.0; // Render time minus playhead (SPEC 4.3).
    Rig rig = default_rig();
    std::vector<Scene> scenes = default_scenes();
    AutomationParams automation;
    int manual_scene = -1; // Overrides the section automation while >= 0.
    std::vector<Palette> palettes = default_palettes();
    LimiterParams limiter;
    std::vector<float> pixel_caps; // Per rig pixel, from the group caps; empty for none.
    Patch patch = build_patch(default_rig());
    OutputSettings output;
    // The local interface the output leaves on (its address); all zero: the system's choice. Set
    // from discovery: the interface whose subnet holds a node.
    std::array<std::uint8_t, 4> output_interface{};
    bool send_to_network = false;
    // Safety controls (SPEC 9.1): blackout sends black (through the limiters, so the way back up
    // counts as a flash); hold keeps sending the last frame shown. Blackout wins over hold.
    bool blackout = false;
    bool hold = false;
    std::uint64_t seed = 1;
    std::array<double, kMacroCount> macros{}; // The macro knobs, 0 to 1 (modulation sources).
    // A test pattern (preflight) replaces the scenes while on; with test.send it goes to the
    // network even when send_to_network is off.
    TestPattern test;
    // Sections edited by hand (the labels), in place of the analysis's; null: the detection.
    std::shared_ptr<const SectionTimeline> sections;
};

struct ShowFrame {
    std::int64_t index = 0;
    double t_s = 0.0;
    Frame frame; // Linear, after the limiters.
    LimiterReport report;
    std::int64_t flashes_limited = 0; // Frames the flash limiter clipped so far.
    std::string section;
    SceneMix mix;
    // The playing scene's (mix.to) route values this frame, held and smoothed, for the GUI's
    // modulation rings; empty during blackout and hold.
    std::vector<double> route_values;
    double test_clock_s = 0.0; // The test pattern's clock (preflight shows the colour step).
    // A manual scene waiting for its launch (-1: the return to the section rules), or
    // SceneLauncher::kNoneQueued.
    int queued_scene = SceneLauncher::kNoneQueued;
    int beat_in_bar = 0;
    std::vector<std::uint8_t> node_rgb; // What the virtual node latched (gamma-encoded bytes).
    VirtualNodeStats node;
    int packets_sent = 0; // This frame.
    std::int64_t send_errors = 0;
    std::string network_error;    // The last one, if any.
    double render_ms = 0.0;       // Measured: this frame's render, limit, patch and send.
    std::int64_t late_frames = 0; // Ticks that started more than a frame late.
};

class ShowRenderer {
  public:
    ShowFrame render(const ShowConfig &config, double t_s);

  private:
    class Tee;

    std::uint64_t config_version_ = ~std::uint64_t{0};
    Limiters limiters_;
    ModMemory modulation_;   // The routes' memory (holding, smoothing); cleared on a seek.
    SceneLauncher launcher_; // When a scene chosen by hand takes over.
    // The test pattern's clock: frames since it started (it runs while the transport is stopped).
    TestPatternKind test_kind_ = TestPatternKind::off;
    std::int64_t test_frames_ = 0;
    OutputSender sender_;
    VirtualNode node_;
    std::unique_ptr<UdpSocket> socket_;
    std::array<std::uint8_t, 4> socket_interface_{}; // What socket_ is bound to.
    double last_t_s_ = 0.0;
    std::int64_t index_ = 0;
    std::int64_t send_errors_ = 0;
    std::int64_t flashes_limited_ = 0;
    ShowFrame held_; // The last frame shown, for hold.
    std::string network_error_;
};

class ShowThread {
  public:
    using PositionSource = std::function<SampleIndex()>; // Must be safe to call from any thread.

    explicit ShowThread(PositionSource position, double rate_hz = 60.0);
    ~ShowThread();
    ShowThread(const ShowThread &) = delete;
    ShowThread &operator=(const ShowThread &) = delete;

    void set_config(std::shared_ptr<const ShowConfig> config) { config_.store(std::move(config)); }
    std::shared_ptr<const ShowFrame> latest() const { return frame_.load(); }
    // Frames go to the recorder too while one is set (null stops).
    void set_recorder(std::shared_ptr<Recorder> recorder) { recorder_.store(std::move(recorder)); }
    double rate_hz() const { return rate_hz_; }

  private:
    void run();

    PositionSource position_;
    double rate_hz_;
    std::atomic<std::shared_ptr<const ShowConfig>> config_;
    std::atomic<std::shared_ptr<const ShowFrame>> frame_;
    std::atomic<std::shared_ptr<Recorder>> recorder_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

} // namespace psyight
