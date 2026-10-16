#include "output/show.h"

#include "engine/inputs.h"
#include "output/precise_sleep.h"

#include <chrono>
#include <cmath>

namespace psyight {

namespace {

constexpr double kSeekResetS = 0.5; // A jump this large restarts the limiters' history.

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
        node_.set_patch(config.patch);
        config_version_ = config.version;
    }
    if (std::abs(t_s - last_t_s_) > kSeekResetS) {
        limiters_.reset();
    }
    last_t_s_ = t_s;
    if (config.send_to_network && socket_ == nullptr) {
        Result<std::unique_ptr<UdpSocket>> opened = open_udp_socket();
        if (opened) {
            socket_ = std::move(opened.value());
            network_error_.clear();
        } else {
            network_error_ = opened.error().message;
        }
    } else if (!config.send_to_network) {
        socket_.reset();
    }

    ShowFrame out;
    out.index = index_++;
    out.t_s = t_s;
    if (config.analysis != nullptr) {
        const EngineInputs inputs = inputs_at(*config.analysis, config.grid, t_s);
        out.mix = automate(config.scenes, *config.analysis, config.grid, inputs, config.automation,
                           config.manual_scene);
        out.frame =
            render_mix(config.scenes, out.mix, config.rig, inputs, config.palettes, config.seed);
        out.section = inputs.section;
        out.beat_in_bar = inputs.beat_in_bar;
    } else {
        out.frame.assign(config.rig.pixels.size(), Rgb{});
    }
    out.report = limiters_.process(out.frame, t_s);
    flashes_limited_ += out.report.flash_limited;
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
