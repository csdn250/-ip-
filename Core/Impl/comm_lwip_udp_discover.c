/**
 ****************************************************************************************************
 * @file        comm_lwip_udp_discover.c
 * @author      Codex
 * @date        2026-05-07
 * @brief       UDP 局域网设备发现实现。
 *
 * 上位机通过 UDP 广播发送发现口令，单片机回复自己的 IP、MAC、设备名称和 TCP 端口。
 ****************************************************************************************************
 */

#include <stdio.h>
#include <string.h>

#include "lwip/ip.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include "comm_lwip_udp_discover.h"
#include "lwip_comm.h"


#define UDP_DISCOVER_PORT          9999U
#define UDP_DISCOVER_TCP_PORT      8080U
#define UDP_DISCOVER_REQUEST       "DISCOVER_DEVICE"
#define UDP_DISCOVER_REQUEST_ALT   "FIND_STM32"
#define UDP_DISCOVER_DEVICE_NAME   "STM32H743_RAW_TCP_ADC"
#define UDP_DISCOVER_RX_SIZE       64U
#define UDP_DISCOVER_REPLY_SIZE    192U


static struct udp_pcb *s_discover_pcb;


static int comm_lwip_udp_discover_is_request(const char *aBuffer)
{
	if ((strcmp(aBuffer, UDP_DISCOVER_REQUEST) == 0) ||
		(strcmp(aBuffer, UDP_DISCOVER_REQUEST_ALT) == 0))
	{
		return 1;
	}

	return 0;
}


static uint16_t comm_lwip_udp_discover_format_reply(char *aBuffer, uint16_t aSize)
{
	int len;

	len = snprintf(aBuffer,
				   aSize,
				   "ACK,DEVICE=%s,IP=%d.%d.%d.%d,MASK=%d.%d.%d.%d,GW=%d.%d.%d.%d,MAC=%02X:%02X:%02X:%02X:%02X:%02X,TCP=%u,UDP=%u\r\n",
				   UDP_DISCOVER_DEVICE_NAME,
				   g_lwipdev.ip[0],
				   g_lwipdev.ip[1],
				   g_lwipdev.ip[2],
				   g_lwipdev.ip[3],
				   g_lwipdev.netmask[0],
				   g_lwipdev.netmask[1],
				   g_lwipdev.netmask[2],
				   g_lwipdev.netmask[3],
				   g_lwipdev.gateway[0],
				   g_lwipdev.gateway[1],
				   g_lwipdev.gateway[2],
				   g_lwipdev.gateway[3],
				   g_lwipdev.mac[0],
				   g_lwipdev.mac[1],
				   g_lwipdev.mac[2],
				   g_lwipdev.mac[3],
				   g_lwipdev.mac[4],
				   g_lwipdev.mac[5],
				   UDP_DISCOVER_TCP_PORT,
				   UDP_DISCOVER_PORT);

	if ((len <= 0) || (len >= (int)aSize))
	{
		return 0U;
	}

	return (uint16_t)len;
}


static void comm_lwip_udp_discover_send_reply(struct udp_pcb *aPcb,
											  const ip_addr_t *aAddr,
											  u16_t aPort,
											  const char *aReply,
											  uint16_t aLen)
{
	struct pbuf *pReply;

	pReply = pbuf_alloc(PBUF_TRANSPORT, aLen, PBUF_RAM);

	if (pReply == 0)
	{
		return;
	}

	memcpy(pReply->payload, aReply, aLen);
	udp_sendto(aPcb, pReply, aAddr, aPort);

	pbuf_free(pReply);
}


static void comm_lwip_udp_discover_on_receive(void *aArg,
											  struct udp_pcb *aPcb,
											  struct pbuf *aPbuf,
											  const ip_addr_t *aAddr,
											  u16_t aPort)
{
	char request[UDP_DISCOVER_RX_SIZE];
	char reply[UDP_DISCOVER_REPLY_SIZE];
	uint16_t copy_len;
	uint16_t reply_len;
	ip_addr_t broadcast_addr;

	(void)aArg;

	if ((aPbuf == 0) || (aPcb == 0) || (aAddr == 0))
	{
		if (aPbuf != 0)
		{
			pbuf_free(aPbuf);
		}

		return;
	}

	copy_len = (aPbuf->tot_len < (UDP_DISCOVER_RX_SIZE - 1U)) ?
			   aPbuf->tot_len :
			   (UDP_DISCOVER_RX_SIZE - 1U);
	pbuf_copy_partial(aPbuf, request, copy_len, 0U);
	request[copy_len] = '\0';

	if (comm_lwip_udp_discover_is_request(request))
	{
		reply_len = comm_lwip_udp_discover_format_reply(reply, sizeof(reply));

		if (reply_len > 0U)
		{
			/* 先按发送方地址回复，适合同网段或 DHCP 正常分配的场景。 */
			comm_lwip_udp_discover_send_reply(aPcb, aAddr, aPort, reply, reply_len);

			/* 再发一份受限广播，兼容同一二层网络但 IP 网段不一致的发现阶段。 */
			IP_ADDR4(&broadcast_addr, 255, 255, 255, 255);
			comm_lwip_udp_discover_send_reply(aPcb, &broadcast_addr, aPort, reply, reply_len);
		}
	}

	pbuf_free(aPbuf);
}


int comm_lwip_udp_discover_init(void)
{
	err_t err;

	if (s_discover_pcb != 0)
	{
		return 0;
	}

	s_discover_pcb = udp_new();

	if (s_discover_pcb == 0)
	{
		return -1;
	}

	ip_set_option(s_discover_pcb, SOF_BROADCAST);

	err = udp_bind(s_discover_pcb, IP_ADDR_ANY, UDP_DISCOVER_PORT);

	if (err != ERR_OK)
	{
		udp_remove(s_discover_pcb);
		s_discover_pcb = 0;
		return -2;
	}

	udp_recv(s_discover_pcb, comm_lwip_udp_discover_on_receive, 0);
	return 0;
}
