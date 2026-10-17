#include "audio/live_input.h"

#include "output/precise_sleep.h"

#include <algorithm>
#include <array>
#include <chrono>

namespace psyight {

namespace {

constexpr auto kWake = std::chrono::milliseconds(2);
constexpr double kSmoothing = 0.05; // Of each new measurement, for the published averages.
constexpr auto kGap = std::chrono::milliseconds(50); // No samples for this long: fill silence.

} // namespace

LiveInput::LiveInput(std::shared_ptr<const StudentModel> model) : model_(std::move(model)) {}

LiveInput::~LiveInput() { stop(); }

Result<void> LiveInput::start(const CaptureOptions &capture, const LiveParams &params) {
    stop();
    if (model_ == nullptr) {
        return Error{"live mode needs the M4 student's weights (data/m4/student.weights)"};
    }
    if (Result<void> r = analyser_.prepare(capture.sample_rate_hz, *model_, params); !r) {
        return r;
    }
    state_.store(analyser_.state());
    delay_ms_.store(0.0);
    load_.store(0.0);
    filled_.store(0);
    if (Result<void> r = capture_.open(capture); !r) {
        return r;
    }
    stop_ = false;
    thread_ = std::thread([this] { run(); });
    return {};
}

void LiveInput::stop() {
    stop_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
    capture_.close();
}

void LiveInput::run() {
    std::array<float, 4096> block{};
    long long dropped_seen = 0;
    double delay_ms = 0.0;
    double load = 0.0;
    auto wake = std::chrono::steady_clock::now();
    auto last_data = wake;
    SampleIndex gap_filled = 0; // Silence analysed since the last samples arrived.
    while (!stop_) {
        wake += kWake;
        sleep_until_precise(wake);
        const auto started = std::chrono::steady_clock::now();
        if (started - wake > 10 * kWake) {
            wake = started; // Fell behind (a stall): do not try to catch up the ticks.
        }
        bool commanded = false;
        for (LiveCommand c; commands_.try_pop(c);) {
            analyser_.apply(c);
            commanded = true;
        }
        if (commanded) {
            state_.store(analyser_.state());
        }
        // Dropped samples become silence, so the analysis keeps the capture clock.
        const long long dropped = capture_.dropped();
        for (long long missing = dropped - dropped_seen; missing > 0;) {
            const auto n = static_cast<size_t>(std::min<long long>(missing, block.size()));
            std::fill(block.begin(), block.begin() + static_cast<std::ptrdiff_t>(n), 0.0f);
            analyser_.process(std::span(block).first(n));
            missing -= static_cast<long long>(n);
        }
        dropped_seen = dropped;
        bool any = false;
        for (size_t n = capture_.read(block); n > 0; n = capture_.read(block)) {
            analyser_.process(std::span(block).first(n));
            any = true;
        }
        if (any) {
            last_data = started;
            gap_filled = 0;
        } else if (started - last_data > kGap) {
            const auto due = static_cast<SampleIndex>(
                std::chrono::duration<double>(started - last_data).count() *
                capture_.sample_rate_hz());
            for (SampleIndex missing = due - gap_filled; missing > 0;) {
                const auto n = static_cast<size_t>(std::min<SampleIndex>(missing, block.size()));
                std::fill(block.begin(), block.begin() + static_cast<std::ptrdiff_t>(n), 0.0f);
                analyser_.process(std::span(block).first(n));
                missing -= static_cast<SampleIndex>(n);
                gap_filled += static_cast<SampleIndex>(n);
                filled_.fetch_add(static_cast<SampleIndex>(n), std::memory_order_acq_rel);
            }
        } else {
            continue;
        }
        const LiveState &s = analyser_.state();
        state_.store(s);
        const double now_s = static_cast<double>(now()) / capture_.sample_rate_hz();
        delay_ms += kSmoothing * (1e3 * (now_s - s.analysed_s) - delay_ms);
        delay_ms_.store(delay_ms, std::memory_order_release);
        const double busy_s =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        load += kSmoothing * (busy_s / std::chrono::duration<double>(kWake).count() - load);
        load_.store(load, std::memory_order_release);
    }
}

} // namespace psyight
