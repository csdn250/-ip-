/**
 ****************************************************************************************************
 * @file        lwip_comm.c
 * @author      
 * @version     V1.0
 * @date        2022-08-01
 * @brief       lwIP配置驱动
 * @license     Copyright (c) 2020-2032
 ****************************************************************************************************
 **/
 
#include "lwip_comm.h"
#include "net_config.h"
#include "netif/etharp.h"
#if LWIP_DHCP
#include "lwip/dhcp.h"
#endif
#include "lwip/init.h"
#include "ethernetif.h"
#include "lwip/timeouts.h"
#include "lwip/tcpip.h"
#include "./MALLOC/malloc.h"
#include "./SYSTEM/delay/delay.h"
#include "./SYSTEM/usart/usart.h"
#include "./BSP/ETHERNET/ethernet_chip.h"
#include <stdio.h>


__lwip_dev g_lwipdev;                                       /* lwip控制结构体 */
struct netif g_lwip_netif;                                  /* 定义一个全局的网络接口 */

#if LWIP_DHCP
uint32_t g_dhcp_fine_timer = 0;                             /* DHCP精细处理计时器 */
__IO uint8_t g_lwip_dhcp_state = LWIP_DHCP_OFF;             /* DHCP状态初始化 */
#endif

void lwip_link_status_updated(struct netif *netif);
#if LWIP_DHCP
void lwip_dhcp_process(struct netif *netif);
#endif

/**
 * @breif       lwip 默认IP设置
 * @param       lwipx: lwip控制结构体指针
 * @retval      无
 */
void lwip_comm_default_ip_set(__lwip_dev *lwipx)
{
    net_config_t saved_config;

    /* 默认远端IP为:192.168.1.134 */
    lwipx->remoteip[0] = 192;
    lwipx->remoteip[1] = 168;
    lwipx->remoteip[2] = 1;
    lwipx->remoteip[3] = 134;
    
    /* MAC地址设置 */
    lwipx->mac[0] = 0xB8;
    lwipx->mac[1] = 0xAE;
    lwipx->mac[2] = 0x1D;
    lwipx->mac[3] = 0x00;
    lwipx->mac[4] = 0x04;
    lwipx->mac[5] = 0x00;
    
    /* 默认本地IP为:192.168.1.30 */
    lwipx->ip[0] = 192;
    lwipx->ip[1] = 168;
    lwipx->ip[2] = 1;
    lwipx->ip[3] = 30;
    /* 默认子网掩码:255.255.255.0 */
    lwipx->netmask[0] = 255;
    lwipx->netmask[1] = 255;
    lwipx->netmask[2] = 255;
    lwipx->netmask[3] = 0;
    
    /* 默认网关:192.168.1.1 */
    lwipx->gateway[0] = 192;
    lwipx->gateway[1] = 168;
    lwipx->gateway[2] = 1;
    lwipx->gateway[3] = 1;
    lwipx->dhcpstatus = 0XFF; /* 当前工程固定使用静态IP */

    /* 如果 Flash 中存在上位机保存过的合法网络配置，则覆盖默认值。
     * Flash 数据带 magic/version/crc 校验，异常掉电或空 Flash 都会自动回退默认 IP。
     */
    if (net_config_load(&saved_config) == 0)
    {
        lwipx->ip[0] = saved_config.ip[0];
        lwipx->ip[1] = saved_config.ip[1];
        lwipx->ip[2] = saved_config.ip[2];
        lwipx->ip[3] = saved_config.ip[3];

        lwipx->netmask[0] = saved_config.netmask[0];
        lwipx->netmask[1] = saved_config.netmask[1];
        lwipx->netmask[2] = saved_config.netmask[2];
        lwipx->netmask[3] = saved_config.netmask[3];

        lwipx->gateway[0] = saved_config.gateway[0];
        lwipx->gateway[1] = saved_config.gateway[1];
        lwipx->gateway[2] = saved_config.gateway[2];
        lwipx->gateway[3] = saved_config.gateway[3];
    }
}

