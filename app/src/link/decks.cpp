#include "link/decks.h"

#include <algorithm>
#include <cmath>
#include <variant>

namespace psyight {

namespace {

constexpr double kAdvanceShare = 0.25; // Of the time passed, for "the position advances".
constexpr std::int64_t kMinElapsedNs = 10'000'000;
constexpr std::int64_t kStillNs = 150'000'000; // Unmoved this long between positions: stopped.

double beat_s_of(const DeckState &d) {
    const double bpm = d.track_bpm * (1.0 + d.pitch);
    return bpm > 0.0 ? 60.0 / bpm : 0.5;
}

} // namespace

double deck_position_ms(const DeckState &deck, std::int64_t t_ns) {
    if (!deck.has_position) {
        return 0.0;
    }
    if (!deck.playing) {
        return deck.playhead_ms;
    }
    const double elapsed_ms = static_cast<double>(t_ns - deck.position_ns) / 1e6;
    return deck.playhead_ms + std::max(0.0, elapsed_ms) * (1.0 + deck.pitch);
}

DeckSet::DeckSet() {
    for (int i = 0; i < kMaxDecks; ++i) {
        decks_[static_cast<size_t>(i)].number = i + 1;
    }
}

const DeckState *DeckSet::deck(int number) const {
    return number >= 1 && number <= kMaxDecks ? &decks_[static_cast<size_t>(number - 1)] : nullptr;
}

DeckState &DeckSet::at(int number, std::int64_t t_ns) {
    DeckState &d = decks_[static_cast<size_t>(std::clamp(number, 1, kMaxDecks) - 1)];
    d.present = true;
    d.seen_ns = t_ns;
    return d;
}

void DeckSet::set_playing(DeckState &d, bool playing, std::int64_t t_ns) {
    if (playing && !d.playing) {
        d.started_ns = t_ns;
    }
    d.playing = playing;
}

bool DeckSet::apply(const LinkPacket &packet) {
    if (packet.port == prodjlink::kAnnouncePort || packet.port == prodjlink::kBeatPort) {
        const std::optional<prodjlink::Message> m = prodjlink::decode(packet.port, packet.bytes);
        if (!m) {
            return false;
        }
        apply(*m, packet.t_ns);
        return true;
    }
    if (packet.port == tcnet::kTimePort) {
        const std::optional<tcnet::Time> t = tcnet::decode_time(packet.bytes);
        if (!t) {
            return false;
        }
        apply(*t, packet.t_ns);
        return true;
    }
    return false;
}

void DeckSet::apply(const prodjlink::Message &message, std::int64_t t_ns) {
    if (const auto *k = std::get_if<prodjlink::KeepAlive>(&message)) {
        // A player's keep-alive (numbers 1 to 6); the mixer (33) and computers are not decks.
        if (k->device >= 1 && k->device <= kMaxDecks) {
            at(k->device, t_ns).name = k->name;
        }
    } else if (const auto *b = std::get_if<prodjlink::Beat>(&message)) {
        if (b->device >= 1 && b->device <= kMaxDecks) {
            DeckState &d = at(b->device, t_ns);
            d.name = b->name;
            d.track_bpm = b->bpm;
            d.pitch = b->pitch;
            d.beat_in_bar = b->beat_in_bar;
            d.beat_ns = t_ns;
            if (!d.has_position) {
                set_playing(d, true, t_ns); // Beats come only while playing.
            }
        }
    } else if (const auto *o = std::get_if<prodjlink::OnAir>(&message)) {
        mixer_name_ = o->name;
        mixer_seen_ns_ = t_ns;
        for (int c = 0; c < o->channels && c < kMaxDecks; ++c) {
            DeckState &d = decks_[static_cast<size_t>(c)];
            d.on_air_known = true;
            d.on_air = o->on_air[static_cast<size_t>(c)];
        }
    } else if (const auto *p = std::get_if<prodjlink::Position>(&message)) {
        if (p->device >= 1 && p->device <= kMaxDecks) {
            DeckState &d = at(p->device, t_ns);
            d.name = p->name;
            const std::int64_t elapsed = t_ns - d.position_ns;
            if (d.has_position && elapsed >= kMinElapsedNs) {
                const double moved = p->playhead_ms - d.playhead_ms;
                const double expected = static_cast<double>(elapsed) / 1e6;
                if (moved > kAdvanceShare * expected) {
                    set_playing(d, true, t_ns);
                    d.position_ns = t_ns;
                    d.playhead_ms = p->playhead_ms;
                } else if (moved != 0.0 || elapsed >= kStillNs) {
                    // Moved backwards (a cue or a seek), or still for long enough: stopped.
                    set_playing(d, false, t_ns);
                    d.position_ns = t_ns;
                    d.playhead_ms = p->playhead_ms;
                }
                // Unmoved for less than kStillNs: keep the last report (no new evidence).
            } else if (!d.has_position) {
                d.position_ns = t_ns;
                d.playhead_ms = p->playhead_ms;
            }
            d.has_position = true;
            d.track_length_s = p->track_length_s;
            d.pitch = p->pitch;
            if (p->bpm > 0.0) {
                d.track_bpm = p->bpm / (1.0 + p->pitch); // Reported at pitch [Verify].
            }
        }
    }
}

void DeckSet::apply(const tcnet::Time &time, std::int64_t t_ns) {
    for (int l = 0; l < tcnet::kLayers && l < kMaxDecks; ++l) {
        const auto i = static_cast<size_t>(l);
        const bool loaded = time.playhead_ms[i] != 0 || (time.has_totals && time.total_ms[i] != 0);
        if (!loaded && !decks_[i].present) {
            continue;
        }
        DeckState &d = at(l + 1, t_ns);
        if (d.name.empty()) {
            d.name = time.header.node_name + " layer " + std::to_string(l + 1);
        }
        d.has_position = true;
        d.playhead_ms = time.playhead_ms[i];
        d.position_ns = t_ns;
        if (time.has_totals) {
            d.track_length_s = time.total_ms[i] / 1000.0;
        }
        if (time.has_states) {
            set_playing(d, tcnet::advancing(time.state[i]), t_ns);
        }
    }
}

void DeckSet::expire(std::int64_t now_ns, std::int64_t stale_ns) {
    for (DeckState &d : decks_) {
        if (d.present && now_ns - d.seen_ns > stale_ns) {
            const int number = d.number;
            d = DeckState{};
            d.number = number;
        } else if (d.present && d.playing && !d.has_position && d.beat_ns != 0 &&
                   static_cast<double>(now_ns - d.beat_ns) / 1e9 > 2.0 * beat_s_of(d)) {
            d.playing = false; // Beats stopped.
        }
    }
    if (!mixer_name_.empty() && now_ns - mixer_seen_ns_ > stale_ns) {
        mixer_name_.clear();
        for (DeckState &d : decks_) {
            d.on_air_known = false;
            d.on_air = false;
        }
    }
}

} // namespace psyight
