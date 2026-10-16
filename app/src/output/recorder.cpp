#include "output/recorder.h"

#include "core/paths.h"
#include "io/text_file.h"

#include <chrono>

namespace psyight {

namespace {

constexpr auto kIdleWait = std::chrono::milliseconds(2);

} // namespace

Result<std::unique_ptr<Recorder>> Recorder::start(const std::filesystem::path &folder,
                                                  const SessionMeta &meta,
                                                  const std::string &events) {
    std::error_code error;
    if (std::filesystem::exists(folder, error)) {
        return Error{path_to_utf8(folder) + " already exists"};
    }
    if (!std::filesystem::create_directories(folder, error)) {
        return Error{"cannot create " + path_to_utf8(folder) + ": " + error.message()};
    }
    const Result<std::string> meta_text = meta_json(meta);
    if (!meta_text) {
        return meta_text.error();
    }
    if (Result<void> w = write_text_file_atomic(folder / "meta.json", meta_text.value()); !w) {
        return w.error();
    }
    if (Result<void> w = write_text_file_atomic(folder / "events.jsonl", events); !w) {
        return w.error();
    }
    std::unique_ptr<Recorder> r(new Recorder(folder, meta.pixels));
    if (!r->frames_ || !r->params_) {
        return Error{"cannot open the session files in " + path_to_utf8(folder)};
    }
    const std::vector<std::uint8_t> header = frames_header(meta.pixels);
    r->frames_.write(reinterpret_cast<const char *>(header.data()),
                     static_cast<std::streamsize>(header.size()));
    r->thread_ = std::thread([raw = r.get()] { raw->run(); });
    return r;
}

Recorder::Recorder(std::filesystem::path folder, int pixels)
    : folder_(std::move(folder)), pixels_(pixels),
      frames_(folder_ / "frames.bin", std::ios::binary | std::ios::trunc),
      params_(folder_ / "params.jsonl", std::ios::binary | std::ios::trunc) {}

Recorder::~Recorder() {
    stop_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool Recorder::push_frame(std::shared_ptr<const RecordedFrame> frame) {
    if (frame->rgb.size() != 3 * static_cast<size_t>(pixels_) || !frame_queue_.try_push(frame)) {
        ++dropped_;
        return false;
    }
    return true;
}

bool Recorder::push_params(std::shared_ptr<const std::string> line) {
    if (!params_queue_.try_push(line)) {
        ++dropped_;
        return false;
    }
    return true;
}

std::string Recorder::error() const { return failed_.load() ? error_ : std::string(); }

bool Recorder::drain() {
    bool any = false;
    std::shared_ptr<const std::string> line;
    while (params_queue_.try_pop(line)) {
        params_ << *line;
        params_.flush(); // Configurations are rare; a crash keeps them.
        any = true;
    }
    std::shared_ptr<const RecordedFrame> frame;
    std::vector<std::uint8_t> bytes;
    while (frame_queue_.try_pop(frame)) {
        bytes.clear();
        append_frame(bytes, *frame);
        frames_.write(reinterpret_cast<const char *>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        ++frames_written_;
        any = true;
    }
    if ((!frames_ || !params_) && !failed_.load()) {
        error_ = "writing the session in " + path_to_utf8(folder_) + " failed";
        failed_ = true;
    }
    return any;
}

void Recorder::run() {
    while (!stop_.load()) {
        if (!drain()) {
            std::this_thread::sleep_for(kIdleWait);
        }
    }
    drain(); // What was queued before the stop.
    frames_.flush();
    params_.flush();
}

} // namespace psyight
