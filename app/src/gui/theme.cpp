#include "gui/theme.h"

#include "core/paths.h"

#include <implot.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <windows.h>

#include <array>
#include <charconv>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace psyight {

namespace {

constexpr float kBaseFontPx = 16.0f;
constexpr float kMonoFontPx = 15.0f; // JetBrains Mono at 15 px sits level with Inter at 16.
constexpr float kPlotFontPx = 13.0f;
const ImWchar kLucideRange[] = {0xE038, 0xE63A, 0}; // Every Lucide 0.460 icon.

// "#RRGGBB" or "#RRGGBBAA".
ImVec4 hex(const char *s) {
    const std::string_view text(s);
    float channel[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const size_t count = text.size() == 9 ? 4 : 3;
    for (size_t i = 0; i < count; ++i) {
        unsigned value = 0;
        const char *first = s + 1 + 2 * i;
        if (text.size() < 1 + 2 * count ||
            std::from_chars(first, first + 2, value, 16).ptr != first + 2) {
            return {1.0f, 0.0f, 1.0f, 1.0f}; // Magenta: a bad built-in value shows at once.
        }
        channel[i] = static_cast<float>(value) / 255.0f;
    }
    return {channel[0], channel[1], channel[2], channel[3]};
}

ImVec4 mix(const ImVec4 &a, const ImVec4 &b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t};
}

ImVec4 dim(ImVec4 c, float f) { return {c.x * f, c.y * f, c.z * f, c.w}; }

// Data panel tokens derived from a mode's base tokens (RESTYLE Phase 2).
void data_tokens(ThemeTokens &t) {
    // Notes: the user's are filled with the accent; detections are neutral, about 40% opaque
    // (the bass roll scales it by confidence); a selected note gets a bright outline.
    t.note_fill = with_alpha(t.accent, 0.85f);
    t.note_border = mix(t.accent, t.text, 0.35f);
    t.note_slide = t.accent;
    t.selection_fill = with_alpha(t.accent, 0.12f);
    t.selected_outline = mix(t.text, ImVec4(1, 1, 1, 1), 0.6f);
    t.detection = t.text_dim;
    t.detection_border = with_alpha(t.text_dim, 0.55f);
    t.kick_line = with_alpha(t.text, 0.10f);
    t.black_key_row = with_alpha(t.bg, 0.5f);
    // Lanes: the plot background is darker than the panel; white-key rows a little lighter than
    // it, black-key rows darker; over the pitch background, a light black-key shade.
    const ImVec4 plot_bg = mix(t.panel, t.bg, 0.7f);
    t.roll_white_row = mix(plot_bg, t.frame, 0.35f);
    t.roll_black_row = mix(plot_bg, ImVec4(0, 0, 0, 1), 0.25f);
    t.roll_black_overlay = ImVec4(0, 0, 0, 0.18f);
    t.roll_c_line = with_alpha(t.text, 0.28f);
    t.pitch_low = plot_bg;
    t.pitch_high = mix(t.text_dim, t.text, 0.5f);
    t.waveform = with_alpha(t.text_dim, 0.6f);
    t.waveform_played = with_alpha(mix(t.text_dim, t.text, 0.4f), 0.9f); // Slightly brighter.
    // Band energy: ImPlot's default line colours, desaturated towards the neutral text so they sit
    // on the background but stay apart.
    const ImVec4 deep[5] = {hex("#4C72B0"), hex("#DD8452"), hex("#55A868"), hex("#C44E52"),
                            hex("#8172B3")};
    ImVec4 *bands[5] = {&t.band_sub, &t.band_bass, &t.band_low_mid, &t.band_click, &t.band_hats};
    for (int i = 0; i < 5; ++i) {
        *bands[i] = mix(deep[i], t.text_dim, 0.35f);
    }
}

ThemeTokens desk_tokens() {
    ThemeTokens t;
    t.bg = hex("#1E222A");
    t.panel = hex("#282C34");
    t.frame = hex("#31363F");
    t.frame_hover = hex("#3A404B");
    t.text = hex("#D7DAE0");
    t.text_dim = hex("#8B919C");
    t.accent = hex("#F2A33A");
    t.danger = hex("#E06C75");  // One Dark's red, yellow, green and blue: they suit the base and
    t.warning = hex("#E5C07B"); // stay clear of the accent.
    t.success = hex("#98C379");
    t.info = hex("#61AFEF");
    t.playhead = t.accent;
    t.loop = t.info;
    t.loop_shade = with_alpha(t.info, 0.06f);
    t.view_rect = with_alpha(t.text, 0.25f);
    data_tokens(t);
    // Section, region and analysis overlay colours as before the restyle.
    t.region_complete = hex("#50C8781C");
    t.region_incomplete = hex("#C8C8C812");
    t.section_groove = hex("#28968ABE");
    t.section_breakdown = hex("#3C5AC8BE");
    t.section_build = hex("#E68C28BE");
    t.section_drop = hex("#DC3C3CC8");
    t.section_other = hex("#787878AA");
    t.section_boundary = hex("#000000AA");
    t.section_text = hex("#FFFFFFEB");
    t.phrase_tick = hex("#FFFFFFDC");
    t.rest_shade = hex("#AA6EFF2D");
    t.hat_tick = hex("#FFFFFFC8");
    t.downbeat_handle = t.danger;
    t.preview_bg = hex("#08080CFF");
    t.spectrogram_colormap = Colormap::magma;

    return t;
}

ThemeTokens show_tokens() {
    ThemeTokens t = desk_tokens();
    const ImVec4 desk_bands[5] = {t.band_sub, t.band_bass, t.band_low_mid, t.band_click,
                                  t.band_hats};
    t.bg = hex("#121316");
    t.panel = hex("#1A1C20");
    t.frame = hex("#24272C");
    t.frame_hover = hex("#2C3036");
    t.text = hex("#B4B8C0");
    t.text_dim = hex("#6E737C");
    t.accent = hex("#C98A33");
    data_tokens(t);               // From the Show base tokens.
    constexpr float kDim = 0.75f; // Everything else, dimmer for a dark room.
    for (ImVec4 *c :
         {&t.danger, &t.warning, &t.success, &t.info, &t.region_complete, &t.section_groove,
          &t.section_breakdown, &t.section_build, &t.section_drop, &t.section_other,
          &t.section_text, &t.phrase_tick, &t.rest_shade, &t.hat_tick}) {
        *c = dim(*c, kDim);
    }
    ImVec4 *bands[5] = {&t.band_sub, &t.band_bass, &t.band_low_mid, &t.band_click, &t.band_hats};
    for (int i = 0; i < 5; ++i) {
        *bands[i] = dim(desk_bands[i], kDim);
    }
    t.playhead = t.accent;
    t.loop = t.info;
    t.loop_shade = with_alpha(t.info, 0.06f);
    t.view_rect = with_alpha(t.text, 0.2f);
    t.downbeat_handle = t.danger;
    return t;
}

// Every token by name, for theme.json.
const std::vector<std::pair<const char *, ImVec4 ThemeTokens::*>> &token_names() {
    static const std::vector<std::pair<const char *, ImVec4 ThemeTokens::*>> names = {
        {"bg", &ThemeTokens::bg},
        {"panel", &ThemeTokens::panel},
        {"frame", &ThemeTokens::frame},
        {"frame_hover", &ThemeTokens::frame_hover},
        {"text", &ThemeTokens::text},
        {"text_dim", &ThemeTokens::text_dim},
        {"accent", &ThemeTokens::accent},
        {"danger", &ThemeTokens::danger},
        {"warning", &ThemeTokens::warning},
        {"success", &ThemeTokens::success},
        {"info", &ThemeTokens::info},
        {"playhead", &ThemeTokens::playhead},
        {"loop", &ThemeTokens::loop},
        {"loop_shade", &ThemeTokens::loop_shade},
        {"view_rect", &ThemeTokens::view_rect},
        {"waveform", &ThemeTokens::waveform},
        {"note_fill", &ThemeTokens::note_fill},
        {"note_border", &ThemeTokens::note_border},
        {"note_slide", &ThemeTokens::note_slide},
        {"selection_fill", &ThemeTokens::selection_fill},
        {"selected_outline", &ThemeTokens::selected_outline},
        {"roll_white_row", &ThemeTokens::roll_white_row},
        {"roll_black_row", &ThemeTokens::roll_black_row},
        {"roll_black_overlay", &ThemeTokens::roll_black_overlay},
        {"roll_c_line", &ThemeTokens::roll_c_line},
        {"waveform_played", &ThemeTokens::waveform_played},
        {"band_sub", &ThemeTokens::band_sub},
        {"band_bass", &ThemeTokens::band_bass},
        {"band_low_mid", &ThemeTokens::band_low_mid},
        {"band_click", &ThemeTokens::band_click},
        {"band_hats", &ThemeTokens::band_hats},
        {"detection", &ThemeTokens::detection},
        {"detection_border", &ThemeTokens::detection_border},
        {"kick_line", &ThemeTokens::kick_line},
        {"black_key_row", &ThemeTokens::black_key_row},
        {"region_complete", &ThemeTokens::region_complete},
        {"region_incomplete", &ThemeTokens::region_incomplete},
        {"section_groove", &ThemeTokens::section_groove},
        {"section_breakdown", &ThemeTokens::section_breakdown},
        {"section_build", &ThemeTokens::section_build},
        {"section_drop", &ThemeTokens::section_drop},
        {"section_other", &ThemeTokens::section_other},
        {"section_boundary", &ThemeTokens::section_boundary},
        {"section_text", &ThemeTokens::section_text},
        {"phrase_tick", &ThemeTokens::phrase_tick},
        {"rest_shade", &ThemeTokens::rest_shade},
        {"hat_tick", &ThemeTokens::hat_tick},
        {"downbeat_handle", &ThemeTokens::downbeat_handle},
        {"preview_bg", &ThemeTokens::preview_bg},
        {"pitch_low", &ThemeTokens::pitch_low},
        {"pitch_high", &ThemeTokens::pitch_high},
    };
    return names;
}

struct State {
    float dpi = 1.0f;
    bool borders = false;
    ThemeMode mode = ThemeMode::desk;
    ThemeTokens desk = desk_tokens();
    ThemeTokens show = show_tokens();
    std::array<ImFont *, 4> fonts{};
};

State &state() {
    static State s;
    return s;
}

std::filesystem::path exe_dir() {
    wchar_t buffer[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return n > 0 ? std::filesystem::path(std::wstring(buffer, n)).parent_path()
                 : std::filesystem::current_path();
}

bool parse_colour(const nlohmann::json &v, ImVec4 &out) {
    if (!v.is_string()) {
        return false;
    }
    const std::string s = v.get<std::string>();
    if ((s.size() != 7 && s.size() != 9) || s[0] != '#' ||
        s.find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos) {
        return false;
    }
    out = hex(s.size() == 7 ? (s + "FF").c_str() : s.c_str());
    return true;
}

void load_overrides(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return;
    }
    const nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        spdlog::warn("theme.json does not parse; using the built-in theme");
        return;
    }
    State &st = state();
    if (const auto b = root.find("borders"); b != root.end() && b->is_boolean()) {
        st.borders = b->get<bool>();
    }
    for (auto &[mode, tokens] : {std::pair<const char *, ThemeTokens *>{"desk", &st.desk},
                                 std::pair<const char *, ThemeTokens *>{"show", &st.show}}) {
        const auto m = root.find(mode);
        if (m == root.end() || !m->is_object()) {
            continue;
        }
        for (const auto &[key, value] : m->items()) {
            bool known = false;
            for (const auto &[name, member] : token_names()) {
                if (key == name) {
                    known = true;
                    if (!parse_colour(value, tokens->*member)) {
                        spdlog::warn("theme.json: {}.{} is not #RRGGBB or #RRGGBBAA", mode, key);
                    }
                }
            }
            if (!known) {
                spdlog::warn("theme.json: unknown token {}.{}", mode, key);
            }
        }
    }
    spdlog::info("Theme overrides from {}", path_to_utf8(path));
}

