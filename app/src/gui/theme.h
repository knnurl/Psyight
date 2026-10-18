// The one place for UI colours, sizes and fonts (docs/RESTYLE.md, Phase 1). No other file holds
// a UI colour: panels read tokens from theme() (LED colours, palettes and preview pixels are show
// data, not UI style, and stay with the engine).
//
// Two modes: Desk (editing, normal light) and Show (a dark room: lower contrast, dimmer accent).
// Every ImGuiCol and ImPlotCol is derived from the base tokens; the semantic tokens (states) and
// data tokens (plots, bass roll, sections) have defaults per mode. The accent is used only for the
// active mode, the playhead, selection, primary buttons and active toggles.
//
// Overrides: theme.json next to the executable, for example
//   {"borders": true, "desk": {"accent": "#F2A33A", "note_fill": "#40BEAAC8"}, "show": {...}}
// Colours are "#RRGGBB" or "#RRGGBBAA" under any token name below; unknown names, bad values or a
// file that does not parse are reported in the log and the defaults are kept.
//
// Fonts (assets/fonts, OFL and ISC licences beside them): Inter Regular for UI text with Lucide
// icons merged (U+E038 to U+E63A), Inter Medium, JetBrains Mono for numeric readouts, and a
// smaller Inter for plot axes. 16 px base, scaled by the DPI. Without the files the app falls back
// to ImGui's built-in font and says so in the log.
#pragma once

#include <imgui.h>

#include <filesystem>

namespace psyight {

enum class ThemeMode { desk, show };

struct ThemeTokens {
    // Base (RESTYLE Phase 1).
    ImVec4 bg, panel, frame, frame_hover, text, text_dim, accent;
    // States.
    ImVec4 danger, warning, success, info;
    // Data panels.
    ImVec4 playhead;
    ImVec4 loop, loop_shade, view_rect, waveform;
    ImVec4 note_fill, note_border, note_slide, selection_fill;
    ImVec4 detection, detection_border, kick_line, black_key_row;
    ImVec4 region_complete, region_incomplete;
    ImVec4 section_groove, section_breakdown, section_build, section_drop, section_other;
    ImVec4 section_boundary, section_text, phrase_tick, rest_shade, hat_tick, downbeat_handle;
    ImVec4 preview_bg;
    // ImPlot colormaps (ImPlotColormap_*) for the spectrogram and the bass roll's pitch background.
    int spectrogram_colormap = 0;
    int pitch_colormap = 0;
};

enum class ThemeFont { ui, ui_medium, mono, plot };

// Loads the fonts and theme.json; call once after ImGui::CreateContext, before the first frame.
// dpi_scale multiplies every size.
void init_theme(float dpi_scale);

// Applies a mode's colours and sizes to ImGui and ImPlot.
void apply_psyight_theme(ThemeMode mode);
ThemeMode theme_mode();
const ThemeTokens &theme();
ImFont *theme_font(ThemeFont font); // Null before init_theme (ImGui then uses its default).
inline ImU32 to_u32(const ImVec4 &c) { return ImGui::ColorConvertFloat4ToU32(c); }
inline ImVec4 with_alpha(ImVec4 c, float alpha) {
    c.w = alpha;
    return c;
}

// A button filled with a strong colour (the accent, danger), with dark text on it; pop with
// pop_filled_button().
void push_filled_button(const ImVec4 &fill);
void pop_filled_button();

// ImPlot::BeginPlot and EndPlot with the plots' smaller font (axis labels, legends, drawn text).
// ImPlot measures and draws tick labels in EndPlot, so the font stays pushed until then.
bool begin_plot(const char *title_id, const ImVec2 &size, int flags = 0);
void end_plot();
float plot_font_size(); // Pixels, after the DPI scale.

// F12 (dev): ImGui's and ImPlot's style editors.
void draw_style_editor(bool *open);

} // namespace psyight
