#ifndef __NET_CONFIG_H
#define __NET_CONFIG_H

#include <stdint.h>

typedef struct
{
    uint8_t ip[4];
    uint8_t netmask[4];
    uint8_t gateway[4];
} net_config_t;

void net_config_get_default(net_config_t *config);
int net_config_validate(const net_config_t *config);
int net_config_load(net_config_t *config);
int net_config_save(const net_config_t *config);
int net_config_clear(void);

#endif
