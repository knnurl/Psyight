// A rig from a DXF drawing (Rig > New rig > From drawing), as rig_from_svg does for SVG. Fusion
// (a sketch's Save As DXF), LibreCAD, KiCad and Inkscape write it. The ENTITIES section's LINE,
// LWPOLYLINE and POLYLINE (bulges: arcs between vertices), ARC, CIRCLE, ELLIPSE and SPLINE (a
// B-spline from its control points, knots and weights, or a line through its fit points) each
// become one strip. Lines and polylines run the way they were drawn. DXF stores arcs and circles
// anticlockwise, so those whose centre lies left of the drawing's middle are reversed, and a
// circle starts at its point nearest the middle (a centred one at its bottom): mirrored halves run
// from the centre outwards, as the presets' do.
//
// Groups: the entity's layer name in lower case (layer "0" adds none). Names: the layer and a
// number (or the entity type on layer "0"). Off and frozen layers are left out, as are blocks
// (INSERT), text, hatches and 3D meshes. Units: the header's $INSUNITS (inches, feet, mm, cm, m;
// unitless counts as mm). Curves become segments of at most about 5 mm and at least 64 a turn.
// Text DXF only: a binary DXF is an error.
#pragma once

#include "core/result.h"
#include "io/svg_rig.h"

#include <string_view>

namespace psyight {

Result<SvgRig> rig_from_dxf(std::string_view dxf, const SvgRigOptions &options);

} // namespace psyight
