// Live mode's input side (SPEC 4.2, M5): capture (audio/capture.h) feeding the analysis thread,
// which runs the causal live analysis (analysis/live_analysis.h) and publishes its state through
// a seqlock. The engine and the GUI read the latest state and the capture clock; neither blocks
// the analysis.
//
// The analysis thread wakes every 2 ms, drains the capture ring and analyses what arrived.
// Samples the ring dropped are analysed as silence, so the analysis stays on the capture clock.
// WASAPI loopback delivers nothing while nothing plays: after 50 ms without samples the thread
// analyses silence at the wall clock's pace, and the live clock counts it, so the clock (and the
// predicted beat) keeps moving through gaps.
// Measured and published: the analysis delay (the capture clock when a state is published minus
// the source time its newest decision describes; it includes the device period, the hop, the
// student's lookahead and the decoders' confirmation frames) and the analysis thread's load.
#pragma once

#include "analysis/live_analysis.h"
#include "audio/capture.h"
#include "core/seqlock.h"
#include "ml/student.h"

#include <atomic>
#include <memory>
#include <thread>

namespace psyight {

class LiveInput {
  public:
    explicit LiveInput(std::shared_ptr<const StudentModel> model);
    ~LiveInput();
    LiveInput(const LiveInput &) = delete;
    LiveInput &operator=(const LiveInput &) = delete;

    Result<void> start(const CaptureOptions &capture, const LiveParams &params = {});
    void stop();
    bool running() const { return thread_.joinable(); }

    LiveState state() const { return state_.load(); }
    // One thread (the GUI): an operator command, applied by the analysis thread within 2 ms.
    // Const: the queue is an input channel, not state the caller observes.
    void command(const LiveCommand &c) const { commands_.try_push(c); }
    std::uint32_t state_version() const { return state_.version(); }
    // The live clock: samples captured, extrapolated to now.
    SampleIndex now() const {
        return capture_.captured_now() + filled_.load(std::memory_order_acquire);
    }
    int sample_rate_hz() const { return capture_.sample_rate_hz(); }
    double analysis_delay_ms() const { return delay_ms_.load(std::memory_order_acquire); }
    double load() const { return load_.load(std::memory_order_acquire); } // 0 to 1 of a core.
    float level() const { return capture_.peak(); }
    long long dropped() const { return capture_.dropped(); }
    int device_period_frames() const { return capture_.device_period_frames(); }

  private:
    void run();

    std::shared_ptr<const StudentModel> model_;
    Capture capture_;
    LiveAnalyser analyser_;
    Seqlock<LiveState> state_;
    mutable SpscQueue<LiveCommand> commands_{64};
    std::atomic<SampleIndex> filled_{0}; // Silence analysed for gaps without samples.
    std::atomic<double> delay_ms_{0.0};
    std::atomic<double> load_{0.0};
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

} // namespace psyight
