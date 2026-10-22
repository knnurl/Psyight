#include "node_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const kPolicies[] = {"hold", "fade", "fallback", "blackout"};

void node_config_defaults(node_config *c, const uint8_t mac[6]) {
    memset(c, 0, sizeof(*c));
    c->magic = NODE_CONFIG_MAGIC;
    c->version = NODE_CONFIG_VERSION;
    /* DHCP first; without a lease the node falls back to the Art-Net style 2.x.y.z/8 address
     * derived from its MAC, so it can always be found. */
    c->dhcp = 1;
    c->ip[0] = 2;
    c->ip[1] = mac[3];
    c->ip[2] = mac[4];
    c->ip[3] = mac[5] == 0 || mac[5] == 255 ? 10 : mac[5];
    c->mask[0] = 255;
    c->protocols = NODE_PROTOCOL_ART_NET | NODE_PROTOCOL_SACN;
    c->active_mask = 0x0F; /* The default rig's four strips on PE0..PE3. */
    strcpy(c->short_name, "Psyight node");
    strcpy(c->long_name, "Psyight STM32F767 pixel node");
    c->node = node_default_settings();
    c->crc = node_config_crc(c);
}

uint32_t node_config_crc(const node_config *c) {
    const uint8_t *p = (const uint8_t *)c;
    const size_t n = offsetof(node_config, crc);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

int node_config_valid(const node_config *c) {
    return c->magic == NODE_CONFIG_MAGIC && c->version == NODE_CONFIG_VERSION &&
           c->crc == node_config_crc(c);
}

static int parse_ip(const char *text, uint8_t out[4]) {
    unsigned a, b, cc, d;
    char extra;
    if (sscanf(text, "%u.%u.%u.%u%c", &a, &b, &cc, &d, &extra) != 4 || a > 255 || b > 255 ||
        cc > 255 || d > 255) {
        return 0;
    }
    out[0] = (uint8_t)a;
    out[1] = (uint8_t)b;
    out[2] = (uint8_t)cc;
    out[3] = (uint8_t)d;
    return 1;
}

static int parse_uint(const char *text, unsigned long max, unsigned long *out) {
    char *end = NULL;
    const unsigned long v = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || v > max) {
        return 0;
    }
    *out = v;
    return 1;
}

static size_t append(char *reply, size_t capacity, size_t used, const char *text) {
    const size_t n = strlen(text);
    if (used + n + 1 > capacity) {
        return used;
    }
    memcpy(reply + used, text, n + 1);
    return used + n;
}

static void describe(const node_config *c, char *reply, size_t capacity) {
    char line[96];
    size_t used = 0;
    reply[0] = '\0';
#define LINE(...)                                                                                  \
    do {                                                                                           \
        snprintf(line, sizeof(line), __VA_ARGS__);                                                 \
        used = append(reply, capacity, used, line);                                                \
    } while (0)
    LINE("protocol %u\n", NODE_PROTOCOL_VERSION);
    LINE("ip %u.%u.%u.%u\n", c->ip[0], c->ip[1], c->ip[2], c->ip[3]);
    LINE("mask %u.%u.%u.%u\n", c->mask[0], c->mask[1], c->mask[2], c->mask[3]);
    LINE("gateway %u.%u.%u.%u\n", c->gateway[0], c->gateway[1], c->gateway[2], c->gateway[3]);
    LINE("dhcp %u\n", c->dhcp);
    LINE("name %s\n", c->short_name);
    LINE("long_name %s\n", c->long_name);
    LINE("protocols %u\n", c->protocols);
    LINE("active %u\n", c->active_mask);
    LINE("universe %u\n", c->node.first_universe);
    LINE("universes %u\n", c->node.universe_count);
    LINE("pixels_per_universe %u\n", c->node.pixels_per_universe);
    for (int l = 0; l < NODE_LANES; ++l) {
        LINE("lane%d %u %u\n", l, c->node.lanes[l].first_pixel, c->node.lanes[l].pixels);
    }
    LINE("loss_ms %lu\n", (unsigned long)c->node.loss_timeout_ms);
    LINE("policy %s\n", kPolicies[c->node.loss_policy]);
    LINE("fade_ms %lu\n", (unsigned long)c->node.fade_ms);
    LINE("fallback_brightness %u\n", (unsigned)(c->node.fallback_brightness * 1000.0f + 0.5f));
    LINE("fallback_speed %u\n", (unsigned)(c->node.fallback_speed_hz * 1000.0f + 0.5f));
    used = append(reply, capacity, used, "palette");
    for (unsigned i = 0; i < c->node.palette_size && i < 4u; ++i) {
        const uint8_t *grb = c->node.palette[i];
        snprintf(line, sizeof(line), " %02X%02X%02X", grb[1], grb[0], grb[2]); /* As RGB. */
        used = append(reply, capacity, used, line);
    }
    used = append(reply, capacity, used, "\n");
#undef LINE
}

