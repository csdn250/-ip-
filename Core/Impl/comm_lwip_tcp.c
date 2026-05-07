/**
 ****************************************************************************************************
 * @file        comm_lwip_tcp.c
 * @author      Codex
 * @date        2026-05-07
 * @brief       lwIP RAW TCP 通信桥实现。
 ****************************************************************************************************
 */

#include "comm_lwip_tcp.h"
#include "device_service.h"


/* 当前 TCP 客户端连接，由 lwIP RAW TCP accept/close 回调维护。 */
static struct tcp_pcb *s_client_pcb;

static int comm_lwip_tcp_send_impl(const uint8_t *aData, uint16_t aLen)
{
	if ((s_client_pcb == 0) || (aData == 0) || (aLen == 0U))
	{
		return -1;
	}

	if (aLen > tcp_sndbuf(s_client_pcb))
	{
		/* NOTE：发送缓存不足时直接返回失败，避免在业务层阻塞。 */
		return -2;
	}

	/* tcp_write() 只接收应用层 payload，TCP/IP/以太网头由 lwIP 后续自动封装。 */
	if (tcp_write(s_client_pcb, aData, aLen, TCP_WRITE_FLAG_COPY) != ERR_OK)
	{
		return -3;
	}

	tcp_output(s_client_pcb);
	return 0;
}

static int comm_lwip_tcp_is_connected_impl(void)
{
	return (s_client_pcb != 0);
}


static const comm_bridge_t s_comm_lwip_tcp_bridge =
{
	comm_lwip_tcp_send_impl,
	comm_lwip_tcp_is_connected_impl
};


const comm_bridge_t *comm_lwip_tcp_get_bridge(void)
{
	return &s_comm_lwip_tcp_bridge;
}


void comm_lwip_tcp_set_client(struct tcp_pcb *aPcb)
{
	/* 连接生命周期由 lwIP 管理，本模块只缓存当前可用连接。 */
	s_client_pcb = aPcb;
}


void comm_lwip_tcp_on_receive(const uint8_t *aData, uint16_t aLen)
{
	/* 到达本函数时，lwIP 已经完成 Ethernet/IP/TCP 解封装。 */
	device_service_on_rx(aData, aLen);
}
