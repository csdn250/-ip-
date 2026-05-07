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
#define UDP_DISCOVER_REQUEST       "DISCOVER_DEVICE"
#define UDP_DISCOVER_REQUEST_ALT   "FIND_STM32"
#define UDP_DISCOVER_DEVICE_NAME   "STM32H743_RAW_TCP_ADC"
#define UDP_DISCOVER_RX_SIZE       64U
#define UDP_DISCOVER_REPLY_SIZE    192U


static struct udp_pcb *s_discover_pcb;


/**
 * @brief 判断 UDP payload 是否为设备发现口令。
 *
 * @param aBuffer 已经补 `\0` 的接收字符串。
 *
 * @retval 1 是支持的发现口令。
 * @retval 0 不是发现口令。
 */
static int comm_lwip_udp_discover_is_request(const char *aBuffer)
{
	uint16_t len;

	if (aBuffer == 0)
	{
		return 0;
	}

	len = (uint16_t)strlen(aBuffer);

	while (len > 0U)
	{
		if ((aBuffer[len - 1U] != '\r') &&
			(aBuffer[len - 1U] != '\n') &&
			(aBuffer[len - 1U] != ' ') &&
			(aBuffer[len - 1U] != '\t'))
		{
			break;
		}

		--len;
	}

	if (((strlen(UDP_DISCOVER_REQUEST) == len) &&
		 (strncmp(aBuffer, UDP_DISCOVER_REQUEST, len) == 0)) ||
		((strlen(UDP_DISCOVER_REQUEST_ALT) == len) &&
		 (strncmp(aBuffer, UDP_DISCOVER_REQUEST_ALT, len) == 0)))
	{
		return 1;
	}

	return 0;
}


/**
 * @brief 生成 UDP 设备发现回复帧。
 *
 * @param aBuffer 输出缓冲区，由调用者提供。
 * @param aSize   输出缓冲区长度，单位字节。
 *
 * @return 实际写入长度；返回 0 表示缓冲区不足或格式化失败。
 *
 * 回复帧会带上当前 IP、MASK、GW、MAC、TCP 端口和 UDP 发现端口，上位机
 * 应解析其中的 IP 和 TCP 字段后再建立 TCP 连接。
 */
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
				   g_lwipdev.tcp_port,
				   UDP_DISCOVER_PORT);

	if ((len <= 0) || (len >= (int)aSize))
	{
		return 0U;
	}

	return (uint16_t)len;
}


/**
 * @brief 发送 UDP 发现回复。
 *
 * @param aPcb   UDP 控制块。
 * @param aAddr  目标 IP 地址。
 * @param aPort  目标 UDP 端口。
 * @param aReply 回复数据首地址。
 * @param aLen   回复数据长度。
 *
 * 本函数内部申请并释放发送 pbuf。调用者不需要管理 pbuf 生命周期。
 */
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


/**
 * @brief UDP 发现接收回调。
 *
 * @param aArg  用户参数，当前未使用。
 * @param aPcb  收到数据的 UDP 控制块。
 * @param aPbuf lwIP 接收 pbuf，本函数负责释放。
 * @param aAddr 发送方 IP 地址。
 * @param aPort 发送方 UDP 端口。
 *
 * 收到合法发现口令后，本函数会先单播回复发送方，再广播回复一份，用于兼容
 * 同一二层网络但 IP 网段不一致的设备发现阶段。
 */
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


/**
 * @brief 初始化 UDP 设备发现服务。
 *
 * @retval 0 初始化成功，或已经初始化过。
 * @retval -1 UDP PCB 创建失败。
 * @retval -2 UDP 端口绑定失败。
 *
 * 调用时机应在 lwip_comm_init() 成功之后，此时 netif、IP、MAC 已经准备好。
 */
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