void load_fonts(float dpi) {
    ImGuiIO &io = ImGui::GetIO();
    std::filesystem::path dir = exe_dir() / "assets" / "fonts";
    if (!std::filesystem::exists(dir / "Inter-Regular.ttf")) {
        dir = std::filesystem::current_path() / "assets" / "fonts";
    }
    const auto file = [&](const char *name) { return path_to_utf8(dir / name); };
    for (const char *name :
         {"Inter-Regular.ttf", "Inter-Medium.ttf", "JetBrainsMono-Regular.ttf", "Lucide.ttf"}) {
        if (!std::filesystem::exists(dir / name)) {
            spdlog::warn("Font {} not found in {}; using ImGui's built-in font", name,
                         path_to_utf8(dir));
            ImFontConfig fallback;
            fallback.SizePixels = 13.0f * dpi;
            ImFont *f = io.Fonts->AddFontDefault(&fallback);
            state().fonts = {f, f, f, f};
            return;
        }
    }
    const auto add = [&](const char *name, float px, bool icons) {
        ImFont *font = io.Fonts->AddFontFromFileTTF(file(name).c_str(), px * dpi);
        if (icons) {
            ImFontConfig merge;
            merge.MergeMode = true;
            merge.PixelSnapH = true;
            merge.GlyphMinAdvanceX = px * dpi; // Icons on a square grid.
            merge.GlyphOffset = ImVec2(0.0f, 0.12f * px * dpi);
            io.Fonts->AddFontFromFileTTF(file("Lucide.ttf").c_str(), px * dpi, &merge,
                                         kLucideRange);
        }
        return font;
    };
    State &st = state();
    st.fonts[static_cast<size_t>(ThemeFont::ui)] = add("Inter-Regular.ttf", kBaseFontPx, true);
    st.fonts[static_cast<size_t>(ThemeFont::ui_medium)] =
        add("Inter-Medium.ttf", kBaseFontPx, true);
    st.fonts[static_cast<size_t>(ThemeFont::mono)] =
        add("JetBrainsMono-Regular.ttf", kMonoFontPx, false);
    st.fonts[static_cast<size_t>(ThemeFont::plot)] = add("Inter-Regular.ttf", kPlotFontPx, false);
    io.FontDefault = st.fonts[static_cast<size_t>(ThemeFont::ui)];
}

