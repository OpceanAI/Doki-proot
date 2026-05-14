#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "doki_portswitch.h"

static port_map_t port_maps[DOKI_PORT_MAX_MAPS];
static int port_map_count = 0;

/* Parse JSON like {"port_map":[{"guest_bind":80,"host_bind":8080,"proto":"tcp"}]} */
void doki_portswitch_load_from_json(const char *json_msg) {
    /* Find port_map array */
    const char *key = strstr(json_msg, "\"port_map\"");
    if (!key) return;

    const char *arr = strchr(key, '[');
    if (!arr) return;
    arr++;

    port_map_count = 0;

    while (*arr && port_map_count < DOKI_PORT_MAX_MAPS) {
        /* Find next object */
        const char *obj = strchr(arr, '{');
        if (!obj) break;

        /* Extract guest_bind, host_bind, proto */
        int guest = 0, host = 0;
        char proto[4] = "tcp";

        const char *gb = strstr(obj, "\"guest_bind\"");
        if (gb) {
            const char *val = strchr(gb, ':');
            if (val) guest = atoi(val + 1);
        }

        const char *hb = strstr(obj, "\"host_bind\"");
        if (hb) {
            const char *val = strchr(hb, ':');
            if (val) host = atoi(val + 1);
        }

        const char *pr = strstr(obj, "\"proto\"");
        if (pr) {
            const char *val = strchr(pr, ':');
            if (val) {
                val++;
                while (*val == ' ' || *val == '"') val++;
                int i = 0;
                while (*val && *val != '"' && *val != ',' && *val != '}' && i < 3) {
                    proto[i++] = *val++;
                }
                proto[i] = '\0';
            }
        }

        if (guest > 0 && host > 0) {
            port_maps[port_map_count].guest_port = guest;
            port_maps[port_map_count].host_port = host;
            strncpy(port_maps[port_map_count].proto, proto, 3);
            port_maps[port_map_count].proto[3] = '\0';
            port_map_count++;
        }

        arr = strchr(obj, '}');
        if (arr) arr++;
    }

    fprintf(stderr, "{\"type\":\"log\",\"msg\":\"doki_portswitch: %d port maps\"}\n",
            port_map_count);
}

int doki_portswitch_has_maps(void) {
    return port_map_count > 0;
}

const port_map_t *doki_portswitch_find(int port, const char *proto) {
    for (int i = 0; i < port_map_count; i++) {
        if (port_maps[i].guest_port == port &&
            strcmp(port_maps[i].proto, proto) == 0) {
            return &port_maps[i];
        }
    }
    return NULL;
}

int doki_portswitch_remap_port(int guest_port, const char *proto) {
    const port_map_t *m = doki_portswitch_find(guest_port, proto);
    return m ? m->host_port : guest_port;
}
