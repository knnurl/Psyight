#include "io/midi_input.h"

#include <rtmidi/RtMidi.h>

namespace psyight {

MidiInput::MidiInput() = default;

MidiInput::~MidiInput() { close(); }

Result<std::vector<std::string>> MidiInput::ports() {
    try {
        RtMidiIn in;
        std::vector<std::string> names;
        for (unsigned int i = 0; i < in.getPortCount(); ++i) {
            names.push_back(in.getPortName(i));
        }
        return names;
    } catch (const RtMidiError &e) {
        return Error{"MIDI: " + e.getMessage()};
    }
}

Result<void> MidiInput::open(const std::string &port) {
    close();
    try {
        auto in = std::make_unique<RtMidiIn>();
        for (unsigned int i = 0; i < in->getPortCount(); ++i) {
            if (in->getPortName(i) == port) {
                in->setCallback(&MidiInput::callback, this);
                in->ignoreTypes(true, true, true); // Sysex, timing and active sensing.
                in->openPort(i, "Psyight");
                in_ = std::move(in);
                port_ = port;
                return {};
            }
        }
        return Error{"no MIDI input named \"" + port + "\""};
    } catch (const RtMidiError &e) {
        return Error{"MIDI: " + e.getMessage()};
    }
}

void MidiInput::close() {
    if (in_ != nullptr) {
        try {
            in_->closePort();
        } catch (const RtMidiError &) {
        }
        in_.reset();
        port_.clear();
    }
}

void MidiInput::drain(std::vector<MidiMessage> &out) {
    MidiMessage m;
    while (queue_.try_pop(m)) {
        out.push_back(m);
    }
}

void MidiInput::callback(double /*stamp_s*/, std::vector<unsigned char> *message, void *user) {
    auto *self = static_cast<MidiInput *>(user);
    if (message == nullptr || message->empty()) {
        return;
    }
    MidiMessage m;
    m.status = (*message)[0];
    m.data1 = message->size() > 1 ? (*message)[1] : 0;
    m.data2 = message->size() > 2 ? (*message)[2] : 0;
    if (!self->queue_.try_push(m)) {
        ++self->dropped_;
    }
}

} // namespace psyight
