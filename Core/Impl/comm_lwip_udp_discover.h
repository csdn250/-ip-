/**
 ****************************************************************************************************
 * @file        comm_lwip_udp_discover.h
 * @author      Codex
 * @date        2026-05-07
 * @brief       UDP 局域网设备发现接口。
 *
 * 本模块只负责“发现设备”，不承载 ADC 数据和网络配置命令。业务数据仍然走 TCP。
 ****************************************************************************************************
 */

#ifndef COMM_LWIP_UDP_DISCOVER_H
#define COMM_LWIP_UDP_DISCOVER_H


/**
 * @brief 初始化 UDP 发现服务。
 *
 * 初始化成功后，单片机会在 9999 端口监听上位机广播搜索口令。
 *
 * @retval 0 初始化成功。
 * @retval <0 初始化失败。
 */
int comm_lwip_udp_discover_init(void);


#endif /* COMM_LWIP_UDP_DISCOVER_H */
