// Pixel model (SPEC section 7.1): LED strips laid along polylines, each pixel with a position in
// metres, its strip and its index along the strip. Effects work on positions, not strip indices,
// so a layout change needs no effect change. The patch (pixels to universes) is separate (M3,
// output).
#pragma once

#include <string>
#include <vector>

namespace psyight {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct StripSpec {
    std::string name;
    std::vector<Vec3> polyline; // At least two points, in metres.
    double pixels_per_m = 60.0;
    int count = 0;                   // Pixels; they start half a pitch in from the first point.
    std::vector<std::string> groups; // Tags for masks and routing, for example "left".
};

struct Pixel {
    Vec3 position;
    int strip = 0;
    int index = 0; // Along its strip, from the polyline's first point.
};

struct Rig {
    std::vector<StripSpec> strips;
    std::vector<Pixel> pixels; // Strip by strip.
    std::vector<int> fits;     // Per strip: the pixels its pieces hold (strip_pieces).
    Vec3 min;                  // Bounding box of the pixels.
    Vec3 max;
};

// Joints: a strip cannot run through another or bend sideways round a corner, so where it does
// it is cut and joined by a wire. A strip's pieces are the stretches of its polyline (in metres
// along it) left after taking out, within kJointClearanceM:
// - each sharp corner (a turn over kJointTurnDeg, a closed strip's start included): both sides;
// - where it comes near another strip (where that strip really is: its own corners already cut
//   out, so a spoke ending on a ring's corner runs into the gap there, level with the ring's
//   pieces): a strip whose end is there stops short (both, when two ends meet); where two strips
//   cross or run together away from their ends, one runs through and the other is cut, with a
//   gap each side: an open strip runs through a closed one (spokes through rings), then the
//   longer through the shorter, then the earlier in the list.
// The clearance is measured to the other strip's centre line (or the corner): with 10 mm strip,
// the pieces' edges just meet (the owner's choice, 2026-10-26). A strip under
// kJointMinPixelsPerM (a pitch over 50 mm, wider than any LED strip) is a string of single pixels
// wired one to the next, placed on purpose: it has no joints and cuts no other strip.
inline constexpr double kJointClearanceM = 0.005;
inline constexpr double kJointTurnDeg = 20.0;
inline constexpr double kJointMinPixelsPerM = 20.0;
struct Span {
    double from_m = 0.0;
    double to_m = 0.0;
};
std::vector<std::vector<Span>> strip_pieces(const std::vector<StripSpec> &strips);
// The pixels that fit on the pieces at this density: each piece's own whole number.
int pixels_fitting(const std::vector<Span> &pieces, double pixels_per_m);
// Every strip's count set to what fits on its pieces.
void fit_counts(std::vector<StripSpec> &strips);
// After an edit: a strip that filled its path before (its count within a pixel of what fitted,
// with joints or without) and whose count the edit left alone gets what fits now; other counts
// stand. Strips are matched by name (a new one counts as filled when its count fills its path).
void refit_counts(std::vector<StripSpec> &strips, const std::vector<StripSpec> &before);

// Places every strip's pixels along its pieces at the strip's own pitch, piece after piece: a
// piece holding all it can has its pixels centred on it (the part of a pitch left over shared
// between its ends); one holding fewer starts half a pitch in from its start. Pixels beyond what
// the pieces hold continue along the last piece and past the polyline's end in the last segment's
// direction. A strip with no joints has one piece, its whole polyline.
Rig build_rig(std::vector<StripSpec> strips);

// A home-test rig: a 2.5 m wide, 1.2 m high frame of four strips at 60 pixels per metre (150
// along the bottom and top, 72 up each side: 444 pixels, under the SPEC's 1,500), the sides a
// pitch outside the bottom and top so no strip runs into another at the corners (joints).
Rig default_rig();

// Wiring on one data line, changed only where it helps: from strip `first` as it runs, the next
// strip stays the next in the current order unless another strip's start (or, with
// allow_reverse, its end: it is turned round) lies at least kWiringMinGainM nearer the last
// one's end. Closed loops are never turned round (that changes nothing but their direction).
inline constexpr double kWiringMinGainM = 0.05;
struct WiringStep {
    size_t strip = 0;
    bool reversed = false;
};
std::vector<WiringStep> wiring_order(const std::vector<StripSpec> &strips, size_t first,
                                     bool allow_reverse);
// The strip turned round: its polyline from the other end, the same pixels.
StripSpec reversed_strip(StripSpec strip);
// The data jumpers' total length: from each strip's end to the next one's start, in metres.
double jumper_length_m(const std::vector<StripSpec> &strips);

} // namespace psyight
