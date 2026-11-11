// The decks as the link reports them (docs/PLAN_FOLLOW_MODE.md, phase 2): what is known of each
// player from the packets so far, from either protocol. A pure state machine over stamped
// packets, so a log replays to the same decks.
//
// Playing: TCNet says so in its layer state. Pro DJ Link passive has no play flag, so a deck
// plays while its absolute position advances (by over a quarter of the time passed) or its
// beats keep coming (one within the last two beats); it stops when neither holds.
// Position: the last reported playhead, extrapolated by the time since at speed 1 + pitch while
// playing. CDJ-3000 positions arrive about every 30 ms and TCNet times every 1 to 40 ms, so the
// extrapolation spans little. Older players (CDJ-2000NXS2) send beats but no absolute position:
// a passive listener cannot know where in the track they are (has_position stays false).
// On air: the mixer's channel n is taken as player n [Verify: the mixer's channel assignment].
#pragma once

#include "link/packet.h"
#include "link/prodjlink.h"
#include "link/tcnet.h"

#include <array>
#include <cstdint>
#include <string>

namespace psyight {

inline constexpr int kMaxDecks = 6;
inline constexpr std::int64_t kDeckStaleNs = 3'000'000'000; // Silent this long: gone.

struct DeckState {
    int number = 0; // 1 to kMaxDecks.
    std::string name;
    bool present = false;
    std::int64_t seen_ns = 0; // The last packet about it.

    bool has_position = false;
    double playhead_ms = 0.0;     // At position_ns.
    std::int64_t position_ns = 0; // When playhead_ms was reported.
    double track_length_s = 0.0;  // 0 when unknown.
    bool playing = false;
    std::int64_t started_ns = 0; // When it last began playing.
    double pitch = 0.0;          // Fraction: 0.06 is +6%.
    double track_bpm = 0.0;      // At 0% pitch; 0 when unknown.
    int beat_in_bar = 0;         // From the last beat packet; 0 when none.
    std::int64_t beat_ns = 0;
    bool on_air_known = false; // A mixer has reported this deck's channel.
    bool on_air = false;

    bool operator==(const DeckState &) const = default;
};

// The deck's playhead at t_ns (ms), extrapolated while playing.
double deck_position_ms(const DeckState &deck, std::int64_t t_ns);

class DeckSet {
  public:
    DeckSet();
    // Decodes and applies a packet (by its port); others are ignored. Returns whether it changed
    // anything.
    bool apply(const LinkPacket &packet);
    void apply(const prodjlink::Message &message, std::int64_t t_ns);
    void apply(const tcnet::Time &time, std::int64_t t_ns);
    // Decks silent for longer than stale_ns are cleared.
    void expire(std::int64_t now_ns, std::int64_t stale_ns = kDeckStaleNs);

    const std::array<DeckState, kMaxDecks> &decks() const { return decks_; }
    // Number 1 to kMaxDecks, or null.
    const DeckState *deck(int number) const;
    std::string mixer_name() const { return mixer_name_; }

  private:
    DeckState &at(int number, std::int64_t t_ns);
    void set_playing(DeckState &d, bool playing, std::int64_t t_ns);

    std::array<DeckState, kMaxDecks> decks_{};
    std::string mixer_name_;
    std::int64_t mixer_seen_ns_ = 0;
};

} // namespace psyight
