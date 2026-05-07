#include "net_config.h"
#include "flash_storage.h"
#include <string.h>

#define NET_CONFIG_MAGIC   0x54454E41UL  /* "ANET" */
#define NET_CONFIG_VERSION 1UL

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t flags;
    uint32_t crc;
    uint32_t reserved;
} net_config_record_t;

static uint32_t net_config_pack_ip(const uint8_t ip[4])
{
    return ((uint32_t)ip[0] << 24) |
           ((uint32_t)ip[1] << 16) |
           ((uint32_t)ip[2] << 8) |
           ((uint32_t)ip[3]);
}

static void net_config_unpack_ip(uint32_t value, uint8_t ip[4])
{
    ip[0] = (uint8_t)(value >> 24);
    ip[1] = (uint8_t)(value >> 16);
    ip[2] = (uint8_t)(value >> 8);
    ip[3] = (uint8_t)value;
}

static uint32_t net_config_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;
    uint8_t bit;

    while (len-- > 0U)
    {
        crc ^= *data++;
        for (i = 0; i < 8U; i++)
        {
            bit = (uint8_t)(crc & 1U);
            crc >>= 1;
            if (bit != 0U)
            {
                crc ^= 0xEDB88320UL;
            }
        }
    }

    return ~crc;
}

static uint32_t net_config_record_crc(const net_config_record_t *record)
{
    return net_config_crc32((const uint8_t *)record, 24U);
}

static int net_config_ip_is_valid(const uint8_t ip[4])
{
    if ((ip[0] == 0U) || (ip[0] == 127U) || (ip[0] >= 224U))
    {
        return 0;
    }

    if ((ip[0] == 255U) && (ip[1] == 255U) && (ip[2] == 255U) && (ip[3] == 255U))
    {
        return 0;
    }

    if ((ip[0] == 0U) && (ip[1] == 0U) && (ip[2] == 0U) && (ip[3] == 0U))
    {
        return 0;
    }

    return 1;
}

static int net_config_mask_is_valid(const uint8_t mask[4])
{
    uint32_t value = net_config_pack_ip(mask);
    uint32_t inverse = ~value;

    if ((value == 0U) || (value == 0xFFFFFFFFUL))
    {
        return 0;
    }

    return ((inverse & (inverse + 1U)) == 0U) ? 1 : 0;
}

void net_config_get_default(net_config_t *config)
{
    if (config == 0)
    {
        return;
    }

    config->ip[0] = 192U;
    config->ip[1] = 168U;
    config->ip[2] = 1U;
    config->ip[3] = 30U;

    config->netmask[0] = 255U;
    config->netmask[1] = 255U;
    config->netmask[2] = 255U;
    config->netmask[3] = 0U;

    config->gateway[0] = 192U;
    config->gateway[1] = 168U;
    config->gateway[2] = 1U;
    config->gateway[3] = 1U;
}

int net_config_validate(const net_config_t *config)
{
    if (config == 0)
    {
        return 0;
    }

    if (!net_config_ip_is_valid(config->ip))
    {
        return 0;
    }

    if (!net_config_mask_is_valid(config->netmask))
    {
        return 0;
    }

    if (!net_config_ip_is_valid(config->gateway))
    {
        return 0;
    }

    return 1;
}

int net_config_load(net_config_t *config)
{
    net_config_record_t record;

    if (config == 0)
    {
        return -1;
    }

    flash_storage_read(&record, sizeof(record));

    if ((record.magic != NET_CONFIG_MAGIC) ||
        (record.version != NET_CONFIG_VERSION) ||
        (record.crc != net_config_record_crc(&record)))
    {
        return -1;
    }

    net_config_unpack_ip(record.ip, config->ip);
    net_config_unpack_ip(record.netmask, config->netmask);
    net_config_unpack_ip(record.gateway, config->gateway);

    return net_config_validate(config) ? 0 : -1;
}

int net_config_save(const net_config_t *config)
{
    net_config_record_t record;

    if (!net_config_validate(config))
    {
        return -1;
    }

    memset(&record, 0xFF, sizeof(record));
    record.magic = NET_CONFIG_MAGIC;
    record.version = NET_CONFIG_VERSION;
    record.ip = net_config_pack_ip(config->ip);
    record.netmask = net_config_pack_ip(config->netmask);
    record.gateway = net_config_pack_ip(config->gateway);
    record.flags = 0U;
    record.crc = net_config_record_crc(&record);

    return flash_storage_write(&record, sizeof(record));
}

int net_config_clear(void)
{
    return flash_storage_erase();
}
