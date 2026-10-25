#include "gui/stage_canvas.h"

#include "engine/strip_edit.h"
#include "gui/theme.h"
#include "gui/widgets.h"
#include "output/node_setup.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

namespace psyight {

namespace {

constexpr float kMarginPixels = 12.0f;
constexpr float kGrabPixels = 7.0f; // A handle or a strip this close is under the pointer.
constexpr double kSnapM = 0.01;     // Shift: a 1 cm grid.
constexpr double kRotateDeg = 15.0; // Q and E.
constexpr double kZoomStep = 1.15;  // Per wheel notch.
constexpr double kNewStripPerM = 60.0;
constexpr double kPi = 3.14159265358979323846;

// Node bytes are LED drive levels (gamma applied); the screen wants the frame's values back.
ImU32 to_screen_colour(const std::uint8_t *rgb, double gamma) {
    const auto channel = [gamma](std::uint8_t b) {
        return static_cast<int>(std::lround(255.0 * std::pow(b / 255.0, 1.0 / gamma)));
    };
    return IM_COL32(channel(rgb[0]), channel(rgb[1]), channel(rgb[2]), 255);
}

double length_m(const std::vector<Vec3> &p) {
    double length = 0.0;
    for (size_t i = 1; i < p.size(); ++i) {
        length += std::hypot(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y);
    }
    return length;
}

} // namespace

bool StageCanvas::set_view(std::string_view name) {
    constexpr std::string_view kNames[] = {"front", "top", "side", "orbit"};
    for (size_t i = 0; i < std::size(kNames); ++i) {
        if (name == kNames[i]) {
            view_ = static_cast<View>(i);
            return true;
        }
    }
    return false;
}

Vec3 StageCanvas::project(const Vec3 &q) const {
    switch (view_) {
    case View::front:
        return q;
    case View::top: // From above: the audience (+z) at the bottom; higher points nearer.
        return {q.x, -q.z, q.y};
    case View::side: // From the right (+x): the audience to the left.
        return {-q.z, q.y, q.x};
    case View::orbit:
    default: {
        const double x = q.x - centre_.x;
        const double y = q.y - centre_.y;
        const double z = q.z - centre_.z;
        const double x1 = x * std::cos(yaw_rad_) + z * std::sin(yaw_rad_);
        const double z1 = -x * std::sin(yaw_rad_) + z * std::cos(yaw_rad_);
        const double y2 = y * std::cos(pitch_rad_) - z1 * std::sin(pitch_rad_);
        const double z2 = y * std::sin(pitch_rad_) + z1 * std::cos(pitch_rad_);
        return {x1 + centre_.x, y2 + centre_.y, z2};
    }
    }
}

void StageCanvas::draw_toolbar(const Rig &rig) {
    static constexpr const char *kViews[] = {"Front", "Top", "Side", "Orbit"};
    int view = static_cast<int>(view_);
    ImGui::SetNextItemWidth(6.0f * ImGui::GetFontSize());
    if (ImGui::Combo("##view", &view, kViews, IM_ARRAYSIZE(kViews))) {
        view_ = static_cast<View>(view);
        zoom_ = 1.0;
        pan_x_m_ = pan_y_m_ = 0.0;
    }
    ImGui::SetItemTooltip("Front: as the audience sees it. Top, Side: the depth. Orbit: drag to "
                          "turn the rig.");
    ImGui::SameLine();
    ImGui::BeginDisabled(view_ != View::front);
    if (ImGui::Checkbox("Edit strips", &editing_) && !editing_) {
        drawing_ = false;
        new_points_.clear();
        selected_.clear();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Select, move, rotate, reshape, split, delete and draw strips here (in "
                          "the Front view).");
    ImGui::SameLine();
    if (ImGui::SmallButton("Fit view")) {
        zoom_ = 1.0;
        pan_x_m_ = pan_y_m_ = 0.0;
        yaw_rad_ = 0.6;
        pitch_rad_ = 0.35;
    }
    ImGui::SetItemTooltip("The whole rig (or double-click empty space). The wheel zooms, a drag "
                          "on empty space pans.");
    ImGui::SameLine();
    ImGui::TextDisabled("%.0f%%", 100.0 * zoom_);
    ImGui::SameLine();
    ImGui::Checkbox("Names", &show_names_);
    ImGui::SetItemTooltip("Strip names and data-in arrows while the pointer is over the view.");
    if (!editing_ || view_ != View::front) {
        return;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(drawing_ ? "Drawing... (Enter ends)" : "Draw strip")) {
        if (drawing_) {
            finish_drawing();
        } else {
            drawing_ = true;
            new_points_.clear();
        }
    }
    ImGui::SetItemTooltip("Click points (Shift snaps to 1 cm); double-click or Enter ends, Esc "
                          "cancels.");
    ImGui::BeginDisabled(selected_.empty());
    ImGui::SameLine();
    if (ImGui::SmallButton("Rotate -15")) {
        rotate_selection(kRotateDeg * kPi / 180.0);
    }
    ImGui::SetItemTooltip("Anticlockwise (Q).");
    ImGui::SameLine();
    if (ImGui::SmallButton("Rotate +15")) {
        rotate_selection(-kRotateDeg * kPi / 180.0);
    }
    ImGui::SetItemTooltip("Clockwise (E).");
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete")) {
        delete_selection();
    }
    ImGui::EndDisabled();
    // The selection: one strip's facts, or how many.
    const Project &p = project_.project();
    if (selected_.size() == 1 && selected_[0] < static_cast<int>(p.strips.size())) {
        const auto s = static_cast<size_t>(selected_[0]);
        const StripSpec &spec = p.strips[s].spec;
        std::string where;
        const NodePlan plan = plan_node(p);
        for (size_t l = 0; l < plan.lanes.size(); ++l) {
            const NodeLane &lane = plan.lanes[l];
            if (static_cast<int>(s) >= lane.first_strip &&
                static_cast<int>(s) < lane.first_strip + lane.strips) {
                where =
                    ", line " + std::to_string(l + 1) + " " + node_lane_pin(static_cast<int>(l));
            }
        }
        std::string groups;
        for (const std::string &g : spec.groups) {
            groups += (groups.empty() ? "; " : ", ") + g;
        }
        mono_text("%zu. %s: %d px, %.2f m at %.0f px/m%s%s", s + 1, spec.name.c_str(), spec.count,
                  length_m(spec.polyline), spec.pixels_per_m, groups.c_str(), where.c_str());
    } else if (selected_.size() > 1) {
        ImGui::TextDisabled("%zu strips selected", selected_.size());
    } else {
        ImGui::TextDisabled("Click a strip to select it; right-click for more.");
    }
    (void)rig;
}

void StageCanvas::rotate_selection(double angle_rad) {
    Project &p = project_.edit();
    double lo_x = 1e9, hi_x = -1e9, lo_y = 1e9, hi_y = -1e9;
    for (const int s : selected_) {
        for (const Vec3 &v : p.strips[static_cast<size_t>(s)].spec.polyline) {
            lo_x = std::min(lo_x, v.x);
            hi_x = std::max(hi_x, v.x);
            lo_y = std::min(lo_y, v.y);
            hi_y = std::max(hi_y, v.y);
        }
    }
    const Vec3 centre{0.5 * (lo_x + hi_x), 0.5 * (lo_y + hi_y), 0.0};
    for (const int s : selected_) {
        StripSpec &spec = p.strips[static_cast<size_t>(s)].spec;
        spec = rotated(spec, centre, angle_rad);
    }
    project_.changed();
}

void StageCanvas::delete_selection() {
    std::vector<int> doomed = selected_;
    std::sort(doomed.rbegin(), doomed.rend());
    Project &p = project_.edit();
    for (const int s : doomed) {
        if (s >= 0 && s < static_cast<int>(p.strips.size()) && p.strips.size() > 1) {
            p.strips.erase(p.strips.begin() + s);
        }
    }
    selected_.clear();
    project_.changed();
}

void StageCanvas::finish_drawing() {
    drawing_ = false;
    if (new_points_.size() < 2) {
        new_points_.clear();
        return;
    }
    Project &p = project_.edit();
    StripSpec spec;
    spec.name = "Strip " + std::to_string(p.strips.size() + 1);
    spec.polyline = new_points_;
    spec.pixels_per_m = kNewStripPerM;
    spec.count = std::max(1, static_cast<int>(std::floor(length_m(spec.polyline) * kNewStripPerM)));
    const ColourOrder order = p.strips.empty() ? ColourOrder::grb : p.strips.front().order;
    p.strips.push_back({spec, order});
    selected_ = {static_cast<int>(p.strips.size()) - 1};
    new_points_.clear();
    project_.changed();
}

void StageCanvas::edit_menu() {
    if (!ImGui::BeginPopup("##strip menu")) {
        return;
    }
    Project &p = project_.edit();
    if (menu_strip_ < 0 || menu_strip_ >= static_cast<int>(p.strips.size())) {
        if (ImGui::MenuItem("Draw strip here")) {
            drawing_ = true;
            new_points_ = {menu_at_};
        }
        if (ImGui::MenuItem("Fit view")) {
            zoom_ = 1.0;
            pan_x_m_ = pan_y_m_ = 0.0;
        }
        ImGui::EndPopup();
        return;
    }
    const auto s = static_cast<size_t>(menu_strip_);
    StripSpec &spec = p.strips[s].spec;
    ImGui::TextDisabled("%s", spec.name.c_str());
    ImGui::Separator();
    bool changed = false;
    if (ImGui::MenuItem("Reverse (data in at the other end)")) {
        std::reverse(spec.polyline.begin(), spec.polyline.end());
        changed = true;
    }
    if (ImGui::MenuItem(is_closed(spec) ? "Start the ring here" : "Split here")) {
        std::vector<StripSpec> parts = split_strip(spec, menu_segment_, menu_at_);
        const ColourOrder order = p.strips[s].order;
        p.strips.erase(p.strips.begin() + static_cast<std::ptrdiff_t>(s));
        for (size_t k = 0; k < parts.size(); ++k) {
            p.strips.insert(p.strips.begin() + static_cast<std::ptrdiff_t>(s + k),
                            {std::move(parts[k]), order});
        }
        selected_.clear();
        changed = true;
    }
    if (ImGui::MenuItem("Add a point here")) {
        spec = with_point_inserted(spec, menu_segment_, menu_at_);
        changed = true;
    }
    if (ImGui::MenuItem("Remove this point", nullptr, false, menu_point_ >= 0)) {
        spec = with_point_removed(spec, static_cast<size_t>(menu_point_));
        changed = true;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Delete the strip", nullptr, false, p.strips.size() > 1)) {
        p.strips.erase(p.strips.begin() + static_cast<std::ptrdiff_t>(s));
        selected_.clear();
        changed = true;
    }
    if (changed) {
        project_.changed();
    }
    ImGui::EndPopup();
}

void StageCanvas::draw(const Rig &shown, const std::vector<std::uint8_t> &node_rgb, double gamma,
                       int highlighted_strip, bool tools) {
    const bool edit = editing_ && tools && view_ == View::front;
    const Rig &rig = edit ? project_.rig() : shown;
    if (tools) {
        draw_toolbar(rig);
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 2.0f * kMarginPixels || size.y < 2.0f * kMarginPixels) {
        return;
    }
    ImGui::InvisibleButton("##stage canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                        to_u32(theme().preview_bg));

    // The fit: the whole rig in the view at zoom 1 (its projected extent); frozen while
    // dragging.
    if (drag_ == Drag::none) {
        centre_ = {0.5 * (rig.min.x + rig.max.x), 0.5 * (rig.min.y + rig.max.y),
                   0.5 * (rig.min.z + rig.max.z)};
        double lo_u = 1e9, hi_u = -1e9, lo_v = 1e9, hi_v = -1e9;
        const auto extend = [&](const Vec3 &q) {
            const Vec3 p = project(q);
            lo_u = std::min(lo_u, p.x);
            hi_u = std::max(hi_u, p.x);
            lo_v = std::min(lo_v, p.y);
            hi_v = std::max(hi_v, p.y);
        };
        for (const Pixel &px : rig.pixels) {
            extend(px.position);
        }
        for (const StripSpec &s : rig.strips) {
            for (const Vec3 &v : s.polyline) {
                extend(v);
            }
        }
        if (lo_u > hi_u) {
            lo_u = hi_u = lo_v = hi_v = 0.0;
        }
        const double width_m = std::max(1e-3, hi_u - lo_u);
        const double height_m = std::max(1e-3, hi_v - lo_v);
        fit_scale_ = std::min((size.x - 2.0f * kMarginPixels) / width_m,
                              (size.y - 2.0f * kMarginPixels) / height_m);
        mid_x_m_ = 0.5 * (lo_u + hi_u);
        mid_y_m_ = 0.5 * (lo_v + hi_v);
    }
    origin_x_ = origin.x;
    origin_y_ = origin.y;
    size_x_ = size.x;
    size_y_ = size.y;
    const double scale = fit_scale_ * zoom_;
    const float cx = origin.x + 0.5f * size.x;
    const float cy = origin.y + 0.5f * size.y;
    const auto screen = [&](const Vec3 &world_point) {
        const Vec3 q = project(world_point);
        return ImVec2(cx + static_cast<float>((q.x - mid_x_m_ - pan_x_m_) * scale),
                      cy - static_cast<float>((q.y - mid_y_m_ - pan_y_m_) * scale));
    };
    // The view's point under a screen position (the rig's x and y in the Front view).
    const auto world = [&](const ImVec2 &m) {
        return Vec3{(m.x - cx) / scale + mid_x_m_ + pan_x_m_,
                    -(m.y - cy) / scale + mid_y_m_ + pan_y_m_, 0.0};
    };
    const float radius = std::max(1.5f, static_cast<float>(0.45 * scale / 60.0));
    const ImVec2 mouse = ImGui::GetMousePos();
    const ImGuiIO &io = ImGui::GetIO();

    // Zoom about the pointer.
    if (hovered && io.MouseWheel != 0.0f) {
        const Vec3 before = world(mouse);
        zoom_ = std::clamp(zoom_ * std::pow(kZoomStep, io.MouseWheel), 0.2, 50.0);
        const double new_scale = fit_scale_ * zoom_;
        pan_x_m_ += before.x - ((mouse.x - cx) / new_scale + mid_x_m_ + pan_x_m_);
        pan_y_m_ += before.y - (-(mouse.y - cy) / new_scale + mid_y_m_ + pan_y_m_);
    }
    // Pan with the middle button anywhere.
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
        pan_x_m_ -= io.MouseDelta.x / scale;
        pan_y_m_ += io.MouseDelta.y / scale;
    }

    // The pixels: the show's colours where they match the rig drawn, else dim.
    const bool colours = node_rgb.size() >= 3 * rig.pixels.size() &&
                         (&rig == &shown || shown.pixels.size() == rig.pixels.size());
    // Far pixels first; in the Orbit view also dimmer.
    std::vector<size_t> order(rig.pixels.size());
    std::vector<double> depth(rig.pixels.size());
    double nearest = -1e9;
    double farthest = 1e9;
    for (size_t i = 0; i < rig.pixels.size(); ++i) {
        order[i] = i;
        depth[i] = project(rig.pixels[i].position).z;
        nearest = std::max(nearest, depth[i]);
        farthest = std::min(farthest, depth[i]);
    }
    if (view_ != View::front) {
        std::stable_sort(order.begin(), order.end(),
                         [&](size_t a, size_t b) { return depth[a] < depth[b]; });
    }
    for (const size_t i : order) {
        ImU32 colour = colours ? to_screen_colour(&node_rgb[3 * i], gamma)
                               : to_u32(with_alpha(theme().text_dim, 0.35f));
        if (view_ == View::orbit && nearest > farthest) {
            const float shade =
                0.45f + 0.55f * static_cast<float>((depth[i] - farthest) / (nearest - farthest));
            const ImVec4 c = ImGui::ColorConvertU32ToFloat4(colour);
            colour =
                ImGui::ColorConvertFloat4ToU32(ImVec4(c.x * shade, c.y * shade, c.z * shade, 1.0f));
        }
        draw->AddCircleFilled(screen(rig.pixels[i].position), radius, colour);
    }

    // The strip under the pointer: by its pixels, or (editing) its path.
    hovered_strip_ = -1;
    if (hovered) {
        float best = std::max(8.0f, 3.0f * radius);
        best *= best;
        for (const Pixel &p : rig.pixels) {
            const ImVec2 at = screen(p.position);
            const float d =
                (at.x - mouse.x) * (at.x - mouse.x) + (at.y - mouse.y) * (at.y - mouse.y);
            if (d < best) {
                best = d;
                hovered_strip_ = p.strip;
            }
        }
        if (edit) {
            double best_m = kGrabPixels / scale;
            for (size_t s = 0; s < rig.strips.size(); ++s) {
                const PolylineHit hit = nearest_on_polyline(rig.strips[s].polyline, world(mouse));
                if (hit.distance_m < best_m) {
                    best_m = hit.distance_m;
                    hovered_strip_ = static_cast<int>(s);
                }
            }
        }
    }

    if (edit) {
        // Every path, the selection's in the accent with its points as handles.
        for (size_t s = 0; s < rig.strips.size(); ++s) {
            const bool chosen = std::find(selected_.begin(), selected_.end(),
                                          static_cast<int>(s)) != selected_.end();
            const std::vector<Vec3> &line = rig.strips[s].polyline;
            const ImU32 colour =
                to_u32(chosen ? theme().accent
                              : with_alpha(theme().text,
                                           static_cast<int>(s) == hovered_strip_ ? 0.8f : 0.35f));
            for (size_t i = 1; i < line.size(); ++i) {
                draw->AddLine(screen(line[i - 1]), screen(line[i]), colour, chosen ? 2.0f : 1.0f);
            }
            if (chosen) {
                for (const Vec3 &v : line) {
                    const ImVec2 at = screen(v);
                    draw->AddRectFilled(ImVec2(at.x - 3.5f, at.y - 3.5f),
                                        ImVec2(at.x + 3.5f, at.y + 3.5f), colour);
                }
            }
        }
        if (drawing_ && !new_points_.empty()) {
            const ImU32 colour = to_u32(theme().accent);
            for (size_t i = 1; i < new_points_.size(); ++i) {
                draw->AddLine(screen(new_points_[i - 1]), screen(new_points_[i]), colour, 2.0f);
            }
            if (hovered) {
                Vec3 next = world(mouse);
                if (io.KeyShift) {
                    next = snapped(next, kSnapM);
                }
                draw->AddLine(screen(new_points_.back()), screen(next),
                              to_u32(with_alpha(theme().accent, 0.5f)), 1.0f);
            }
        }
        handle_edit(rig);
    } else if (hovered || drag_ == Drag::orbit) {
        // Not editing: a drag pans (in the Orbit view, turns the rig); a double-click fits.
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            if (view_ == View::orbit) {
                drag_ = Drag::orbit; // The fit stays put while turning.
                yaw_rad_ += 0.01 * io.MouseDelta.x;
                pitch_rad_ = std::clamp(pitch_rad_ + 0.01 * io.MouseDelta.y, -1.5, 1.5);
            } else {
                pan_x_m_ -= io.MouseDelta.x / scale;
                pan_y_m_ += io.MouseDelta.y / scale;
            }
        }
        if (drag_ == Drag::orbit && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            drag_ = Drag::none;
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            zoom_ = 1.0;
            pan_x_m_ = pan_y_m_ = 0.0;
            yaw_rad_ = 0.6;
            pitch_rad_ = 0.35;
        }
    }

