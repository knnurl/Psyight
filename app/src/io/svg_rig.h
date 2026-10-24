// A rig from an SVG drawing (Rig > New rig > From SVG): every visible path, line, polyline,
// polygon, rect, circle and ellipse becomes strips, one per subpath, its pixels running the way
// the path was drawn (Inkscape's Path > Reverse turns a strip round). A strip's groups are the
// labels of the layers and labelled groups holding it (Inkscape's inkscape:label, lower case),
// plus "left" and/or "right" by its centre, as the presets'. Its name is the element's label or
// id, numbered per subpath when there are several.
//
// Size: the document's width and height in real units (mm, cm, m, in, pt, pc; px and no unit at
// 96 per inch) over its viewBox give metres; width_m above 0 scales the drawing to that width
// instead. Placement: x centred on 0, the drawing's bottom base_m above the floor, y up, z 0.
// Curves and arcs become short straight segments (at most about 5 mm in the drawing's own size,
// and at least 16 per curve and 64 per full turn of an arc, so a drawing scaled up stays smooth).
// Hidden elements (display:none, as Inkscape's hidden layers) and defs, clip paths, masks,
// patterns, markers and symbols are skipped; so are <use>, text and images. A subpath too short for
// one pixel is dropped and counted.
#pragma once

#include "core/result.h"
#include "engine/rig.h"

#include <string>
#include <string_view>
#include <vector>

namespace psyight {

struct SvgRigOptions {
    double width_m = 0.0; // 0: the drawing's own size.
    double base_m = 1.0;
    double pixels_per_m = 60.0;
    // Solid rings drawn as two outlines (a laser-cut ring's inner and outer edge): two closed
    // shapes, one inside the other with a steady gap of at most ring_width_m (as drawn), become
    // one strip along the middle, named and grouped as the outer one and running its way.
    bool centre_lines = false;
    double ring_width_m = 0.03;
};

struct SvgRig {
    std::vector<StripSpec> strips;
    double width_m = 0.0; // The drawing as placed.
    double height_m = 0.0;
    double natural_width_m = 0.0; // At the document's own size.
    int too_short = 0;            // Subpaths dropped: shorter than one pixel.
    int centre_lines = 0;         // Pairs of outlines made into one strip along their middle.
};

Result<SvgRig> rig_from_svg(std::string_view svg, const SvgRigOptions &options);

// A drawing's shape before placement (SVG and DXF readers): its points in metres at the
// drawing's own size, y up; its name and groups as the strip will have them.
struct DrawingShape {
    std::string name;
    std::vector<std::string> groups;
    std::vector<Vec3> points;
};

// Places shapes as the rig's strips: scaled to options.width_m (0: as drawn), x centred on 0,
// the lowest point base_m above the floor; left and right groups; too-short ones counted.
Result<SvgRig> rig_from_shapes(std::vector<DrawingShape> shapes, const SvgRigOptions &options);

} // namespace psyight