/**
 * @breif       lwIP初始化(lwIP启动的时候使用)
 * @param       无
 * @retval      0,成功
 *              1,网卡添加失败.
 */
uint8_t lwip_comm_init(void)
{
    struct netif *netif_init_flag;                  /* 调用netif_add()函数时的返回值,用于判断网络初始化是否成功 */
    ip_addr_t ipaddr;                               /* ip地址 */
    ip_addr_t netmask;                              /* 子网掩码 */
    ip_addr_t gw;                                   /* 默认网关 */
    uint8_t eth_linkstate;                          /* 网线连接状态标志位 */

    lwip_comm_default_ip_set(&g_lwipdev);           /* 设置默认IP等信息 */
    
    ethernet_init();                                /* 初始化以太网IO */
    
    lwip_init();                                    /* 初始化LWIP内核 */

#if LWIP_DHCP                                       /* 使用动态IP */
    ip_addr_set_zero_ip4(&ipaddr);                  /* 对IP地址、子网掩码及网关清零 */
    ip_addr_set_zero_ip4(&netmask);
    ip_addr_set_zero_ip4(&gw);
#else   /* 使用静态IP */
    IP4_ADDR(&ipaddr, g_lwipdev.ip[0], g_lwipdev.ip[1], g_lwipdev.ip[2], g_lwipdev.ip[3]);
    IP4_ADDR(&netmask, g_lwipdev.netmask[0], g_lwipdev.netmask[1], g_lwipdev.netmask[2], g_lwipdev.netmask[3]);
    IP4_ADDR(&gw, g_lwipdev.gateway[0], g_lwipdev.gateway[1], g_lwipdev.gateway[2], g_lwipdev.gateway[3]);
    printf("网卡en的MAC地址为:................%d.%d.%d.%d.%d.%d\r\n", g_lwipdev.mac[0], g_lwipdev.mac[1], g_lwipdev.mac[2], g_lwipdev.mac[3], g_lwipdev.mac[4], g_lwipdev.mac[5]);
    printf("静态IP地址........................%d.%d.%d.%d\r\n", g_lwipdev.ip[0], g_lwipdev.ip[1], g_lwipdev.ip[2], g_lwipdev.ip[3]);
    printf("子网掩码..........................%d.%d.%d.%d\r\n", g_lwipdev.netmask[0], g_lwipdev.netmask[1], g_lwipdev.netmask[2], g_lwipdev.netmask[3]);
    printf("默认网关..........................%d.%d.%d.%d\r\n", g_lwipdev.gateway[0], g_lwipdev.gateway[1], g_lwipdev.gateway[2], g_lwipdev.gateway[3]);
    g_lwipdev.dhcpstatus = 0XFF;
#endif  /* 向网卡列表中添加一个网口 */
    netif_init_flag = netif_add(&g_lwip_netif, (const ip_addr_t *)&ipaddr, (const ip_addr_t *)&netmask, (const ip_addr_t *)&gw, NULL, &ethernetif_init, &ethernet_input);

    
    if (netif_init_flag == NULL)
    {
        return 1;                                   /* 网卡添加失败 */
    }
    else                                            /* 网口添加成功后,设置netif为默认值,并且打开netif网口 */
    {
        netif_set_default(&g_lwip_netif);           /* 设置netif为默认网口 */
        lwip_link_status_updated(&g_lwip_netif);    /* 更新当前网线连接状态 */
        netif_set_link_callback(&g_lwip_netif, lwip_link_status_updated);
    }

#if LWIP_DHCP                                       /* 如果使用DHCP的话 */
    g_lwipdev.dhcpstatus = 0;                       /* DHCP标记为0 */
#endif
    
    /* 检测网线是否插入 */
    do
    {
        eth_linkstate = ethernet_read_phy(ETH_CHIP_BSR);
        delay_ms(1000);
        
        if ((eth_linkstate & ETH_CHIP_BSR_LINK_STATUS) == 4)
        {
            printf("检测到网线已经插入\r\n");
        }
        else
        {
            printf("检测到网线没有插入\r\n");
        }

    }while((eth_linkstate & ETH_CHIP_BSR_LINK_STATUS) == 0);
    
    return 0;                                       /* 操作OK. */
}