    // The outlined strip: the one under the pointer, else the Rig tab's hovered row.
    const int outlined = hovered_strip_ >= 0 ? hovered_strip_ : highlighted_strip;
    if (outlined >= 0) {
        for (const Pixel &p : rig.pixels) {
            if (p.strip == outlined) {
                draw->AddCircle(screen(p.position), radius + 2.0f, to_u32(theme().accent), 0, 1.5f);
            }
        }
    }
    // Hovering: each strip's data-in arrow and name (unless hidden).
    if (hovered) {
        ImFont *font = theme_font(ThemeFont::plot);
        const float font_px = font != nullptr ? font->FontSize : ImGui::GetFontSize();
        for (size_t s = 0; show_names_ && s < rig.strips.size(); ++s) {
            const std::vector<Vec3> &line = rig.strips[s].polyline;
            if (line.size() < 2) {
                continue;
            }
            const ImVec2 p0 = screen(line[0]);
            const ImVec2 p1 = screen(line[1]);
            const float len = std::hypot(p1.x - p0.x, p1.y - p0.y);
            const ImVec2 dir =
                len > 1e-3f ? ImVec2((p1.x - p0.x) / len, (p1.y - p0.y) / len) : ImVec2(1.0f, 0.0f);
            ImVec2 perp(-dir.y, dir.x);
            if (perp.x * (cx - p0.x) + perp.y * (cy - p0.y) < 0.0f) {
                perp = ImVec2(-perp.x, -perp.y);
            }
            const float a = 3.0f * radius + 4.0f;
            const ImU32 colour =
                to_u32(static_cast<int>(s) == outlined ? theme().accent : theme().text);
            draw->AddTriangleFilled(ImVec2(p0.x + dir.x * a, p0.y + dir.y * a),
                                    ImVec2(p0.x - dir.x * 0.4f * a + perp.x * 0.6f * a,
                                           p0.y - dir.y * 0.4f * a + perp.y * 0.6f * a),
                                    ImVec2(p0.x - dir.x * 0.4f * a - perp.x * 0.6f * a,
                                           p0.y - dir.y * 0.4f * a - perp.y * 0.6f * a),
                                    colour);
            const char *name = rig.strips[s].name.c_str();
            const float text_w =
                font != nullptr ? font->CalcTextSizeA(font_px, FLT_MAX, 0.0f, name).x : 0.0f;
            const float tx =
                std::clamp(p0.x + perp.x * a + 2.0f, origin.x + 2.0f,
                           std::max(origin.x + 2.0f, origin.x + size.x - text_w - 2.0f));
            const float ty =
                std::clamp(p0.y + perp.y * a - 0.5f * font_px, origin.y + 2.0f,
                           std::max(origin.y + 2.0f, origin.y + size.y - font_px - 2.0f));
            draw->AddText(font, font_px, ImVec2(tx, ty), colour, name);
        }
        if (hovered_strip_ >= 0 && hovered_strip_ < static_cast<int>(rig.strips.size()) &&
            drag_ == Drag::none && !ImGui::IsPopupOpen("##strip menu")) {
            const StripSpec &spec = rig.strips[static_cast<size_t>(hovered_strip_)];
            std::string groups;
            for (const std::string &g : spec.groups) {
                groups += (groups.empty() ? "" : ", ") + g;
            }
            ImGui::SetTooltip("%s: %d pixels, %.0f per m%s%s. The arrow marks its first pixel.",
                              spec.name.c_str(), spec.count, spec.pixels_per_m,
                              groups.empty() ? "" : "; groups ", groups.c_str());
        }
    }
    draw->PopClipRect();
    edit_menu();
}

