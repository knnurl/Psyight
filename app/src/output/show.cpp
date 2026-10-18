#include "output/show.h"

#include "engine/inputs.h"
#include "output/precise_sleep.h"

#include <chrono>
#include <cmath>

namespace psyight {

namespace {

constexpr double kSeekResetS = 0.5; // A jump this large restarts the limiters' history.
constexpr double kTestPatternFrameRateHz = 60.0; // The show thread's rate: the pattern's clock.

} // namespace

// Sends each packet to the virtual node and, when open, the network.
class ShowRenderer::Tee final : public PacketSink {
  public:
    Tee(VirtualNode &node, UdpSocket *socket) : node_(node), socket_(socket) {}
    void send(std::span<const std::uint8_t> packet, const Ipv4Endpoint &to) override {
        node_.send(packet, to);
        if (socket_ == nullptr) {
            return;
        }
        if (Result<void> r = socket_->send_to(packet, to); !r) {
            ++errors;
            error = r.error().message;
        }
    }
    int errors = 0;
    std::string error;

  private:
    VirtualNode &node_;
    UdpSocket *socket_;
};

ShowFrame ShowRenderer::render(const ShowConfig &config, double t_s) {
    const auto started = std::chrono::steady_clock::now();
    if (config.version != config_version_) {
        limiters_.set_params(config.limiter);
        limiters_.set_pixel_caps(config.pixel_caps);
        node_.set_patch(config.patch);
        config_version_ = config.version;
    }
    if (std::abs(t_s - last_t_s_) > kSeekResetS) {
        limiters_.reset();
        modulation_.clear();
    }
    last_t_s_ = t_s;
    const bool testing = config.test.kind != TestPatternKind::off;
    if (config.test.kind != test_kind_) {
        test_kind_ = config.test.kind;
        test_frames_ = 0;
    }
    const bool network = config.send_to_network || (testing && config.test.send);
    if (network && socket_ == nullptr) {
        Result<std::unique_ptr<UdpSocket>> opened = open_udp_socket();
        if (opened) {
            socket_ = std::move(opened.value());
            network_error_.clear();
        } else {
            network_error_ = opened.error().message;
        }
    } else if (!network) {
        socket_.reset();
    }

    ShowFrame out;
    if (testing) {
        out.section = "test";
        out.test_clock_s = static_cast<double>(test_frames_++) / kTestPatternFrameRateHz;
        out.frame = config.blackout ? Frame(config.rig.pixels.size())
                                    : render_test_pattern(config.test, config.rig, config.patch,
                                                          out.test_clock_s);
    } else if (config.hold && !config.blackout && held_.frame.size() == config.rig.pixels.size()) {
        out = held_; // The look, frozen; the limiters and automation are skipped.
    } else if (config.analysis != nullptr || config.live != nullptr) {
        EngineInputs inputs;
        if (config.live != nullptr) {
            const LiveState state = config.live->state();
            inputs = inputs_at(state, t_s);
            out.mix =
                automate_live(config.scenes, state, inputs, config.automation, config.manual_scene);
        } else {
            inputs = inputs_at(*config.analysis, config.grid, t_s);
            out.mix = automate(config.scenes, *config.analysis, config.grid, inputs,
                               config.automation, config.manual_scene);
        }
        inputs.macros = config.macros;
        out.section = inputs.section;
        out.beat_in_bar = inputs.beat_in_bar;
        if (config.blackout) {
            out.frame.assign(config.rig.pixels.size(), Rgb{});
        } else {
            out.frame = render_mix(config.scenes, out.mix, config.rig, inputs, config.palettes,
                                   config.seed, &modulation_);
            if (out.mix.to >= 0 && static_cast<size_t>(out.mix.to) < modulation_.size()) {
                for (const ModRouteState &r : modulation_[static_cast<size_t>(out.mix.to)]) {
                    out.route_values.push_back(r.value);
                }
            }
        }
    } else {
        out.frame.assign(config.rig.pixels.size(), Rgb{});
    }
    if (testing || !config.hold || config.blackout ||
        held_.frame.size() != config.rig.pixels.size()) {
        out.report = limiters_.process(out.frame, t_s);
        flashes_limited_ += out.report.flash_limited;
        held_ = out;
    }
    out.index = index_++;
    out.t_s = t_s;
    out.flashes_limited = flashes_limited_;
    const std::vector<std::uint8_t> bytes = to_bytes(out.frame, config.limiter.gamma);
    Tee tee(node_, socket_.get());
    out.packets_sent = sender_.send_frame(config.output, patch_frame(config.patch, bytes), tee);
    send_errors_ += tee.errors;
    if (!tee.error.empty()) {
        network_error_ = tee.error;
    }
    out.node_rgb = node_.rgb();
    out.node = node_.stats();
    out.send_errors = send_errors_;
    out.network_error = network_error_;
    out.render_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
    return out;
}

ShowThread::ShowThread(PositionSource position, double rate_hz)
    : position_(std::move(position)), rate_hz_(rate_hz), thread_([this] { run(); }) {}

ShowThread::~ShowThread() {
    stop_ = true;
    thread_.join();
}

void ShowThread::run() {
    using Clock = std::chrono::steady_clock;
    const auto period =
        std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / rate_hz_));
    ShowRenderer renderer;
    std::int64_t late_frames = 0;
    auto next = Clock::now();
    while (!stop_) {
        const std::shared_ptr<const ShowConfig> config = config_.load();
        if (config != nullptr) {
            const double t_s =
                samples_to_seconds(position_(), config->sample_rate_hz) + config->latency_offset_s;
            auto frame = std::make_shared<ShowFrame>(renderer.render(*config, t_s));
            frame->late_frames = late_frames;
            if (const std::shared_ptr<Recorder> recorder = recorder_.load()) {
                auto record = std::make_shared<RecordedFrame>();
                record->t_s = frame->t_s;
                record->index = frame->index;
                record->config_version = config->version;
                record->flags = static_cast<std::uint8_t>((config->blackout ? kFrameBlackout : 0) |
                                                          (config->hold ? kFrameHold : 0));
                record->rgb = frame->node_rgb;
                recorder->push_frame(std::move(record));
            }
            frame_.store(std::move(frame));
        }
        next += period;
        const auto now = Clock::now();
        if (now > next + period) {
            ++late_frames; // Fell behind by more than a frame: skip ahead rather than burst.
            next = now;
        }
        sleep_until_precise(next);
    }
}

} // namespace psyight
