// MIDI controller input (M5) through RtMidi (Windows multimedia MIDI). RtMidi's callback runs on
// its own thread and only pushes the message into a lock-free queue; the GUI drains it once a
// frame and maps the messages (io/midi_map.h). RtMidi reports errors by throwing: open() and
// ports() catch them and return errors.
#pragma once

#include "core/result.h"
#include "core/spsc_queue.h"
#include "io/midi_map.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

class RtMidiIn;

namespace psyight {

class MidiInput {
  public:
    MidiInput();
    ~MidiInput();
    MidiInput(const MidiInput &) = delete;
    MidiInput &operator=(const MidiInput &) = delete;

    static Result<std::vector<std::string>> ports();
    Result<void> open(const std::string &port);
    void close();
    bool is_open() const { return in_ != nullptr; }
    const std::string &port() const { return port_; }

    // GUI thread: the messages received since the last call, oldest first.
    void drain(std::vector<MidiMessage> &out);
    long long dropped() const { return dropped_.load(); } // Queue full (the GUI stalled).

  private:
    static void callback(double stamp_s, std::vector<unsigned char> *message, void *user);

    std::unique_ptr<RtMidiIn> in_;
    std::string port_;
    SpscQueue<MidiMessage> queue_{1024};
    std::atomic<long long> dropped_{0}; // Written by RtMidi's thread only.
};

} // namespace psyight
