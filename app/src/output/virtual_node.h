// Virtual node (SPEC sections 8 and 11): an in-app receiver for the packets the sender produces,
// parsing them as a real node would and latching universes into a frame that drives the preview.
// sACN: data with a sync universe is held until that universe's sync packet; data without one
// latches at once. Art-Net: after the first ArtSync, ArtDmx is held until the next ArtSync.
// Counts packets, latched frames, parse failures and sequence gaps per universe, so the preview
// also tests the packetising. Fault injection comes later (SPEC 11).
#pragma once

#include "output/patch.h"
#include "output/sender.h"

#include <cstdint>
#include <vector>

namespace psyight {

struct VirtualNodeStats {
    std::int64_t packets = 0;
    std::int64_t bad_packets = 0;
    std::int64_t latches = 0;
    std::int64_t sequence_gaps = 0;
};

class VirtualNode final : public PacketSink {
  public:
    // Listens to the patch's universes.
    void set_patch(const Patch &patch);
    void send(std::span<const std::uint8_t> packet, const Ipv4Endpoint &to) override;

    // RGB bytes per rig pixel from the last latched universes.
    std::vector<std::uint8_t> rgb() const { return unpatch_frame(patch_, latched_); }
    const VirtualNodeStats &stats() const { return stats_; }

  private:
    int slot_of(int universe) const;

    Patch patch_;
    std::vector<std::vector<std::uint8_t>> pending_;
    std::vector<std::vector<std::uint8_t>> latched_;
    std::vector<int> last_sequence_; // -1 before the first packet.
    bool art_sync_mode_ = false;
    std::uint16_t sacn_sync_universe_ = 0;
    VirtualNodeStats stats_;
};

} // namespace psyight
