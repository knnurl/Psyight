#include "io/project.h"

#include "core/paths.h"
#include "engine/effect_spec.h"
#include "io/text_file.h"
#include "output/udp_socket.h"

#include <nlohmann/json.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace psyight {

namespace {

using nlohmann::json;

// Thrown by the checks below and caught at the API boundary, like nlohmann's own exceptions.
struct FieldError {
    std::string message;
};

constexpr std::array<ColourOrder, 6> kOrders = {ColourOrder::rgb, ColourOrder::grb,
                                                ColourOrder::brg, ColourOrder::rbg,
                                                ColourOrder::gbr, ColourOrder::bgr};

double number(const json &object, const char *key, double fallback, double min, double max) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return fallback;
    }
    const double value = it->get<double>();
    if (!std::isfinite(value) || value < min || value > max) {
        throw FieldError{std::string("field ") + key + " is out of range"};
    }
    return value;
}

int integer(const json &object, const char *key, int fallback, int min, int max) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return fallback;
    }
    if (!it->is_number_integer()) {
        throw FieldError{std::string("field ") + key + " must be an integer"};
    }
    const int value = it->get<int>();
    if (value < min || value > max) {
        throw FieldError{std::string("field ") + key + " is out of range"};
    }
    return value;
}

template <typename T> T optional(const json &object, const char *key, T fallback) {
    const auto it = object.find(key);
    return it == object.end() ? fallback : it->get<T>();
}

const json &section(const json &root, const char *key) {
    static const json kEmpty = json::object();
    const auto it = root.find(key);
    return it == root.end() ? kEmpty : *it;
}

double finite(double value, const char *field) {
    if (!std::isfinite(value)) {
        throw FieldError{std::string("field ") + field + " is not a finite number"};
    }
    return value;
}

ColourOrder parse_order(const std::string &name) {
    for (const ColourOrder order : kOrders) {
        if (name == colour_order_name(order)) {
            return order;
        }
    }
    throw FieldError{"unknown colour order " + name};
}

float unit_value(const json &value, const char *field) {
    const double v = value.get<double>();
    if (!std::isfinite(v) || v < 0.0 || v > 1.0) {
        throw FieldError{std::string(field) + " must be between 0 and 1"};
    }
    return static_cast<float>(v);
}

template <typename T>
T from_id_or_throw(const std::optional<T> &value, const char *what, const std::string &id) {
    if (!value) {
        throw FieldError{std::string("unknown ") + what + " " + id};
    }
    return *value;
}

Scene parse_scene(const json &item) {
    Scene s;
    s.name = optional<std::string>(item, "name", "");
    s.palette = integer(item, "palette", 0, 0, 1000);
    s.sections = optional<std::vector<std::string>>(item, "sections", {});
    for (const json &l : item.at("layers").get_ref<const json::array_t &>()) {
        const std::string effect = l.at("effect").get<std::string>();
        const EffectType type = from_id_or_throw(effect_type_from_id(effect), "effect", effect);
        EffectSpec spec = default_spec(type);
        if (const auto params = l.find("params"); params != l.end()) {
            const std::vector<EffectParamInfo> infos = effect_param_infos(type);
            for (size_t i = 0; i < infos.size(); ++i) {
                if (const auto v = params->find(infos[i].id); v != params->end()) {
                    set_effect_param(spec, i, finite(v->get<double>(), infos[i].id));
                }
            }
        }
        const std::string blend = optional<std::string>(l, "blend", "add");
        Layer layer = make_layer(spec, from_id_or_throw(blend_mode_from_id(blend), "blend", blend),
                                 static_cast<float>(number(l, "opacity", 1.0, 0.0, 1.0)));
        layer.enabled = optional(l, "enabled", true);
        layer.group = optional<std::string>(l, "group", "");
        s.layers.push_back(std::move(layer));
    }
    if (const auto routes = item.find("routes"); routes != item.end()) {
        for (const json &r : routes->get_ref<const json::array_t &>()) {
            ModRoute route;
            const std::string source = r.at("source").get<std::string>();
            const std::string curve = optional<std::string>(r, "curve", "linear");
            const std::string target = r.at("target").get<std::string>();
            route.source = from_id_or_throw(mod_source_from_id(source), "source", source);
            route.curve = from_id_or_throw(mod_curve_from_id(curve), "curve", curve);
            route.target = from_id_or_throw(mod_target_from_id(target), "target", target);
            route.invert = optional(r, "invert", false);
            route.layer = integer(r, "layer", 0, 0, 1000);
            route.amount = number(r, "amount", 0.0, -360.0, 360.0);
            const std::string quantise = optional<std::string>(r, "quantise", "off");
            route.quantise = from_id_or_throw(mod_quantise_from_id(quantise), "quantise", quantise);
            route.attack_s = number(r, "attack_s", 0.0, 0.0, kMaxSmoothingS);
            route.release_s = number(r, "release_s", 0.0, 0.0, kMaxSmoothingS);
            route.param = optional<std::string>(r, "param", "");
            s.routes.push_back(route);
        }
    }
    return s;
}

