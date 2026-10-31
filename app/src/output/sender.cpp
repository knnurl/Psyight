#include "output/sender.h"

namespace psyight {

int OutputSender::send_frame(const OutputSettings &settings,
                             const std::vector<UniverseData> &universes, PacketSink &sink) {
    const auto sacn_sequence = static_cast<std::uint8_t>(frame_);
    const auto art_sequence = static_cast<std::uint8_t>(1 + frame_ % 255);
    ++frame_;
    int sent = 0;
    if (settings.protocol == OutputProtocol::sacn) {
        SacnSource source;
        source.cid = settings.cid;
        source.priority = settings.priority;
        source.sync_universe = settings.sync ? settings.sync_universe : 0;
        for (const UniverseData &u : universes) {
            const auto universe = static_cast<std::uint16_t>(u.universe);
            if (build_sacn_data(source, universe, sacn_sequence, u.channels, buffer_) == 0) {
                continue;
            }
            const Ipv4Endpoint to{
                settings.multicast ? sacn_multicast_group(universe) : settings.address, kSacnPort};
            sink.send(buffer_, to);
            ++sent;
        }
        if (settings.sync &&
            build_sacn_sync(settings.cid, settings.sync_universe, sacn_sequence, buffer_) != 0) {
            const Ipv4Endpoint to{settings.multicast ? sacn_multicast_group(settings.sync_universe)
                                                     : settings.address,
                                  kSacnPort};
            sink.send(buffer_, to);
            ++sent;
        }
        return sent;
    }
    if (settings.protocol == OutputProtocol::ddp) {
        // One frame of bytes, universe after universe as a node maps them (a universe's place is
        // its pixels per universe times three, the first universe's length when there are more),
        // in packets of up to 480 pixels; the last pushes the frame.
        if (universes.empty()) {
            return 0;
        }
        const size_t stride = universes.size() > 1 ? universes.front().channels.size() : 0;
        std::vector<std::uint8_t> &frame = ddp_frame_;
        frame.clear();
        for (const UniverseData &u : universes) {
            const size_t at = static_cast<size_t>(u.universe - universes.front().universe) * stride;
            if (frame.size() < at + u.channels.size()) {
                frame.resize(at + u.channels.size(), 0);
            }
            std::copy(u.channels.begin(), u.channels.end(),
                      frame.begin() + static_cast<std::ptrdiff_t>(at));
        }
        const Ipv4Endpoint to{settings.address, kDdpPort};
        for (size_t at = 0; at < frame.size(); at += kDdpMaxData) {
            const size_t n = std::min(kDdpMaxData, frame.size() - at);
            ddp_sequence_ = static_cast<std::uint8_t>(ddp_sequence_ % 15 + 1);
            build_ddp(ddp_sequence_, static_cast<std::uint32_t>(at),
                      std::span<const std::uint8_t>(frame).subspan(at, n), at + n >= frame.size(),
                      buffer_);
            sink.send(buffer_, to);
            ++sent;
        }
        return sent;
    }
    const Ipv4Endpoint to{settings.address, kArtNetPort};
    for (const UniverseData &u : universes) {
        if (u.universe < 1 || build_art_dmx(static_cast<std::uint16_t>(u.universe - 1),
                                            art_sequence, u.channels, buffer_) == 0) {
            continue;
        }
        sink.send(buffer_, to);
        ++sent;
    }
    if (settings.sync) {
        build_art_sync(buffer_);
        sink.send(buffer_, to);
        ++sent;
    }
    return sent;
}

Cid cid_from_name(const std::string &name) {
    Cid cid{};
    std::uint64_t h = 0xcbf29ce484222325ull; // FNV-1a, then SplitMix to fill 16 bytes.
    for (const char c : name) {
        h = (h ^ static_cast<std::uint8_t>(c)) * 0x100000001b3ull;
    }
    for (size_t i = 0; i < cid.size(); i += 8) {
        h += 0x9e3779b97f4a7c15ull;
        std::uint64_t z = h;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        z ^= z >> 31;
        for (size_t b = 0; b < 8; ++b) {
            cid[i + b] = static_cast<std::uint8_t>(z >> (8 * b));
        }
    }
    return cid;
}

} // namespace psyight