void apply_imgui(const ThemeTokens &t, float dpi, bool borders) {
    ImGuiStyle &style = ImGui::GetStyle();
    style = ImGuiStyle(); // Sizes from scratch, so switching modes never compounds the scale.
    style.WindowPadding = ImVec2(10, 10);
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 6);
    style.FrameRounding = 4;
    style.GrabRounding = 4;
    style.TabRounding = 4;
    style.WindowRounding = 6;
    style.ChildRounding = 6;
    style.FrameBorderSize = 0;
    style.WindowBorderSize = 0;
    style.ChildBorderSize = borders ? 1.0f : 0.0f;
    style.PopupBorderSize = borders ? 1.0f : 0.0f;
    style.ScrollbarSize = 10;
    style.ScaleAllSizes(dpi);

    const ImVec4 clear(0, 0, 0, 0);
    const ImVec4 child = mix(t.panel, t.bg, 0.5f);
    const ImVec4 pressed = mix(t.frame_hover, t.text, 0.08f);
    ImVec4 *c = style.Colors;
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        c[i] = t.text_dim; // Everything derives from the tokens, including anything unlisted.
    }
    c[ImGuiCol_Text] = t.text;
    c[ImGuiCol_TextDisabled] = t.text_dim;
    c[ImGuiCol_WindowBg] = t.panel;
    c[ImGuiCol_ChildBg] = child;
    c[ImGuiCol_PopupBg] = mix(t.panel, t.frame, 0.5f);
    c[ImGuiCol_Border] = borders ? t.frame_hover : clear;
    c[ImGuiCol_BorderShadow] = clear;
    c[ImGuiCol_FrameBg] = t.frame;
    c[ImGuiCol_FrameBgHovered] = t.frame_hover;
    c[ImGuiCol_FrameBgActive] = pressed;
    c[ImGuiCol_TitleBg] = t.bg;
    c[ImGuiCol_TitleBgActive] = t.bg;
    c[ImGuiCol_TitleBgCollapsed] = t.bg;
    c[ImGuiCol_MenuBarBg] = t.bg;
    c[ImGuiCol_ScrollbarBg] = clear;
    c[ImGuiCol_ScrollbarGrab] = t.frame_hover;
    c[ImGuiCol_ScrollbarGrabHovered] = mix(t.frame_hover, t.text_dim, 0.3f);
    c[ImGuiCol_ScrollbarGrabActive] = t.text_dim;
    c[ImGuiCol_CheckMark] = t.text; // Neutral; the active mode pushes the accent itself.
    c[ImGuiCol_SliderGrab] = t.text_dim;
    c[ImGuiCol_SliderGrabActive] = t.text;
    c[ImGuiCol_Button] = t.frame;
    c[ImGuiCol_ButtonHovered] = t.frame_hover;
    c[ImGuiCol_ButtonActive] = pressed;
    c[ImGuiCol_Header] = t.frame;
    c[ImGuiCol_HeaderHovered] = t.frame_hover;
    c[ImGuiCol_HeaderActive] = pressed;
    c[ImGuiCol_Separator] = t.frame;
    c[ImGuiCol_SeparatorHovered] = t.frame_hover;
    c[ImGuiCol_SeparatorActive] = t.text_dim;
    c[ImGuiCol_ResizeGrip] = clear;
    c[ImGuiCol_ResizeGripHovered] = t.frame_hover;
    c[ImGuiCol_ResizeGripActive] = t.text_dim;
    c[ImGuiCol_TabHovered] = t.frame_hover;
    c[ImGuiCol_Tab] = t.bg;
    c[ImGuiCol_TabSelected] = t.panel;
    c[ImGuiCol_TabSelectedOverline] = t.text_dim;
    c[ImGuiCol_TabDimmed] = t.bg;
    c[ImGuiCol_TabDimmedSelected] = t.panel;
    c[ImGuiCol_TabDimmedSelectedOverline] = clear;
    c[ImGuiCol_DockingPreview] = with_alpha(t.text, 0.25f);
    c[ImGuiCol_DockingEmptyBg] = t.bg;
    c[ImGuiCol_PlotLines] = t.text_dim;
    c[ImGuiCol_PlotLinesHovered] = t.text;
    c[ImGuiCol_PlotHistogram] = t.text_dim;
    c[ImGuiCol_PlotHistogramHovered] = t.text;
    c[ImGuiCol_TableHeaderBg] = t.frame;
    c[ImGuiCol_TableBorderStrong] = t.frame_hover;
    c[ImGuiCol_TableBorderLight] = t.frame;
    c[ImGuiCol_TableRowBg] = clear;
    c[ImGuiCol_TableRowBgAlt] = with_alpha(t.text, 0.03f);
    c[ImGuiCol_TextLink] = t.info;
    c[ImGuiCol_TextSelectedBg] = with_alpha(t.accent, 0.35f); // Selection.
    c[ImGuiCol_DragDropTarget] = t.text;
    c[ImGuiCol_NavCursor] = t.text_dim;
    c[ImGuiCol_NavWindowingHighlight] = with_alpha(t.text, 0.7f);
    c[ImGuiCol_NavWindowingDimBg] = with_alpha(t.bg, 0.6f);
    c[ImGuiCol_ModalWindowDimBg] = with_alpha(t.bg, 0.6f);
}

