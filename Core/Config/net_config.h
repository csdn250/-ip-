/**
 ****************************************************************************************************
 * @file        net_config.h
 * @author      Codex
 * @date        2026-05-07
 * @brief       网络参数持久化配置接口。
 *
 * 网络参数包括 IP 地址、子网掩码、网关和 TCP 服务端口。配置保存到片内 Flash，
 * 掉电后仍然有效。
 ****************************************************************************************************
 */

#ifndef NET_CONFIG_H
#define NET_CONFIG_H


#include <stdint.h>


#define NET_CONFIG_DEFAULT_TCP_PORT  8080U


typedef struct
{
	uint8_t ip[4];
	uint8_t netmask[4];
	uint8_t gateway[4];
	uint16_t tcp_port;
} net_config_t;


void net_config_get_default(net_config_t *aConfig);
int net_config_validate(const net_config_t *aConfig);
int net_config_load(net_config_t *aConfig);
int net_config_save(const net_config_t *aConfig);
int net_config_clear(void);


#endif /* NET_CONFIG_H */