json scene_json(const Scene &s) {
    json layers = json::array();
    for (const Layer &l : s.layers) {
        json params = json::object();
        const std::vector<EffectParamInfo> infos = effect_param_infos(l.spec.type());
        for (size_t i = 0; i < infos.size(); ++i) {
            params[infos[i].id] = finite(effect_param(l.spec, i), infos[i].id);
        }
        layers.push_back({{"effect", effect_type_id(l.spec.type())},
                          {"blend", blend_mode_id(l.blend)},
                          {"opacity", finite(l.opacity, "opacity")},
                          {"enabled", l.enabled},
                          {"group", l.group},
                          {"params", params}});
    }
    json routes = json::array();
    for (const ModRoute &r : s.routes) {
        routes.push_back({{"source", mod_source_id(r.source)},
                          {"curve", mod_curve_id(r.curve)},
                          {"invert", r.invert},
                          {"target", mod_target_id(r.target)},
                          {"layer", r.layer},
                          {"amount", finite(r.amount, "amount")},
                          {"quantise", mod_quantise_id(r.quantise)},
                          {"attack_s", finite(r.attack_s, "attack_s")},
                          {"release_s", finite(r.release_s, "release_s")},
                          {"param", r.param}});
    }
    return {{"name", s.name},
            {"palette", s.palette},
            {"sections", s.sections},
            {"layers", layers},
            {"routes", routes}};
}

} // namespace

std::string macro_name(const Project &project, int index) {
    const std::string &name = project.macro_names[static_cast<size_t>(index)];
    return name.empty() ? "Macro " + std::to_string(index + 1) : name;
}

const char *colour_order_name(ColourOrder order) {
    switch (order) {
    case ColourOrder::rgb:
        return "rgb";
    case ColourOrder::grb:
        return "grb";
    case ColourOrder::brg:
        return "brg";
    case ColourOrder::rbg:
        return "rbg";
    case ColourOrder::gbr:
        return "gbr";
    case ColourOrder::bgr:
        return "bgr";
    }
    return "grb";
}

std::optional<std::vector<Vec3>> parse_polyline_text(const std::string &text) {
    std::vector<Vec3> points;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ' ' || text[i] == ';' || text[i] == '\t')) {
            ++i;
        }
        if (i == text.size()) {
            break;
        }
        std::array<double, 3> xyz{};
        int n = 0;
        while (true) {
            if (n == 3) {
                return std::nullopt;
            }
            const auto [next, ec] =
                std::from_chars(text.data() + i, text.data() + text.size(), xyz[n]);
            if (ec != std::errc() || !std::isfinite(xyz[n])) {
                return std::nullopt;
            }
            ++n;
            i = static_cast<size_t>(next - text.data());
            if (i < text.size() && text[i] == ',') {
                ++i;
                continue;
            }
            break;
        }
        if (n < 2 || (i < text.size() && text[i] != ' ' && text[i] != ';' && text[i] != '\t')) {
            return std::nullopt;
        }
        points.push_back({xyz[0], xyz[1], xyz[2]});
    }
    if (points.size() < 2) {
        return std::nullopt;
    }
    return points;
}

std::string polyline_text(const std::vector<Vec3> &polyline) {
    std::string out;
    for (const Vec3 &q : polyline) {
        char buffer[96];
        if (q.z == 0.0) {
            std::snprintf(buffer, sizeof(buffer), "%g,%g", q.x, q.y);
        } else {
            std::snprintf(buffer, sizeof(buffer), "%g,%g,%g", q.x, q.y, q.z);
        }
        out += (out.empty() ? "" : " ") + std::string(buffer);
    }
    return out;
}