/**
 * @breif       当接收到数据后调用
 * @param       无
 * @retval      无
 */
void lwip_pkt_handle(void)
{
    /* 从网络缓冲区中读取接收到的数据包并将其发送给LWIP处理 */
    ethernetif_input(&g_lwip_netif);
}

/**
 * @brief       通知用户网络接口配置状态
 * @param       netif：网卡控制块
 * @retval      无
 */
void lwip_link_status_updated(struct netif *netif)
{
    if (netif_is_up(netif))
    {
#if LWIP_DHCP
        /* Update DHCP state machine */
        g_lwip_dhcp_state = LWIP_DHCP_START;
#endif /* LWIP_DHCP */
        printf ("The network cable is connected \r\n");
    }
    else
    {
#if LWIP_DHCP
        /* Update DHCP state machine */
        g_lwip_dhcp_state = LWIP_DHCP_LINK_DOWN;
#endif /* LWIP_DHCP */
        printf ("The network cable is not connected \r\n");
    }
}

/**
 * @breif       LWIP轮询处理
 * @param       无
 * @retval      无
 */
void lwip_periodic_handle(void)
{
    sys_check_timeouts();
#if LWIP_DHCP       /* 如果使用DHCP */
    /* Fine DHCP periodic process every 500ms */
    if (HAL_GetTick() - g_dhcp_fine_timer >= DHCP_FINE_TIMER_MSECS)
    {
        g_dhcp_fine_timer =  HAL_GetTick();
        /* process DHCP state machine */
        lwip_dhcp_process(&g_lwip_netif);
    }
#endif
}

#if LWIP_DHCP       /* 如果使用DHCP */

/**
 * @brief       lwIP的DHCP分配进程
 * @param       netif：网卡控制块
 * @retval      无
 */
