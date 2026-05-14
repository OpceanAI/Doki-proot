#ifndef DOKI_PORTSWITCH_H
#define DOKI_PORTSWITCH_H

#define DOKI_PORT_MAX_MAPS 64
#define DOKI_PORT_THRESHOLD 1024
#define DOKI_PORT_ADDITION 2000

typedef struct {
    int guest_port;
    int host_port;
    char proto[4];
} port_map_t;

void doki_portswitch_load_from_json(const char *json_msg);
int doki_portswitch_has_maps(void);
const port_map_t *doki_portswitch_find(int port, const char *proto);
int doki_portswitch_remap_port(int guest_port, const char *proto);

#endif /* DOKI_PORTSWITCH_H */