int pixels_to_fill(const StripSpec &spec) {
    double length_m = 0.0;
    for (size_t i = 1; i < spec.polyline.size(); ++i) {
        const Vec3 &a = spec.polyline[i - 1];
        const Vec3 &b = spec.polyline[i];
        length_m += std::hypot(b.x - a.x, b.y - a.y, b.z - a.z);
    }
    return static_cast<int>(std::floor(length_m * spec.pixels_per_m + 1e-9));
}

Project default_project() {
    Project project;
    for (const StripSpec &spec : default_rig().strips) {
        project.strips.push_back({spec, ColourOrder::grb});
    }
    project.output.cid = cid_from_name(project.source_name);
    return project;
}

Rig rig_of(const Project &project) {
    std::vector<StripSpec> specs;
    for (const ProjectStrip &strip : project.strips) {
        specs.push_back(strip.spec);
    }
    return build_rig(std::move(specs));
}

Patch patch_of(const Project &project) {
    std::vector<ColourOrder> orders;
    for (const ProjectStrip &strip : project.strips) {
        orders.push_back(strip.order);
    }
    return build_patch(rig_of(project), project.first_universe, project.pixels_per_universe,
                       orders);
}

Result<Project> parse_project_json(std::string_view json_text) {
    try {
        const json root = json::parse(json_text);
        const int schema = integer(root, "schema", -1, -1, 1 << 30);
        if (schema != kProjectSchemaVersion) {
            return Error{"unsupported project schema " + std::to_string(schema) + ", expected " +
                         std::to_string(kProjectSchemaVersion)};
        }
        Project p = default_project();
        if (const auto rig = root.find("rig"); rig != root.end()) {
            p.strips.clear();
            for (const json &item : rig->at("strips").get_ref<const json::array_t &>()) {
                ProjectStrip strip;
                strip.spec.name = optional<std::string>(item, "name", "");
                for (const json &point : item.at("polyline").get_ref<const json::array_t &>()) {
                    const auto &xyz = point.get_ref<const json::array_t &>();
                    if (xyz.size() != 3) {
                        throw FieldError{"polyline points need x, y and z"};
                    }
                    strip.spec.polyline.push_back({finite(xyz[0].get<double>(), "x"),
                                                   finite(xyz[1].get<double>(), "y"),
                                                   finite(xyz[2].get<double>(), "z")});
                }
                if (strip.spec.polyline.size() < 2) {
                    throw FieldError{"strip " + strip.spec.name + " needs two polyline points"};
                }
                strip.spec.pixels_per_m = number(item, "pixels_per_m", 60.0, 1.0, 1000.0);
                strip.spec.count = integer(item, "count", 0, 0, 10000);
                strip.spec.groups = optional<std::vector<std::string>>(item, "groups", {});
                strip.order = parse_order(optional<std::string>(item, "colour_order", "grb"));
                p.strips.push_back(std::move(strip));
            }
        }
        const json &patch = section(root, "patch");
        p.first_universe = integer(patch, "first_universe", 1, 1, 63999);
        p.pixels_per_universe = integer(patch, "pixels_per_universe", 170, 1, 170);

        const json &out = section(root, "output");
        const std::string protocol = optional<std::string>(out, "protocol", "sacn");
        if (protocol != "sacn" && protocol != "art_net") {
            throw FieldError{"unknown protocol " + protocol};
        }
        p.output.protocol = protocol == "sacn" ? OutputProtocol::sacn : OutputProtocol::art_net;
        p.output.multicast = optional(out, "multicast", true);
        const std::string address = optional<std::string>(out, "address", "127.0.0.1");
        const auto parsed = parse_ipv4(address);
        if (!parsed) {
            throw FieldError{"address " + address + " is not an IPv4 address"};
        }
        p.output.address = *parsed;
        p.output.sync = optional(out, "sync", true);
        p.output.sync_universe =
            static_cast<std::uint16_t>(integer(out, "sync_universe", 1000, 1, 63999));
        p.output.priority = static_cast<std::uint8_t>(integer(out, "priority", 100, 0, 200));
        p.source_name = optional<std::string>(out, "source_name", "Psyight");
        p.output.cid = cid_from_name(p.source_name);

        const json &lim = section(root, "limiter");
        LimiterParams &l = p.limiter;
        l.brightness_cap = number(lim, "brightness_cap", l.brightness_cap, 0.0, 1.0);
        l.flash_limiter = optional(lim, "flash_limiter", l.flash_limiter);
        l.max_flashes_per_s = number(lim, "max_flashes_per_s", l.max_flashes_per_s, 0.0, 60.0);
        l.flash_delta = number(lim, "flash_delta", l.flash_delta, 0.0, 1.0);
        l.flash_area = number(lim, "flash_area", l.flash_area, 0.0, 1.0);
        l.current_per_channel_ma =
            number(lim, "current_per_channel_ma", l.current_per_channel_ma, 0.0, 1000.0);
        l.budget_a = number(lim, "budget_a", l.budget_a, 0.0, 10000.0);
        l.gamma = number(lim, "gamma", l.gamma, 1.0, 4.0);
        if (const auto caps = lim.find("group_caps"); caps != lim.end()) {
            for (const json &c : caps->get_ref<const json::array_t &>()) {
                p.group_caps.push_back(
                    {c.at("group").get<std::string>(), number(c, "cap", 1.0, 0.0, 1.0)});
            }
        }

        p.latency_offset_ms = number(root, "latency_offset_ms", 0.0, -1000.0, 1000.0);
        if (const auto it = root.find("midi"); it != root.end()) {
            const json &midi = *it;
            p.midi_port = midi.value("port", std::string());
            if (const auto b = midi.find("bindings"); b != midi.end()) {
                for (const json &m : b->get_ref<const json::array_t &>()) {
                    const std::string kind = m.at("kind").get<std::string>();
                    if (kind != "cc" && kind != "note") {
                        throw FieldError{"midi kind must be cc or note"};
                    }
                    p.midi.push_back({m.at("target").get<std::string>(),
                                      kind == "cc" ? MidiKind::cc : MidiKind::note,
                                      integer(m, "channel", 0, 0, 15),
                                      integer(m, "number", 0, 0, 127)});
                }
            }
        }

        if (const auto it = root.find("macros"); it != root.end()) {
            const json::array_t &macros = it->get_ref<const json::array_t &>();
            for (size_t i = 0; i < macros.size() && i < p.macro_values.size(); ++i) {
                p.macro_names[i] = optional<std::string>(macros[i], "name", "");
                p.macro_values[i] = number(macros[i], "value", 0.0, 0.0, 1.0);
            }
        }

        const json &aut = section(root, "automation");
        AutomationParams &a = p.automation;
        a.crossfade_beats = number(aut, "crossfade_beats", a.crossfade_beats, 0.0, 64.0);
        a.palette_every_phrases =
            integer(aut, "palette_every_phrases", a.palette_every_phrases, 0, 64);
        a.palette_slew_beats = number(aut, "palette_slew_beats", a.palette_slew_beats, 0.0, 64.0);

        if (const auto it = root.find("palettes"); it != root.end()) {
            p.palettes.clear();
            for (const json &item : it->get_ref<const json::array_t &>()) {
                Palette palette;
                palette.name = optional<std::string>(item, "name", "");
                for (const json &c : item.at("colours").get_ref<const json::array_t &>()) {
                    const auto &rgb = c.get_ref<const json::array_t &>();
                    if (rgb.size() != 3) {
                        throw FieldError{"palette colours need r, g and b"};
                    }
                    palette.colours.push_back({unit_value(rgb[0], "r"), unit_value(rgb[1], "g"),
                                               unit_value(rgb[2], "b")});
                }
                if (palette.colours.empty()) {
                    throw FieldError{"palette " + palette.name + " has no colours"};
                }
                p.palettes.push_back(std::move(palette));
            }
            if (p.palettes.empty()) {
                throw FieldError{"no palettes"};
            }
        }
        if (const auto it = root.find("scenes"); it != root.end()) {
            p.scenes.clear();
            for (const json &item : it->get_ref<const json::array_t &>()) {
                p.scenes.push_back(parse_scene(item));
            }
            if (p.scenes.empty()) {
                throw FieldError{"no scenes"};
            }
        }
        return p;
    } catch (const json::exception &e) {
        return Error{std::string("invalid project JSON: ") + e.what()};
    } catch (const FieldError &e) {
        return Error{"invalid project JSON: " + e.message};
    }
}

