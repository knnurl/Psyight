#include "output/virtual_node.h"

#include <algorithm>

namespace psyight {

void VirtualNode::set_patch(const Patch &patch) {
    patch_ = patch;
    pending_.assign(patch.universes.size(), {});
    latched_.assign(patch.universes.size(), {});
    last_sequence_.assign(patch.universes.size(), -1);
    // A DDP frame holds the universes back to back, each its pixels per universe long: the
    // first universe's pixels when there are more (pixels never split across universes).
    ddp_stride_ = 0;
    if (patch.universes.size() > 1) {
        for (const PatchedPixel &p : patch.pixels) {
            ddp_stride_ += p.universe_slot == 0 ? 3 : 0;
        }
    }
}

int VirtualNode::slot_of(int universe) const {
    const auto it = std::find(patch_.universes.begin(), patch_.universes.end(), universe);
    return it == patch_.universes.end() ? -1 : static_cast<int>(it - patch_.universes.begin());
}

void VirtualNode::send(std::span<const std::uint8_t> packet, const Ipv4Endpoint &to) {
    ++stats_.packets;
    const bool art_net = to.port == kArtNetPort;
    const std::optional<ParsedPacket> parsed = to.port == kDdpPort ? parse_ddp(packet)
                                               : art_net           ? parse_art_net(packet)
                                                                   : parse_sacn(packet);
    if (!parsed) {
        ++stats_.bad_packets;
        return;
    }
    if (parsed->kind == ParsedPacket::Kind::ddp) {
        for (size_t i = 0; i < parsed->slots.size(); ++i) {
            const size_t at = parsed->offset + i;
            const size_t u = ddp_stride_ > 0 ? at / ddp_stride_ : 0;
            const size_t within = ddp_stride_ > 0 ? at % ddp_stride_ : at;
            if (u >= pending_.size()) {
                break;
            }
            if (pending_[u].size() <= within) {
                pending_[u].resize(within + 1, 0);
            }
            pending_[u][within] = parsed->slots[i];
        }
        if (parsed->push) {
            latched_ = pending_;
            ++stats_.latches;
        }
        return;
    }
    if (parsed->kind == ParsedPacket::Kind::sync) {
        if (!art_net && parsed->sync_universe != sacn_sync_universe_) {
            return; // Another sync group's.
        }
        art_sync_mode_ |= art_net;
        latched_ = pending_;
        ++stats_.latches;
        return;
    }
    const int slot = slot_of(parsed->universe);
    if (slot < 0) {
        return; // Not ours.
    }
    const auto s = static_cast<size_t>(slot);
    int expected = (last_sequence_[s] + 1) % 256;
    if (art_net && expected == 0) {
        expected = 1; // Art-Net skips 0.
    }
    if (last_sequence_[s] >= 0 && parsed->sequence != expected) {
        ++stats_.sequence_gaps;
    }
    last_sequence_[s] = parsed->sequence;
    pending_[s] = parsed->slots;
    if (!art_net) {
        sacn_sync_universe_ = parsed->sync_universe;
    }
    const bool held = art_net ? art_sync_mode_ : parsed->sync_universe != 0;
    if (!held) {
        latched_[s] = pending_[s];
        ++stats_.latches;
    }
}

} // namespace psyight