void StageCanvas::handle_edit(const Rig &rig) {
    const ImGuiIO &io = ImGui::GetIO();
    const double scale = fit_scale_ * zoom_;
    const float cx = origin_x_ + 0.5f * size_x_;
    const float cy = origin_y_ + 0.5f * size_y_;
    const auto screen = [&](const Vec3 &q) {
        return ImVec2(cx + static_cast<float>((q.x - mid_x_m_ - pan_x_m_) * scale),
                      cy - static_cast<float>((q.y - mid_y_m_ - pan_y_m_) * scale));
    };
    const auto world = [&](const ImVec2 &m) {
        return Vec3{(m.x - cx) / scale + mid_x_m_ + pan_x_m_,
                    -(m.y - cy) / scale + mid_y_m_ + pan_y_m_, 0.0};
    };
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool hovered = ImGui::IsItemHovered();
    Vec3 at = world(mouse);
    if (io.KeyShift) {
        at = snapped(at, kSnapM);
    }
    // The handle under the pointer, among the selection's points.
    int handle_strip = -1;
    int handle_point = -1;
    float best = kGrabPixels * kGrabPixels;
    for (const int s : selected_) {
        if (s >= static_cast<int>(rig.strips.size())) {
            continue;
        }
        const std::vector<Vec3> &line = rig.strips[static_cast<size_t>(s)].polyline;
        for (size_t i = 0; i < line.size(); ++i) {
            const ImVec2 p = screen(line[i]);
            const float d = (p.x - mouse.x) * (p.x - mouse.x) + (p.y - mouse.y) * (p.y - mouse.y);
            if (d < best) {
                best = d;
                handle_strip = s;
                handle_point = static_cast<int>(i);
            }
        }
    }

    // Keys.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput) {
        if (drawing_ &&
            (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
            finish_drawing();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (drawing_) {
                drawing_ = false;
                new_points_.clear();
            } else {
                selected_.clear();
            }
        }
        if (!selected_.empty() && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            delete_selection();
        }
        if (!selected_.empty() && ImGui::IsKeyPressed(ImGuiKey_Q)) {
            rotate_selection(kRotateDeg * kPi / 180.0);
        }
        if (!selected_.empty() && ImGui::IsKeyPressed(ImGuiKey_E)) {
            rotate_selection(-kRotateDeg * kPi / 180.0);
        }
    }

    // Drawing a new strip: each click a point, a double-click ends it.
    if (drawing_) {
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            finish_drawing();
        } else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            new_points_.push_back(at);
        }
        return;
    }

    // Press: a handle, a strip (select, then move) or empty space (pan, and deselect).
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        drag_start_x_m_ = world(mouse).x;
        drag_start_y_m_ = world(mouse).y;
        if (handle_strip >= 0) {
            drag_ = Drag::point;
            drag_strip_ = handle_strip;
            drag_point_ = handle_point;
        } else if (hovered_strip_ >= 0) {
            const auto found = std::find(selected_.begin(), selected_.end(), hovered_strip_);
            if (io.KeyCtrl) {
                if (found != selected_.end()) {
                    selected_.erase(found);
                } else {
                    selected_.push_back(hovered_strip_);
                }
            } else if (found == selected_.end()) {
                selected_ = {hovered_strip_};
            }
            drag_ = Drag::strips;
            drag_originals_.clear();
            for (const int s : selected_) {
                drag_originals_.push_back(project_.project().strips[static_cast<size_t>(s)].spec);
            }
        } else {
            if (!io.KeyCtrl) {
                selected_.clear();
            }
            drag_ = Drag::pan;
        }
    }
    if (hovered && drag_ == Drag::pan && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        zoom_ = 1.0;
        pan_x_m_ = pan_y_m_ = 0.0;
    }
    // Dragging.
    if (drag_ != Drag::none && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        Project &p = project_.edit();
        if (drag_ == Drag::pan) {
            pan_x_m_ -= io.MouseDelta.x / scale;
            pan_y_m_ += io.MouseDelta.y / scale;
        } else if (drag_ == Drag::point &&
                   io.MouseDelta.x * io.MouseDelta.x + io.MouseDelta.y * io.MouseDelta.y > 0.0f) {
            StripSpec &spec = p.strips[static_cast<size_t>(drag_strip_)].spec;
            Vec3 to = at;
            to.z = spec.polyline[static_cast<size_t>(drag_point_)].z;
            spec = with_point_moved(spec, static_cast<size_t>(drag_point_), to);
            project_.changed();
        } else if (drag_ == Drag::strips && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            double dx = world(mouse).x - drag_start_x_m_;
            double dy = world(mouse).y - drag_start_y_m_;
            if (io.KeyShift) {
                dx = std::round(dx / kSnapM) * kSnapM;
                dy = std::round(dy / kSnapM) * kSnapM;
            }
            for (size_t k = 0; k < selected_.size() && k < drag_originals_.size(); ++k) {
                p.strips[static_cast<size_t>(selected_[k])].spec =
                    moved(drag_originals_[k], dx, dy);
            }
            project_.changed();
        }
    }
    if (drag_ != Drag::none && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        drag_ = Drag::none;
        drag_originals_.clear();
    }

    // Right-click: the strip's menu (or empty space's).
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        menu_strip_ = hovered_strip_;
        menu_point_ = -1;
        menu_at_ = at;
        if (menu_strip_ >= 0) {
            const StripSpec &spec = rig.strips[static_cast<size_t>(menu_strip_)];
            const PolylineHit hit = nearest_on_polyline(spec.polyline, world(mouse));
            menu_segment_ = hit.segment;
            menu_at_ = hit.point;
            for (size_t i = 0; i < spec.polyline.size(); ++i) {
                const ImVec2 p = screen(spec.polyline[i]);
                if ((p.x - mouse.x) * (p.x - mouse.x) + (p.y - mouse.y) * (p.y - mouse.y) <
                    kGrabPixels * kGrabPixels) {
                    menu_point_ = static_cast<int>(i);
                }
            }
            if (std::find(selected_.begin(), selected_.end(), menu_strip_) == selected_.end()) {
                selected_ = {menu_strip_};
            }
        }
        ImGui::OpenPopup("##strip menu");
    }
}

} // namespace psyight
