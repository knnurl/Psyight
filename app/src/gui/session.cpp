#include "gui/session.h"

#include "core/paths.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>

namespace psyight {

namespace {

constexpr int kOverviewBuckets = 4000;

} // namespace

void Session::open_file(const std::filesystem::path &path) {
    if (loading_.valid()) {
        status_ = "Still loading " + loading_name_;
        return;
    }
    loading_name_ = path_to_utf8(path.filename());
    status_ = "Loading and analysing " + loading_name_ + "...";
    loading_ = std::async(std::launch::async, &Session::load, path);
}

Session::LoadResult Session::load(std::filesystem::path path) {
    LoadResult result;
    result.audio = decode_audio_file(path);
    if (!result.audio) {
        return result;
    }
    const AudioBuffer &buffer = result.audio.value();
    auto track = std::make_shared<LoadedTrack>();
    track->name = path_to_utf8(path.filename());
    track->sample_rate_hz = buffer.sample_rate_hz;
    track->duration_s = samples_to_seconds(buffer.frames(), buffer.sample_rate_hz);

    const std::vector<float> mono = mono_mix(buffer);
    const auto frames = static_cast<SampleIndex>(mono.size());
    const int buckets = static_cast<int>(std::min<SampleIndex>(kOverviewBuckets, frames));
    for (int b = 0; b < buckets; ++b) {
        const SampleIndex begin = frames * b / buckets;
        const SampleIndex end = frames * (b + 1) / buckets;
        const auto [low, high] = std::minmax_element(mono.begin() + begin, mono.begin() + end);
        track->overview_time_s.push_back(
            samples_to_seconds((begin + end) / 2, buffer.sample_rate_hz));
        track->overview_min.push_back(std::min(0.0f, *low));
        track->overview_max.push_back(std::max(0.0f, *high));
    }

    const auto start = std::chrono::steady_clock::now();
    Result<FrontEndResult> analysis = analyse_front_end(mono, buffer.sample_rate_hz, {});
    if (analysis) {
        track->analysis = std::make_shared<const FrontEndResult>(std::move(analysis.value()));
        spdlog::info(
            "Analysed {} in {:.0f} ms", track->name,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count());
    } else {
        spdlog::error("Analysis of {} failed: {}", track->name, analysis.error().message);
    }
    result.track = std::move(track);
    return result;
}

void Session::update() {
    if (!loading_.valid() ||
        loading_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    LoadResult result = loading_.get();
    if (!result.audio) {
        status_ = result.audio.error().message;
        spdlog::error("{}", status_);
        return;
    }
    const Result<void> opened = transport_.open(std::move(result.audio.value()));
    if (!opened) {
        status_ = opened.error().message;
        spdlog::error("{}", status_);
        return;
    }
    track_ = std::move(result.track);
    status_.clear();
    spdlog::info("Loaded {} ({:.2f} s), device period {} frames", track_->name, track_->duration_s,
                 transport_.device_period_frames());
}

} // namespace psyight
