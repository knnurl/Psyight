#include "audio/live_input.h"

#include "core/paths.h"
#include "output/precise_sleep.h"

#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <vector>

namespace psyight {

namespace {

constexpr auto kWake = std::chrono::milliseconds(2);
constexpr double kSmoothing = 0.05; // Of each new measurement, for the published averages.
constexpr auto kGap = std::chrono::milliseconds(50); // No samples for this long: fill silence.
constexpr auto kWriteEvery = std::chrono::milliseconds(100);
constexpr auto kClockEvery = std::chrono::seconds(1);

std::string utc_now() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_s(&tm, &t);
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    char text[40];
    std::snprintf(text, sizeof(text), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<int>(ms.count()));
    return text;
}

} // namespace

struct LiveInput::Recorder {
    ma_encoder encoder{};
    std::ofstream clock;
    SpscQueue<float> ring{1 << 21}; // 44 s at 48 kHz.
    std::vector<float> buffer = std::vector<float>(1 << 16);
};

LiveInput::LiveInput(std::shared_ptr<const StudentModel> model) : model_(std::move(model)) {}

LiveInput::~LiveInput() { stop(); }

Result<void> LiveInput::start(const CaptureOptions &capture, const LiveParams &params,
                              const std::filesystem::path &record) {
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
    recorded_.store(0);
    record_dropped_.store(0);
    record_path_.clear();
    if (!record.empty()) {
        std::error_code error;
        std::filesystem::create_directories(record.parent_path(), error);
        auto recorder = std::make_unique<Recorder>();
        const ma_encoder_config config =
            ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 1,
                                   static_cast<ma_uint32>(capture.sample_rate_hz));
        if (ma_encoder_init_file_w(record.c_str(), &config, &recorder->encoder) != MA_SUCCESS) {
            return Error{"cannot write the recording " + path_to_utf8(record)};
        }
        std::filesystem::path clock_path = record;
        clock_path.replace_extension(".clock.csv");
        recorder->clock.open(clock_path);
        recorder->clock << "live_clock_samples,steady_clock_ns,utc\n";
        recorder_ = std::move(recorder);
        record_path_ = record;
    }
    if (Result<void> r = capture_.open(capture); !r) {
        stop();
        return r;
    }
    stop_ = false;
    thread_ = std::thread([this] { run(); });
    if (recorder_ != nullptr) {
        record_stop_ = false;
        writer_ = std::thread([this] { write_recording(); });
    }
    return {};
}

void LiveInput::stop() {
    stop_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
    record_stop_ = true;
    if (writer_.joinable()) {
        writer_.join(); // Drains the ring; reads the capture clock, so before the close.
    }
    capture_.close();
    if (recorder_ != nullptr) {
        ma_encoder_uninit(&recorder_->encoder); // Writes the WAV's sizes.
        recorder_.reset();
    }
}

void LiveInput::analyse(std::span<const float> samples) {
    analyser_.process(samples);
    if (recorder_ == nullptr) {
        return;
    }
    long long lost = 0;
    for (const float s : samples) {
        lost += recorder_->ring.try_push(s) ? 0 : 1;
    }
    if (lost > 0) {
        record_dropped_.fetch_add(lost, std::memory_order_acq_rel);
    }
}

void LiveInput::write_recording() {
    Recorder &r = *recorder_;
    auto next_clock = std::chrono::steady_clock::now();
    for (bool last = false; !last;) {
        last = record_stop_.load(std::memory_order_acquire); // After the analysis thread ended.
        size_t n = 0;
        do {
            n = 0;
            while (n < r.buffer.size() && r.ring.try_pop(r.buffer[n])) {
                ++n;
            }
            ma_uint64 written = 0;
            ma_encoder_write_pcm_frames(&r.encoder, r.buffer.data(), n, &written);
            recorded_.fetch_add(static_cast<long long>(written), std::memory_order_acq_rel);
        } while (n == r.buffer.size());
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_clock || last) {
            next_clock = now + kClockEvery;
            r.clock << this->now() << ','
                    << std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch())
                           .count()
                    << ',' << utc_now() << '\n';
            r.clock.flush();
        }
        if (!last) {
            std::this_thread::sleep_for(kWriteEvery);
        }
    }
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
            analyse(std::span(block).first(n));
            missing -= static_cast<long long>(n);
        }
        dropped_seen = dropped;
        bool any = false;
        for (size_t n = capture_.read(block); n > 0; n = capture_.read(block)) {
            analyse(std::span(block).first(n));
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
                analyse(std::span(block).first(n));
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
