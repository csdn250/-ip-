/**
  ******************************************************************************
  * @file    ethernetif.h
  * @author  MCD Application Team
  * @brief   Ethernet interface header file.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2017 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

#ifndef __ETHERNETIF_H__
#define __ETHERNETIF_H__
#include "lwip/err.h"
#include "lwip/netif.h"


err_t   ethernetif_init(struct netif *netif);   /* 网卡初始化函数 */
void    ethernetif_input(struct netif *netif);  /* 数据包输入函数 */
u32_t   sys_now(void);                          /* 提供lwIP时基 */
#endif