Result<std::string> to_project_json(const Project &p) {
    try {
        json strips = json::array();
        for (const ProjectStrip &s : p.strips) {
            json polyline = json::array();
            for (const Vec3 &q : s.spec.polyline) {
                polyline.push_back({finite(q.x, "x"), finite(q.y, "y"), finite(q.z, "z")});
            }
            strips.push_back({{"name", s.spec.name},
                              {"polyline", polyline},
                              {"pixels_per_m", finite(s.spec.pixels_per_m, "pixels_per_m")},
                              {"count", s.spec.count},
                              {"groups", s.spec.groups},
                              {"colour_order", colour_order_name(s.order)}});
        }
        const LimiterParams &l = p.limiter;
        const AutomationParams &a = p.automation;
        json group_caps = json::array();
        for (const GroupCap &c : p.group_caps) {
            group_caps.push_back({{"group", c.group}, {"cap", finite(c.cap, "cap")}});
        }
        json macros = json::array();
        for (size_t i = 0; i < p.macro_values.size(); ++i) {
            macros.push_back(
                {{"name", p.macro_names[i]}, {"value", finite(p.macro_values[i], "macro value")}});
        }
        json midi_bindings = json::array();
        for (const MidiBinding &b : p.midi) {
            midi_bindings.push_back({{"target", b.target},
                                     {"kind", b.kind == MidiKind::cc ? "cc" : "note"},
                                     {"channel", b.channel},
                                     {"number", b.number}});
        }
        json scenes = json::array();
        for (const Scene &s : p.scenes) {
            scenes.push_back(scene_json(s));
        }
        json palettes = json::array();
        for (const Palette &palette : p.palettes) {
            json colours = json::array();
            for (const Rgb &c : palette.colours) {
                colours.push_back({finite(c.r, "r"), finite(c.g, "g"), finite(c.b, "b")});
            }
            palettes.push_back({{"name", palette.name}, {"colours", colours}});
        }
        const json root = {
            {"schema", kProjectSchemaVersion},
            {"rig", {{"strips", strips}}},
            {"patch",
             {{"first_universe", p.first_universe},
              {"pixels_per_universe", p.pixels_per_universe}}},
            {"output",
             {{"protocol", p.output.protocol == OutputProtocol::sacn ? "sacn" : "art_net"},
              {"multicast", p.output.multicast},
              {"address", ipv4_text(p.output.address)},
              {"sync", p.output.sync},
              {"sync_universe", p.output.sync_universe},
              {"priority", p.output.priority},
              {"source_name", p.source_name}}},
            {"limiter",
             {{"brightness_cap", finite(l.brightness_cap, "brightness_cap")},
              {"flash_limiter", l.flash_limiter},
              {"max_flashes_per_s", finite(l.max_flashes_per_s, "max_flashes_per_s")},
              {"flash_delta", finite(l.flash_delta, "flash_delta")},
              {"flash_area", finite(l.flash_area, "flash_area")},
              {"current_per_channel_ma", finite(l.current_per_channel_ma, "current")},
              {"budget_a", finite(l.budget_a, "budget_a")},
              {"gamma", finite(l.gamma, "gamma")},
              {"group_caps", group_caps}}},
            {"latency_offset_ms", finite(p.latency_offset_ms, "latency_offset_ms")},
            {"automation",
             {{"crossfade_beats", finite(a.crossfade_beats, "crossfade_beats")},
              {"palette_every_phrases", a.palette_every_phrases},
              {"palette_slew_beats", finite(a.palette_slew_beats, "palette_slew_beats")}}},
            {"scenes", scenes},
            {"palettes", palettes},
            {"midi", {{"port", p.midi_port}, {"bindings", midi_bindings}}},
            {"macros", macros},
        };
        return root.dump(2) + "\n";
    } catch (const json::exception &e) {
        return Error{std::string("cannot write project: ") + e.what()};
    } catch (const FieldError &e) {
        return Error{"cannot write project: " + e.message};
    }
}

Result<Project> load_project(const std::filesystem::path &path) {
    const Result<std::string> contents = read_text_file(path);
    if (!contents) {
        return contents.error();
    }
    Result<Project> project = parse_project_json(contents.value());
    if (!project) {
        return Error{path_to_utf8(path) + ": " + project.error().message};
    }
    return project;
}

Result<void> save_project(const std::filesystem::path &path, const Project &project) {
    const Result<std::string> json_text = to_project_json(project);
    if (!json_text) {
        return json_text.error();
    }
    return write_text_file_atomic(path, json_text.value());
}

} // namespace psyight
