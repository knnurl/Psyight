/* Art-Net 4, sACN (E1.31) and DDP receive side for the pixel node (SPEC sections 8 and 12).
 *
 * Portable C99 with no hardware dependency: the app's unit tests compile this file and feed it
 * the app's own packet builders, so the two ends are checked against each other on the PC.
 * Parsers never copy: psy_packet.slots points into the caller's buffer. Multi-byte fields are
 * read byte by byte (network order is big-endian; several fields sit at odd offsets).
 * Universes use sACN numbering (1 to 63999) throughout; an Art-Net port address p is universe
 * p + 1, as in the app.
 */
#ifndef PSY_PROTOCOL_H
#define PSY_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PSY_ARTNET_PORT 6454
#define PSY_SACN_PORT 5568
#define PSY_DDP_PORT 4048
#define PSY_MAX_SLOTS 512
#define PSY_ART_POLL_REPLY_BYTES 239
#define PSY_ART_POLL_REPLY_PORTS 4

typedef enum { PSY_PACKET_DMX, PSY_PACKET_SYNC, PSY_PACKET_POLL, PSY_PACKET_DDP } psy_packet_kind;

typedef struct {
    psy_packet_kind kind;
    uint16_t universe;      /* DMX: sACN numbering. */
    uint8_t sequence;       /* sACN 0..255; Art-Net 1..255, 0 meaning unsequenced. */
    uint16_t sync_universe; /* sACN data and sync packets; 0: none. */
    uint8_t priority;       /* sACN only. */
    const uint8_t *slots;   /* DMX and DDP: into the caller's buffer. */
    uint16_t slot_count;
    uint32_t offset; /* DDP: where the bytes go in the frame. */
    bool push;       /* DDP: the frame is complete; show it. */
} psy_packet;

/* Return true and fill `out` for a well-formed packet of the protocol. */
bool psy_parse_sacn(const uint8_t *p, size_t n, psy_packet *out);
bool psy_parse_art_net(const uint8_t *p, size_t n, psy_packet *out); /* ArtDmx, ArtSync, ArtPoll. */
/* DDP data for display 1 (version 1, not a query or reply; a timecode header is skipped): the
 * bytes at `offset` of the frame, sequence 1..15 (0: none), push. */
bool psy_parse_ddp(const uint8_t *p, size_t n, psy_packet *out);

/* The IPv4 multicast group of an sACN universe: 239.255.hi.lo. */
void psy_sacn_group(uint16_t universe, uint8_t group[4]);

typedef struct {
    uint8_t ip[4];
    uint8_t mac[6];
    const char *short_name;                       /* Up to 17 characters. */
    const char *long_name;                        /* Up to 63. */
    const char *report;                           /* Up to 63. */
    uint16_t universes[PSY_ART_POLL_REPLY_PORTS]; /* Outputs, sACN numbering, one Sub-Net. */
    uint8_t universe_count;
    uint8_t bind_index; /* 1 for the first reply of a node, then 2, 3... */
} psy_poll_reply;

/* Writes one ArtPollReply; returns its size, or 0 when the universes do not share a Sub-Net,
 * there are none or too many, or the buffer is too small. */
size_t psy_build_art_poll_reply(const psy_poll_reply *reply, uint8_t *out, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
