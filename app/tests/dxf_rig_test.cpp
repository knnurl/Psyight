// A rig from a DXF drawing (io/dxf_rig.h): units, layers as groups, frozen layers, bulges,
// arcs and circles turned so mirrored halves run from the centre outwards, splines.

#include "io/dxf_rig.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

using namespace psyight;

namespace {

constexpr double kPi = 3.14159265358979323846;

// DXF text from code and value pairs.
std::string dxf(std::initializer_list<std::pair<int, std::string>> pairs) {
    std::string out;
    for (const auto &[code, value] : pairs) {
        out += "  " + std::to_string(code) + "\r\n" + value + "\r\n";
    }
    return out;
}

// A millimetre drawing with layers Contour, Rings, Tunnels and a frozen Photo.
std::string drawing(std::initializer_list<std::pair<int, std::string>> entities) {
    std::string out =
        dxf({{0, "SECTION"}, {2, "HEADER"}, {9, "$INSUNITS"}, {70, "4"},     {0, "ENDSEC"},
             {0, "SECTION"}, {2, "TABLES"}, {0, "TABLE"},     {2, "LAYER"},  {0, "LAYER"},
             {2, "Contour"}, {70, "0"},     {62, "7"},        {0, "LAYER"},  {2, "Photo"},
             {70, "1"},      {62, "7"},     {0, "ENDTAB"},    {0, "ENDSEC"}, {0, "SECTION"},
             {2, "ENTITIES"}});
    out += dxf(entities);
    out += dxf({{0, "ENDSEC"}, {0, "EOF"}});
    return out;
}

double length_m(const StripSpec &s) {
    double length = 0.0;
    for (size_t i = 1; i < s.polyline.size(); ++i) {
        length += std::hypot(s.polyline[i].x - s.polyline[i - 1].x,
                             s.polyline[i].y - s.polyline[i - 1].y);
    }
    return length;
}

bool has(const StripSpec &s, const char *group) {
    return std::find(s.groups.begin(), s.groups.end(), group) != s.groups.end();
}

const StripSpec *find(const SvgRig &rig, const char *name) {
    for (const StripSpec &s : rig.strips) {
        if (s.name == name) {
            return &s;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("dxf rig: millimetres, layers as groups, frozen layers left out, y up") {
    const std::string text = drawing({{0, "LINE"},
                                      {8, "Contour"},
                                      {10, "0"},
                                      {20, "0"},
                                      {11, "1400"},
                                      {21, "0"},
                                      {0, "LINE"},
                                      {8, "Photo"},
                                      {10, "0"},
                                      {20, "0"},
                                      {11, "0"},
                                      {21, "750"},
                                      {0, "LINE"},
                                      {8, "Contour"},
                                      {10, "700"},
                                      {20, "750"},
                                      {11, "1400"},
                                      {21, "750"}});
    const Result<SvgRig> made = rig_from_dxf(text, {});
    REQUIRE(made);
    const SvgRig &rig = made.value();
    REQUIRE(rig.strips.size() == 2); // The frozen Photo layer's line is left out.
    CHECK(rig.width_m == doctest::Approx(1.4));
    CHECK(rig.height_m == doctest::Approx(0.75));
    const StripSpec &bottom = *find(rig, "Contour 1");
    CHECK(has(bottom, "contour"));
    CHECK(bottom.polyline.front().x == doctest::Approx(-0.7)); // As drawn, centred.
    CHECK(bottom.polyline.front().y == doctest::Approx(1.0));  // On base_m.
    CHECK(bottom.count == 84);
    const StripSpec &top = *find(rig, "Contour 2");
    CHECK(top.polyline.front().y == doctest::Approx(1.75)); // y up, unlike SVG.
    CHECK(top.polyline.front().x == doctest::Approx(0.0));
}

TEST_CASE("dxf rig: bulges, mirrored circles and arcs, splines, units") {
    // A polyline with a half-circle bulge; circles either side of the middle; an arc on the left;
    // a straight degree-1 spline; the drawing in centimetres.
    std::string text =
        drawing({{0, "LWPOLYLINE"}, {8, "Rings"},  {90, "2"},    {70, "0"}, {10, "40"},
                 {20, "0"},         {42, "1"},     {10, "60"},   {20, "0"}, {0, "CIRCLE"},
                 {8, "Tunnels"},    {10, "20"},    {20, "10"},   {40, "5"}, {0, "CIRCLE"},
                 {8, "Tunnels"},    {10, "80"},    {20, "10"},   {40, "5"}, {0, "ARC"},
                 {8, "Arcs"},       {10, "20"},    {20, "30"},   {40, "5"}, {50, "0"},
                 {51, "90"},        {0, "SPLINE"}, {8, "Lines"}, {71, "1"}, {40, "0"},
                 {40, "0"},         {40, "1"},     {40, "1"},    {10, "0"}, {20, "40"},
                 {10, "100"},       {20, "40"}});
    text.replace(text.find("\r\n4\r\n"), 5, "\r\n5\r\n"); // $INSUNITS 5: centimetres.
    const Result<SvgRig> made = rig_from_dxf(text, {0.0, 1.0, 1000.0});
    REQUIRE(made);
    const SvgRig &rig = made.value();
    CHECK(rig.width_m == doctest::Approx(1.0)); // 0 to 100 cm.

    const StripSpec &bow = *find(rig, "Rings 1");
    CHECK(length_m(bow) == doctest::Approx(kPi * 0.1).epsilon(0.001)); // A 20 cm half circle.

    const StripSpec &left = *find(rig, "Tunnels 1");
    const StripSpec &right = *find(rig, "Tunnels 2");
    CHECK(has(left, "left"));
    CHECK(has(right, "right"));
    // Mirror images: each starts at its point nearest the middle and runs the mirrored way.
    REQUIRE(left.polyline.size() == right.polyline.size());
    for (size_t i = 0; i < left.polyline.size(); i += 7) {
        CHECK(left.polyline[i].x == doctest::Approx(-right.polyline[i].x).epsilon(1e-6));
        CHECK(left.polyline[i].y == doctest::Approx(right.polyline[i].y).epsilon(1e-6));
    }
    CHECK(left.polyline.front().x == doctest::Approx(-0.25)); // 20 + 5 cm, from the middle.
    CHECK(length_m(left) == doctest::Approx(2.0 * kPi * 0.05).epsilon(0.001));

    // The arc (0 to 90 degrees, on the left) runs reversed: from its top end to its right end.
    const StripSpec &arc = *find(rig, "Arcs 1");
    CHECK(arc.polyline.front().y > arc.polyline.back().y);

    const StripSpec &line = *find(rig, "Lines 1");
    CHECK(length_m(line) == doctest::Approx(1.0));
    CHECK(line.polyline.front().x == doctest::Approx(-0.5));
}

TEST_CASE("dxf rig: binary files and files without entities are errors") {
    CHECK_FALSE(rig_from_dxf("AutoCAD Binary DXF\r\n\x1a", {}));
    CHECK_FALSE(rig_from_dxf(dxf({{0, "SECTION"}, {2, "HEADER"}, {0, "ENDSEC"}, {0, "EOF"}}), {}));
    CHECK_FALSE(rig_from_dxf(drawing({}), {})); // No shapes.
}

TEST_CASE("dxf rig: depth comes in: 3D lines, 3D polylines, elevated circles, 3D splines") {
    const std::string text = drawing(
        {{0, "LINE"},   {8, "Depth"}, {10, "0"},       {20, "0"},     {30, "0"},     {11, "0"},
         {21, "0"},     {31, "300"},  {0, "POLYLINE"}, {8, "Depth"},  {66, "1"},     {70, "8"},
         {0, "VERTEX"}, {10, "100"},  {20, "0"},       {30, "0"},     {0, "VERTEX"}, {10, "100"},
         {20, "0"},     {30, "400"},  {0, "SEQEND"},   {0, "CIRCLE"}, {8, "Depth"},  {10, "200"},
         {20, "0"},     {30, "150"},  {40, "50"},      {0, "SPLINE"}, {8, "Depth"},  {71, "1"},
         {40, "0"},     {40, "0"},    {40, "1"},       {40, "1"},     {10, "300"},   {20, "0"},
         {30, "0"},     {10, "300"},  {20, "0"},       {30, "500"}});
    const Result<SvgRig> made = rig_from_dxf(text, {0.0, 1.0, 1000.0});
    REQUIRE(made);
    const SvgRig &rig = made.value();
    REQUIRE(rig.strips.size() == 4);
    const StripSpec &line = rig.strips[0];
    CHECK(line.polyline.back().z == doctest::Approx(0.3));
    CHECK(line.count == 300); // 0.3 m deep at 1000 px/m: depth counts in the length.
    CHECK(rig.strips[1].polyline.back().z == doctest::Approx(0.4));
    for (const Vec3 &v : rig.strips[2].polyline) {
        CHECK(v.z == doctest::Approx(0.15)); // The circle at its elevation.
    }
    CHECK(rig.strips[3].polyline.back().z == doctest::Approx(0.5));
}