/* "RRGGBB RRGGBB ...": one to four colours, stored in the strip's GRB order. */
static int parse_palette(const char *value, node_settings *s) {
    uint8_t colours[4][3];
    unsigned n = 0;
    const char *p = value;
    while (*p != '\0') {
        while (*p == ' ') {
            ++p;
        }
        if (*p == '\0') {
            break;
        }
        char hex[7];
        unsigned length = 0;
        while (p[length] != '\0' && p[length] != ' ' && length < 7u) {
            ++length;
        }
        if (length != 6u || n == 4u) {
            return 0;
        }
        for (unsigned i = 0; i < 6u; ++i) {
            const char ch = p[i];
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
                  (ch >= 'A' && ch <= 'F'))) {
                return 0;
            }
            hex[i] = ch;
        }
        hex[6] = '\0';
        const unsigned long rgb = strtoul(hex, NULL, 16);
        colours[n][0] = (uint8_t)(rgb >> 8);  /* G */
        colours[n][1] = (uint8_t)(rgb >> 16); /* R */
        colours[n][2] = (uint8_t)rgb;         /* B */
        ++n;
        p += 6;
    }
    if (n == 0u) {
        return 0;
    }
    memcpy(s->palette, colours, sizeof(colours[0]) * n);
    s->palette_size = (uint8_t)n;
    return 1;
}

/* Applies "key value"; returns 0 with a reason on failure. */
static int set(node_config *c, const char *key, const char *value, const char **why) {
    unsigned long v = 0;
    *why = "bad value";
    if (strcmp(key, "ip") == 0) {
        return parse_ip(value, c->ip);
    }
    if (strcmp(key, "mask") == 0) {
        return parse_ip(value, c->mask);
    }
    if (strcmp(key, "gateway") == 0) {
        return parse_ip(value, c->gateway);
    }
    if (strcmp(key, "name") == 0 || strcmp(key, "long_name") == 0) {
        char *dest = key[0] == 'n' ? c->short_name : c->long_name;
        const size_t cap = key[0] == 'n' ? sizeof(c->short_name) : sizeof(c->long_name);
        if (strlen(value) >= cap) {
            *why = "too long";
            return 0;
        }
        memset(dest, 0, cap);
        strcpy(dest, value);
        return 1;
    }
    if (strcmp(key, "palette") == 0) {
        *why = "one to four RRGGBB colours";
        return parse_palette(value, &c->node);
    }
    if (strcmp(key, "policy") == 0) {
        for (unsigned i = 0; i < 4; ++i) {
            if (strcmp(value, kPolicies[i]) == 0) {
                c->node.loss_policy = (node_loss_policy)i;
                return 1;
            }
        }
        return 0;
    }
    if (strncmp(key, "lane", 4) == 0 && key[4] >= '0' && key[4] < '0' + NODE_LANES &&
        key[5] == '\0') {
        unsigned first = 0, count = 0;
        char extra;
        if (sscanf(value, "%u %u%c", &first, &count, &extra) != 2 || first > 65535u ||
            count > NODE_MAX_PIXELS_PER_LANE) {
            return 0;
        }
        c->node.lanes[key[4] - '0'].first_pixel = (uint16_t)first;
        c->node.lanes[key[4] - '0'].pixels = (uint16_t)count;
        return 1;
    }
    struct {
        const char *key;
        unsigned long max;
    } const numbers[] = {{"dhcp", 1},
                         {"protocols", 3},
                         {"active", 255},
                         {"universe", 63999},
                         {"universes", NODE_MAX_UNIVERSES},
                         {"pixels_per_universe", 170},
                         {"loss_ms", 600000},
                         {"fade_ms", 600000},
                         {"fallback_brightness", 1000},
                         {"fallback_speed", 10000}};
    for (size_t i = 0; i < sizeof(numbers) / sizeof(numbers[0]); ++i) {
        if (strcmp(key, numbers[i].key) != 0) {
            continue;
        }
        if (!parse_uint(value, numbers[i].max, &v)) {
            return 0;
        }
        switch (i) {
        case 0:
            c->dhcp = (uint8_t)v;
            break;
        case 1:
            c->protocols = (uint8_t)v;
            break;
        case 2:
            c->active_mask = (uint8_t)v;
            break;
        case 3:
            c->node.first_universe = (uint16_t)(v == 0 ? 1 : v);
            break;
        case 4:
            c->node.universe_count = (uint8_t)v;
            break;
        case 5:
            c->node.pixels_per_universe = (uint8_t)(v == 0 ? 170 : v);
            break;
        case 6:
            c->node.loss_timeout_ms = (uint32_t)v;
            break;
        case 7:
            c->node.fade_ms = (uint32_t)v;
            break;
        case 8:
            c->node.fallback_brightness = (float)v / 1000.0f;
            break;
        default:
            c->node.fallback_speed_hz = (float)v / 1000.0f;
            break;
        }
        return 1;
    }
    *why = "unknown key";
    return 0;
}

