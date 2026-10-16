#include "io/project.h"

#include "core/paths.h"
#include "io/text_file.h"
#include "output/udp_socket.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>

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

std::string address_text(const std::array<std::uint8_t, 4> &a) {
    return std::to_string(a[0]) + "." + std::to_string(a[1]) + "." + std::to_string(a[2]) + "." +
           std::to_string(a[3]);
}

} // namespace

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

        p.latency_offset_ms = number(root, "latency_offset_ms", 0.0, -1000.0, 1000.0);

        const json &aut = section(root, "automation");
        AutomationParams &a = p.automation;
        a.crossfade_beats = number(aut, "crossfade_beats", a.crossfade_beats, 0.0, 64.0);
        a.palette_every_phrases =
            integer(aut, "palette_every_phrases", a.palette_every_phrases, 0, 64);
        a.palette_slew_beats = number(aut, "palette_slew_beats", a.palette_slew_beats, 0.0, 64.0);
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
        const json root = {
            {"schema", kProjectSchemaVersion},
            {"rig", {{"strips", strips}}},
            {"patch",
             {{"first_universe", p.first_universe},
              {"pixels_per_universe", p.pixels_per_universe}}},
            {"output",
             {{"protocol", p.output.protocol == OutputProtocol::sacn ? "sacn" : "art_net"},
              {"multicast", p.output.multicast},
              {"address", address_text(p.output.address)},
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
              {"gamma", finite(l.gamma, "gamma")}}},
            {"latency_offset_ms", finite(p.latency_offset_ms, "latency_offset_ms")},
            {"automation",
             {{"crossfade_beats", finite(a.crossfade_beats, "crossfade_beats")},
              {"palette_every_phrases", a.palette_every_phrases},
              {"palette_slew_beats", finite(a.palette_slew_beats, "palette_slew_beats")}}},
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
