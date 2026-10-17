// psyight_soak: the live show for hours, to find leaks, drift and stalls (SPEC M6 soak test).
//
//   psyight_soak <audio> [--minutes N] [--student <weights>]
//
// Runs live mode as the app does, on the file capture source (the track looping at real-time
// pace, silent): capture, the analysis thread, and the show thread rendering the default rig at
// 60 fps from the live state, with the limiters and the virtual node (no network). Every minute
// it prints frames rendered, late frames, the last render time, the analysis delay and load,
// dropped samples, the loop's lock and tempo, the section, and the process's working set and
// private bytes. Ends with the growth in memory and the totals. Windows only (process memory).

#include "audio/live_input.h"
#include "core/paths.h"
#include "core/playhead_clock.h"
#include "ml/student.h"
#include "output/show.h"

#include <windows.h>

#include <psapi.h>

#include <chrono>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

using namespace psyight;

namespace {

struct Memory {
    double working_set_mb = 0.0;
    double private_mb = 0.0;
};

Memory process_memory() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    GetProcessMemoryInfo(GetCurrentProcess(),
                         reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters));
    return {counters.WorkingSetSize / 1048576.0, counters.PrivateUsage / 1048576.0};
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    std::filesystem::path audio;
    std::filesystem::path weights = "data/m4/student.weights";
    double minutes = 240.0;
    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--minutes") == 0 && i + 1 < argc) {
            minutes = _wtof(argv[++i]);
        } else if (std::wcscmp(argv[i], L"--student") == 0 && i + 1 < argc) {
            weights = argv[++i];
        } else {
            audio = argv[i];
        }
    }
    if (audio.empty()) {
        std::fprintf(stderr, "usage: psyight_soak <audio> [--minutes N] [--student <weights>]\n");
        return 2;
    }
    Result<StudentModel> model = load_student(weights);
    if (!model) {
        std::fprintf(stderr, "%s\n", model.error().message.c_str());
        return 1;
    }
    auto live = std::make_shared<LiveInput>(std::make_shared<const StudentModel>(model.value()));
    CaptureOptions options;
    options.source = CaptureSource::file;
    options.file = audio;
    if (Result<void> started = live->start(options); !started) {
        std::fprintf(stderr, "%s\n", started.error().message.c_str());
        return 1;
    }
    ShowThread show([live, clock = PlayheadClock()]() mutable {
        const double now_s =
            std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        return clock.update(now_s, live->now(), live->sample_rate_hz());
    });
    auto config = std::make_shared<ShowConfig>();
    config->live = live;
    config->sample_rate_hz = live->sample_rate_hz();
    config->version = 1;
    show.set_config(config);

    const Memory first = process_memory();
    std::printf("%s: live show on the looping file (silent), %.0f minutes; start: working set "
                "%.1f MB, private %.1f MB\n",
                path_to_utf8(audio.filename()).c_str(), minutes, first.working_set_mb,
                first.private_mb);
    std::printf("minute | frames | late | render ms | delay ms | load %% | dropped | lock | BPM | "
                "section | working set MB | private MB\n");
    std::fflush(stdout);
    const auto start = std::chrono::steady_clock::now();
    Memory peak = first;
    for (int minute = 1; minute <= static_cast<int>(minutes); ++minute) {
        std::this_thread::sleep_until(start + std::chrono::minutes(minute));
        const std::shared_ptr<const ShowFrame> frame = show.latest();
        const LiveState s = live->state();
        const Memory m = process_memory();
        peak.working_set_mb = std::max(peak.working_set_mb, m.working_set_mb);
        peak.private_mb = std::max(peak.private_mb, m.private_mb);
        std::printf("%6d | %6lld | %4lld | %9.2f | %8.1f | %6.2f | %7lld | %4s | %6.2f | %-9s | "
                    "%14.1f | %10.1f\n",
                    minute, frame != nullptr ? static_cast<long long>(frame->index) : 0LL,
                    frame != nullptr ? static_cast<long long>(frame->late_frames) : 0LL,
                    frame != nullptr ? frame->render_ms : 0.0, live->analysis_delay_ms(),
                    100.0 * live->load(), live->dropped(), s.locked ? "yes" : "no",
                    s.tempo_known ? 60.0 / s.period_s : 0.0, live_section_name(s.section),
                    m.working_set_mb, m.private_mb);
        std::fflush(stdout);
    }
    const std::shared_ptr<const ShowFrame> frame = show.latest();
    const Memory last = process_memory();
    const double elapsed_s =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("done: %.0f s, %lld frames (%.2f fps), %lld late; private bytes %+.1f MB over the "
                "run (peak %.1f MB); dropped samples %lld\n",
                elapsed_s, frame != nullptr ? static_cast<long long>(frame->index + 1) : 0LL,
                frame != nullptr ? (frame->index + 1) / elapsed_s : 0.0,
                frame != nullptr ? static_cast<long long>(frame->late_frames) : 0LL,
                last.private_mb - first.private_mb, peak.private_mb, live->dropped());
    live->stop();
    return 0;
}
