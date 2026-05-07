/**
 ****************************************************************************************************
 * @file        net_config.c
 * @author      Codex
 * @date        2026-05-07
 * @brief       网络参数持久化配置实现。
 ****************************************************************************************************
 */

#include <string.h>

#include "flash_storage.h"
#include "net_config.h"


#define NET_CONFIG_MAGIC    0x54454E41UL
#define NET_CONFIG_VERSION  2UL
#define NET_CONFIG_CRC_LEN  24U
#define NET_CONFIG_PORT_MIN 1U


typedef struct
{
	uint32_t magic;
	uint32_t version;
	uint32_t ip;
	uint32_t netmask;
	uint32_t gateway;
	uint32_t tcp_port;
	uint32_t crc;
	uint32_t reserved;
} net_config_record_t;


static uint32_t net_config_pack_ip(const uint8_t aIp[4])
{
	return ((uint32_t)aIp[0] << 24) |
		   ((uint32_t)aIp[1] << 16) |
		   ((uint32_t)aIp[2] << 8) |
		   ((uint32_t)aIp[3]);
}


static void net_config_unpack_ip(uint32_t aValue, uint8_t aIp[4])
{
	aIp[0] = (uint8_t)(aValue >> 24);
	aIp[1] = (uint8_t)(aValue >> 16);
	aIp[2] = (uint8_t)(aValue >> 8);
	aIp[3] = (uint8_t)aValue;
}


static uint32_t net_config_crc32(const uint8_t *aData, uint32_t aLen)
{
	uint32_t crc = 0xFFFFFFFFUL;
	uint32_t i;
	uint8_t bit;

	while (aLen > 0U)
	{
		crc ^= *aData;
		++aData;
		--aLen;

		for (i = 0U; i < 8U; ++i)
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


static uint32_t net_config_record_crc(const net_config_record_t *aRecord)
{
	return net_config_crc32((const uint8_t *)aRecord, NET_CONFIG_CRC_LEN);
}


static int net_config_ip_is_valid(const uint8_t aIp[4])
{
	if ((aIp[0] == 0U) || (aIp[0] == 127U) || (aIp[0] >= 224U))
	{
		return 0;
	}

	if ((aIp[0] == 255U) && (aIp[1] == 255U) && (aIp[2] == 255U) && (aIp[3] == 255U))
	{
		return 0;
	}

	return 1;
}


static int net_config_mask_is_valid(const uint8_t aMask[4])
{
	uint32_t value = net_config_pack_ip(aMask);
	uint32_t inverse = ~value;

	if ((value == 0U) || (value == 0xFFFFFFFFUL))
	{
		return 0;
	}

	return ((inverse & (inverse + 1U)) == 0U) ? 1 : 0;
}


static int net_config_port_is_valid(uint16_t aPort)
{
	if (aPort < NET_CONFIG_PORT_MIN)
	{
		return 0;
	}

	return 1;
}


void net_config_get_default(net_config_t *aConfig)
{
	if (aConfig == 0)
	{
		return;
	}

	aConfig->ip[0] = 192U;
	aConfig->ip[1] = 168U;
	aConfig->ip[2] = 1U;
	aConfig->ip[3] = 30U;

	aConfig->netmask[0] = 255U;
	aConfig->netmask[1] = 255U;
	aConfig->netmask[2] = 255U;
	aConfig->netmask[3] = 0U;

	aConfig->gateway[0] = 192U;
	aConfig->gateway[1] = 168U;
	aConfig->gateway[2] = 1U;
	aConfig->gateway[3] = 1U;

	aConfig->tcp_port = NET_CONFIG_DEFAULT_TCP_PORT;
}


int net_config_validate(const net_config_t *aConfig)
{
	if (aConfig == 0)
	{
		return 0;
	}

	if (!net_config_ip_is_valid(aConfig->ip))
	{
		return 0;
	}

	if (!net_config_mask_is_valid(aConfig->netmask))
	{
		return 0;
	}

	if (!net_config_ip_is_valid(aConfig->gateway))
	{
		return 0;
	}

	if (!net_config_port_is_valid(aConfig->tcp_port))
	{
		return 0;
	}

	return 1;
}


int net_config_load(net_config_t *aConfig)
{
	net_config_record_t record;

	if (aConfig == 0)
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

	net_config_unpack_ip(record.ip, aConfig->ip);
	net_config_unpack_ip(record.netmask, aConfig->netmask);
	net_config_unpack_ip(record.gateway, aConfig->gateway);
	aConfig->tcp_port = (uint16_t)record.tcp_port;

	return net_config_validate(aConfig) ? 0 : -1;
}


int net_config_save(const net_config_t *aConfig)
{
	net_config_record_t record;

	if (!net_config_validate(aConfig))
	{
		return -1;
	}

	memset(&record, 0xFF, sizeof(record));

	record.magic = NET_CONFIG_MAGIC;
	record.version = NET_CONFIG_VERSION;
	record.ip = net_config_pack_ip(aConfig->ip);
	record.netmask = net_config_pack_ip(aConfig->netmask);
	record.gateway = net_config_pack_ip(aConfig->gateway);
	record.tcp_port = aConfig->tcp_port;
	record.crc = net_config_record_crc(&record);

	return flash_storage_write(&record, sizeof(record));
}


int net_config_clear(void)
{
	return flash_storage_erase();
}
