/**
 ****************************************************************************************************
 * @file        net_config.h
 * @author      Codex
 * @date        2026-05-07
 * @brief       网络参数持久化配置接口。
 *
 * 网络参数包括 IP 地址、子网掩码和网关。配置保存到片内 Flash，掉电后仍然有效。
 ****************************************************************************************************
 */

#ifndef NET_CONFIG_H
#define NET_CONFIG_H


#include <stdint.h>


typedef struct
{
	uint8_t ip[4];
	uint8_t netmask[4];
	uint8_t gateway[4];
} net_config_t;


/**
 * @brief 获取默认静态网络配置。
 *
 * @param aConfig 输出配置，由调用者提供存储空间。
 */
void net_config_get_default(net_config_t *aConfig);

/**
 * @brief 校验网络配置是否合法。
 *
 * @param aConfig 待校验配置。
 *
 * @retval 1 配置合法。
 * @retval 0 配置非法。
 */
int net_config_validate(const net_config_t *aConfig);

/**
 * @brief 从 Flash 读取网络配置。
 *
 * @param aConfig 输出配置，由调用者提供存储空间。
 *
 * @retval 0 读取并校验成功。
 * @retval <0 Flash 中没有有效配置。
 */
int net_config_load(net_config_t *aConfig);

/**
 * @brief 将网络配置写入 Flash。
 *
 * @param aConfig 待保存配置。
 *
 * @retval 0 写入成功。
 * @retval <0 参数非法或 Flash 写入失败。
 */
int net_config_save(const net_config_t *aConfig);

/**
 * @brief 清除 Flash 中保存的网络配置。
 *
 * @retval 0 清除成功。
 * @retval <0 Flash 擦除失败。
 */
int net_config_clear(void);


#endif /* NET_CONFIG_H */