void apply_implot(const ThemeTokens &t) {
    ImPlotStyle &style = ImPlot::GetStyle();
    ImVec4 *c = style.Colors;
    const ImVec4 auto_colour = IMPLOT_AUTO_COL;
    c[ImPlotCol_Line] = auto_colour;
    c[ImPlotCol_Fill] = auto_colour;
    c[ImPlotCol_MarkerOutline] = auto_colour;
    c[ImPlotCol_MarkerFill] = auto_colour;
    c[ImPlotCol_ErrorBar] = t.text;
    c[ImPlotCol_FrameBg] = ImVec4(0, 0, 0, 0);
    c[ImPlotCol_PlotBg] = mix(t.panel, t.bg, 0.7f);
    c[ImPlotCol_PlotBorder] = ImVec4(0, 0, 0, 0);
    c[ImPlotCol_LegendBg] = with_alpha(t.panel, 0.85f);
    c[ImPlotCol_LegendBorder] = ImVec4(0, 0, 0, 0);
    c[ImPlotCol_LegendText] = t.text;
    c[ImPlotCol_TitleText] = t.text;
    c[ImPlotCol_InlayText] = t.text;
    c[ImPlotCol_AxisText] = t.text_dim;
    c[ImPlotCol_AxisGrid] = with_alpha(t.text, 0.10f);
    c[ImPlotCol_AxisTick] = with_alpha(t.text, 0.25f);
    c[ImPlotCol_AxisBg] = ImVec4(0, 0, 0, 0);
    c[ImPlotCol_AxisBgHovered] = with_alpha(t.frame_hover, 0.5f);
    c[ImPlotCol_AxisBgActive] = t.frame_hover;
    c[ImPlotCol_Selection] = t.accent;
    c[ImPlotCol_Crosshairs] = with_alpha(t.text, 0.5f);
    style.MinorAlpha = 0.5f; // Minor grid lines at half the major ones' 10%.
}

} // namespace

