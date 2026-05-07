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


/**
 * @brief 将 4 字节 IPv4 地址打包为 32 位整数。
 *
 * @param aIp 输入 IP 数组，顺序为 a.b.c.d。
 *
 * @return 打包后的网络配置存储值。
 */
static uint32_t net_config_pack_ip(const uint8_t aIp[4])
{
	return ((uint32_t)aIp[0] << 24) |
		   ((uint32_t)aIp[1] << 16) |
		   ((uint32_t)aIp[2] << 8) |
		   ((uint32_t)aIp[3]);
}


/**
 * @brief 将 32 位整数还原为 4 字节 IPv4 地址。
 *
 * @param aValue Flash 记录中保存的 IP 整数。
 * @param aIp    输出 IP 数组，长度必须至少为 4 字节。
 */
static void net_config_unpack_ip(uint32_t aValue, uint8_t aIp[4])
{
	aIp[0] = (uint8_t)(aValue >> 24);
	aIp[1] = (uint8_t)(aValue >> 16);
	aIp[2] = (uint8_t)(aValue >> 8);
	aIp[3] = (uint8_t)aValue;
}


/**
 * @brief 计算 CRC32 校验值。
 *
 * @param aData 输入数据首地址。
 * @param aLen  输入数据长度，单位字节。
 *
 * @return CRC32 结果，用于判断 Flash 配置记录是否完整。
 */
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


/**
 * @brief 计算网络配置记录的 CRC。
 *
 * @param aRecord 待校验记录。
 *
 * @return 记录前 NET_CONFIG_CRC_LEN 字节的 CRC32。
 */
static uint32_t net_config_record_crc(const net_config_record_t *aRecord)
{
	return net_config_crc32((const uint8_t *)aRecord, NET_CONFIG_CRC_LEN);
}


/**
 * @brief 校验 IPv4 地址是否适合作为普通设备地址。
 *
 * @param aIp 待校验 IP。
 *
 * @retval 1 合法。
 * @retval 0 非法，例如 0.x.x.x、127.x.x.x、组播地址或全 255。
 */
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


/**
 * @brief 校验子网掩码是否合法。
 *
 * @param aMask 待校验子网掩码。
 *
 * @retval 1 合法，且二进制形式为连续 1 后接连续 0。
 * @retval 0 非法。
 */
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


/**
 * @brief 校验 TCP 端口是否合法。
 *
 * @param aPort 待校验端口。
 *
 * @retval 1 合法。
 * @retval 0 非法。端口 0 表示未指定端口，不能作为 TCP Server 监听端口。
 */
static int net_config_port_is_valid(uint16_t aPort)
{
	if (aPort < NET_CONFIG_PORT_MIN)
	{
		return 0;
	}

	return 1;
}


/**
 * @brief 获取默认网络配置。
 *
 * @param aConfig 输出配置，由调用者提供存储空间。
 *
 * 默认值用于 Flash 中没有有效配置、用户恢复出厂网络配置等场景。
 */
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


/**
 * @brief 校验完整网络配置是否合法。
 *
 * @param aConfig 待校验配置。
 *
 * @retval 1 IP、子网掩码、网关、TCP 端口均合法。
 * @retval 0 配置为空或任一字段非法。
 */
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


/**
 * @brief 从 Flash 读取网络配置。
 *
 * @param aConfig 输出配置，由调用者提供存储空间。
 *
 * @retval 0 读取成功，且 magic、version、crc、字段合法性全部通过。
 * @retval <0 Flash 中没有有效配置，调用者应回退到默认配置。
 */
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


/**
 * @brief 保存网络配置到 Flash。
 *
 * @param aConfig 待保存配置，本函数只读取其内容。
 *
 * @retval 0 保存成功。
 * @retval <0 参数非法或 Flash 写入失败。
 */
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


/**
 * @brief 清除 Flash 中保存的网络配置。
 *
 * @retval 0 擦除成功。
 * @retval <0 Flash 擦除失败。
 */
int net_config_clear(void)
{
	return flash_storage_erase();
}
