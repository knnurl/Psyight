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
#include "core/timebase.h"
#include "engine/limiters.h"
#include "engine/rig.h"
#include "engine/scene.h"
#include "output/patch.h"
#include "output/sender.h"
#include "output/udp_socket.h"
#include "output/virtual_node.h"

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
    Grid grid;
    int sample_rate_hz = 48000;
    double latency_offset_s = 0.0; // Render time minus playhead (SPEC 4.3).
    Rig rig = default_rig();
    Scene scene = default_scene();
    std::vector<Palette> palettes = default_palettes();
    LimiterParams limiter;
    Patch patch = build_patch(default_rig());
    OutputSettings output;
    bool send_to_network = false;
    std::uint64_t seed = 1;
};

struct ShowFrame {
    std::int64_t index = 0;
    double t_s = 0.0;
    Frame frame; // Linear, after the limiters.
    LimiterReport report;
    std::int64_t flashes_limited = 0; // Frames the flash limiter clipped so far.
    std::string section;
    int beat_in_bar = 0;
    std::vector<std::uint8_t> node_rgb; // What the virtual node latched (gamma-encoded bytes).
    VirtualNodeStats node;
    int packets_sent = 0;      // This frame.
    std::int64_t send_errors = 0;
    std::string network_error; // The last one, if any.
    double render_ms = 0.0;    // Measured: this frame's render, limit, patch and send.
    std::int64_t late_frames = 0; // Ticks that started more than a frame late.
};

class ShowRenderer {
  public:
    ShowFrame render(const ShowConfig &config, double t_s);

  private:
    class Tee;

    std::uint64_t config_version_ = ~std::uint64_t{0};
    Limiters limiters_;
    OutputSender sender_;
    VirtualNode node_;
    std::unique_ptr<UdpSocket> socket_;
    double last_t_s_ = 0.0;
    std::int64_t index_ = 0;
    std::int64_t send_errors_ = 0;
    std::int64_t flashes_limited_ = 0;
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
    double rate_hz() const { return rate_hz_; }

  private:
    void run();

    PositionSource position_;
    double rate_hz_;
    std::atomic<std::shared_ptr<const ShowConfig>> config_;
    std::atomic<std::shared_ptr<const ShowFrame>> frame_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

} // namespace psyight