void init_theme(float dpi_scale) {
    State &st = state();
    st.dpi = dpi_scale;
    load_fonts(dpi_scale);
    load_overrides(exe_dir() / "theme.json");
}

void apply_psyight_theme(ThemeMode mode) {
    State &st = state();
    st.mode = mode;
    const ThemeTokens &t = mode == ThemeMode::show ? st.show : st.desk;
    apply_imgui(t, st.dpi, st.borders);
    apply_implot(t);
}

ThemeMode theme_mode() { return state().mode; }

const ThemeTokens &theme() {
    const State &st = state();
    return st.mode == ThemeMode::show ? st.show : st.desk;
}

ColourTable pitch_colour_table() {
    const ThemeTokens &t = theme();
    ColourTable table{};
    for (size_t i = 0; i < table.size(); ++i) {
        ImVec4 c = mix(t.pitch_low, t.pitch_high, static_cast<float>(i) / 255.0f);
        c.w = 1.0f;
        table[i] = to_u32(c);
    }
    return table;
}

ImFont *theme_font(ThemeFont font) { return state().fonts[static_cast<size_t>(font)]; }

void push_filled_button(const ImVec4 &fill) {
    const ThemeTokens &t = theme();
    ImGui::PushStyleColor(ImGuiCol_Button, fill);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, mix(fill, t.text, 0.15f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, mix(fill, t.text, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_Text, t.bg);
}

void pop_filled_button() { ImGui::PopStyleColor(4); }

bool begin_plot(const char *title_id, const ImVec2 &size, int flags) {
    // Pushed outside the plot's child window (ImGui checks the font stack per window).
    ImGui::PushFont(theme_font(ThemeFont::plot));
    if (!ImPlot::BeginPlot(title_id, size, flags)) {
        ImGui::PopFont();
        return false;
    }
    return true;
}

float plot_font_size() {
    const ImFont *font = theme_font(ThemeFont::plot);
    return font != nullptr ? font->FontSize : ImGui::GetFontSize();
}

void end_plot() {
    ImPlot::EndPlot();
    ImGui::PopFont();
}

void draw_style_editor(bool *open) {
    if (!*open) {
        return;
    }
    if (ImGui::Begin("Style editor (F12)", open)) {
        ImGui::TextDisabled("Dev only: changes last until the theme is applied again.");
        if (ImGui::CollapsingHeader("ImGui", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ShowStyleEditor();
        }
        if (ImGui::CollapsingHeader("ImPlot")) {
            ImPlot::ShowStyleEditor();
        }
    }
    ImGui::End();
}

} // namespace psyight