node_command_action node_config_command(node_config *c, const uint8_t mac[6], const char *command,
                                        size_t length, char *reply, size_t capacity) {
    char text[160];
    if (length >= sizeof(text)) {
        snprintf(reply, capacity, "error too long\n");
        return NODE_CMD_NONE;
    }
    memcpy(text, command, length);
    text[length] = '\0';
    while (length > 0 &&
           (text[length - 1] == '\n' || text[length - 1] == '\r' || text[length - 1] == ' ')) {
        text[--length] = '\0';
    }
    if (strcmp(text, "get") == 0) {
        describe(c, reply, capacity);
        return NODE_CMD_NONE;
    }
    if (strcmp(text, "save") == 0) {
        c->crc = node_config_crc(c);
        snprintf(reply, capacity, "ok saving\n");
        return NODE_CMD_SAVE;
    }
    if (strcmp(text, "reboot") == 0) {
        snprintf(reply, capacity, "ok rebooting\n");
        return NODE_CMD_REBOOT;
    }
    if (strcmp(text, "apply") == 0) {
        snprintf(reply, capacity, "ok applied (save keeps it after a restart)\n");
        return NODE_CMD_APPLY;
    }
    if (strcmp(text, "defaults") == 0) {
        node_config_defaults(c, mac);
        snprintf(reply, capacity, "ok defaults (not saved)\n");
        return NODE_CMD_NONE;
    }
    if (strncmp(text, "set ", 4) == 0) {
        char *key = text + 4;
        char *value = strchr(key, ' ');
        if (value == NULL) {
            snprintf(reply, capacity, "error set needs a key and a value\n");
            return NODE_CMD_NONE;
        }
        *value++ = '\0';
        const char *why = "";
        if (!set(c, key, value, &why)) {
            snprintf(reply, capacity, "error %s: %s\n", key, why);
            return NODE_CMD_NONE;
        }
        c->crc = node_config_crc(c);
        const int network = strcmp(key, "ip") == 0 || strcmp(key, "mask") == 0 ||
                            strcmp(key, "gateway") == 0 || strcmp(key, "dhcp") == 0;
        const int live = strcmp(key, "name") == 0 || strcmp(key, "long_name") == 0 ||
                         strcmp(key, "protocols") == 0;
        snprintf(reply, capacity, "ok %s%s\n", key,
                 network ? " (save, then reboot, to use it)"
                         : live ? "" : " (apply to use it now, save to keep it)");
        return NODE_CMD_NONE;
    }
    snprintf(reply, capacity, "error unknown command (get, set, apply, save, defaults, reboot)\n");
    return NODE_CMD_NONE;
}