void lwip_dhcp_process(struct netif *netif)
{
    uint32_t ip = 0;
    uint32_t netmask = 0;
    uint32_t gw = 0;
    struct dhcp *dhcp;
    uint8_t iptxt[20];              /* 存储已分配的IP地址 */
    g_lwipdev.dhcpstatus = 1;       /* DHCP标记为1 */
    
    /* 根据DHCP状态进入执行相应的动作 */
    switch (g_lwip_dhcp_state)
    {
        case LWIP_DHCP_START:
        {
            /* 对IP地址、网关地址及子网页码清零操作 */
            ip_addr_set_zero_ip4(&netif->ip_addr);
            ip_addr_set_zero_ip4(&netif->netmask);
            ip_addr_set_zero_ip4(&netif->gw);
            /* 设置DHCP的状态为等待分配IP地址 */
            g_lwip_dhcp_state = LWIP_DHCP_WAIT_ADDRESS;
            /* 开启DHCP */
            dhcp_start(netif);
        }
        break;

        case LWIP_DHCP_WAIT_ADDRESS:
        {
            ip = g_lwip_netif.ip_addr.addr;       /* 读取新IP地址 */
            netmask = g_lwip_netif.netmask.addr;  /* 读取子网掩码 */
            gw = g_lwip_netif.gw.addr;            /* 读取默认网关 */
            printf ("等待DHCP分配网络参数......\r\n");
            
            if (dhcp_supplied_address(netif)) 
            {
                printf ("DHCP分配成功......\r\n");
                g_lwip_dhcp_state = LWIP_DHCP_ADDRESS_ASSIGNED;
                sprintf((char *)iptxt, "%s", ip4addr_ntoa((const ip4_addr_t *)&netif->ip_addr));
                printf ("IP address assigned by a DHCP server: %s\r\n", iptxt);
                
                if (ip != 0)
                {
                    g_lwipdev.dhcpstatus = 2;         /* DHCP成功 */
                    printf("网卡en的MAC地址为:................%d.%d.%d.%d.%d.%d\r\n", g_lwipdev.mac[0], g_lwipdev.mac[1], g_lwipdev.mac[2], g_lwipdev.mac[3], g_lwipdev.mac[4], g_lwipdev.mac[5]);
                    /* 解析出通过DHCP获取到的IP地址 */
                    g_lwipdev.ip[3] = (uint8_t)(ip >> 24);
                    g_lwipdev.ip[2] = (uint8_t)(ip >> 16);
                    g_lwipdev.ip[1] = (uint8_t)(ip >> 8);
                    g_lwipdev.ip[0] = (uint8_t)(ip);
                    printf("通过DHCP获取到IP地址..............%d.%d.%d.%d\r\n", g_lwipdev.ip[0], g_lwipdev.ip[1], g_lwipdev.ip[2], g_lwipdev.ip[3]);
                    /* 解析通过DHCP获取到的子网掩码地址 */
                    g_lwipdev.netmask[3] = (uint8_t)(netmask >> 24);
                    g_lwipdev.netmask[2] = (uint8_t)(netmask >> 16);
                    g_lwipdev.netmask[1] = (uint8_t)(netmask >> 8);
                    g_lwipdev.netmask[0] = (uint8_t)(netmask);
                    printf("通过DHCP获取到子网掩码............%d.%d.%d.%d\r\n", g_lwipdev.netmask[0], g_lwipdev.netmask[1], g_lwipdev.netmask[2], g_lwipdev.netmask[3]);
                    /* 解析出通过DHCP获取到的默认网关 */
                    g_lwipdev.gateway[3] = (uint8_t)(gw >> 24);
                    g_lwipdev.gateway[2] = (uint8_t)(gw >> 16);
                    g_lwipdev.gateway[1] = (uint8_t)(gw >> 8);
                    g_lwipdev.gateway[0] = (uint8_t)(gw);
                    printf("通过DHCP获取到的默认网关..........%d.%d.%d.%d\r\n", g_lwipdev.gateway[0], g_lwipdev.gateway[1], g_lwipdev.gateway[2], g_lwipdev.gateway[3]);
                }
            }
            else
            {
                dhcp = (struct dhcp *)netif_get_client_data(netif, LWIP_NETIF_CLIENT_DATA_INDEX_DHCP);

                /* DHCP超时 */
                if (dhcp->tries > LWIP_MAX_DHCP_TRIES)
                {
                    printf ("DHCP分配失败，进入静态分配......\r\n");
                    g_lwip_dhcp_state = LWIP_DHCP_TIMEOUT;

                    /* 停止DHCP */
                    dhcp_stop(netif);
                    g_lwipdev.dhcpstatus = 0XFF;
                    /* 使用静态IP地址 */
                    IP4_ADDR(&(g_lwip_netif.ip_addr), g_lwipdev.ip[0], g_lwipdev.ip[1], g_lwipdev.ip[2], g_lwipdev.ip[3]);
                    IP4_ADDR(&(g_lwip_netif.netmask), g_lwipdev.netmask[0], g_lwipdev.netmask[1], g_lwipdev.netmask[2], g_lwipdev.netmask[3]);
                    IP4_ADDR(&(g_lwip_netif.gw), g_lwipdev.gateway[0], g_lwipdev.gateway[1], g_lwipdev.gateway[2], g_lwipdev.gateway[3]);
                    netif_set_addr(netif, &g_lwip_netif.ip_addr, &g_lwip_netif.netmask, &g_lwip_netif.gw);
                    sprintf((char *)iptxt, "%s", ip4addr_ntoa((const ip4_addr_t *)&netif->ip_addr));
                    printf ("Static IP address: %s\r\n", iptxt);
                }
            }
        }
        break;
        case LWIP_DHCP_LINK_DOWN:
        {
            /* 停止DHCP */
            dhcp_stop(netif);
            g_lwip_dhcp_state = LWIP_DHCP_OFF; 
        }
        break;
        default: break;
    }
}
#endif
