/**
 ****************************************************************************************************
 * @file        lwip_comm.h
 * @author      ALIENTEK / Codex
 * @date        2026-05-07
 * @brief       lwIP 网络初始化和全局网络状态接口。
 ****************************************************************************************************
 */

#ifndef LWIP_COMM_H
#define LWIP_COMM_H


#include <stdint.h>

#include "BSP/ETHERNET/ethernet.h"
#include "netif/etharp.h"


#if LWIP_DHCP
#define LWIP_DHCP_OFF                   (uint8_t)0
#define LWIP_DHCP_START                 (uint8_t)1
#define LWIP_DHCP_WAIT_ADDRESS          (uint8_t)2
#define LWIP_DHCP_ADDRESS_ASSIGNED      (uint8_t)3
#define LWIP_DHCP_TIMEOUT               (uint8_t)4
#define LWIP_DHCP_LINK_DOWN             (uint8_t)5
#define LWIP_MAX_DHCP_TRIES             (uint8_t)4
#endif /* LWIP_DHCP */


typedef struct
{
	uint8_t mac[6];
	uint8_t remoteip[4];
	uint8_t ip[4];
	uint8_t netmask[4];
	uint8_t gateway[4];
	uint16_t tcp_port;
	uint8_t dhcpstatus;
} __lwip_dev;


extern __lwip_dev g_lwipdev;


void lwip_periodic_handle(void);
void lwip_comm_default_ip_set(__lwip_dev *lwipx);
uint8_t lwip_comm_init(void);
void lwip_pkt_handle(void);


#endif /* LWIP_COMM_H */
