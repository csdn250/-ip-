/**
 ****************************************************************************************************
 * @file        comm_lwip_tcp.c
 * @author      Codex
 * @date        2026-05-07
 * @brief       lwIP RAW TCP 通信桥实现。
 *
 * 本文件把 lwIP RAW TCP 的 `tcp_write()`、`tcp_output()` 和 TCP PCB 连接状态
 * 封装成 `comm_bridge_t`，供业务层 `device_service.c` 使用。
 ****************************************************************************************************
 */

#include <string.h>

#include "comm_lwip_tcp.h"
#include "device_service.h"


#define COMM_LWIP_TCP_TX_SLOT_NUM   2U
#define COMM_LWIP_TCP_TX_SLOT_SIZE  1460U


typedef struct
{
	uint8_t data[COMM_LWIP_TCP_TX_SLOT_SIZE];
	uint16_t len;
	uint16_t offset;
} comm_lwip_tcp_tx_slot_t;


/* 当前 TCP 客户端连接，由 lwIP RAW TCP accept/close 回调维护。 */
static struct tcp_pcb *s_client_pcb;
static comm_lwip_tcp_tx_slot_t s_tx_slots[COMM_LWIP_TCP_TX_SLOT_NUM];
static uint8_t s_tx_read_index;
static uint8_t s_tx_write_index;
static uint8_t s_tx_count;


/**
 * @brief 清空 TCP 桥接层内部发送队列。
 */
static void comm_lwip_tcp_clear_queue(void)
{
	s_tx_read_index = 0U;
	s_tx_write_index = 0U;
	s_tx_count = 0U;
}


/**
 * @brief 尽可能把内部发送队列中的数据提交给 lwIP。
 *
 * 本函数不会阻塞等待 ACK。若 tcp_sndbuf() 暂时不足，会保留当前发送进度，
 * 等待 tcp_sent 回调或 poll 回调再次调用本函数继续发送。
 */
static void comm_lwip_tcp_pump(void)
{
	comm_lwip_tcp_tx_slot_t *slot;
	uint16_t remain;
	uint16_t send_len;
	uint16_t sndbuf;
	err_t err;

	if (s_client_pcb == 0)
	{
		return;
	}

	while (s_tx_count > 0U)
	{
		slot = &s_tx_slots[s_tx_read_index];

		if (slot->offset >= slot->len)
		{
			slot->len = 0U;
			slot->offset = 0U;
			s_tx_read_index = (uint8_t)((s_tx_read_index + 1U) % COMM_LWIP_TCP_TX_SLOT_NUM);
			--s_tx_count;
			continue;
		}

		sndbuf = tcp_sndbuf(s_client_pcb);

		if (sndbuf == 0U)
		{
			break;
		}

		remain = (uint16_t)(slot->len - slot->offset);
		send_len = remain;

		if (send_len > sndbuf)
		{
			send_len = sndbuf;
		}

		if (send_len > TCP_MSS)
		{
			send_len = TCP_MSS;
		}

		err = tcp_write(s_client_pcb,
						&slot->data[slot->offset],
						send_len,
						TCP_WRITE_FLAG_COPY);

		if (err != ERR_OK)
		{
			break;
		}

		slot->offset = (uint16_t)(slot->offset + send_len);
		tcp_output(s_client_pcb);
	}
}


/**
 * @brief 通过当前 TCP 连接发送应用层数据。
 *
 * @param aData 待发送数据首地址。该数据是 TCP payload，不包含 TCP/IP/以太网头。
 * @param aLen  待发送数据长度，单位字节。
 *
 * @retval 0  发送请求成功提交给 lwIP。
 * @retval -1 当前没有 TCP 客户端、输入指针为空或长度为 0。
 * @retval -2 lwIP 当前 TCP 发送缓存不足。
 * @retval -3 `tcp_write()` 返回失败。
 *
 * NOTE：本函数不会阻塞等待对方真正收到数据，只表示数据已经提交给 lwIP。
 */
static int comm_lwip_tcp_send_impl(const uint8_t *aData, uint16_t aLen)
{
	if ((s_client_pcb == 0) || (aData == 0) || (aLen == 0U))
	{
		return -1;
	}

	if (aLen > COMM_LWIP_TCP_TX_SLOT_SIZE)
	{
		return -2;
	}

	if (s_tx_count >= COMM_LWIP_TCP_TX_SLOT_NUM)
	{
		return -3;
	}

	memcpy(s_tx_slots[s_tx_write_index].data, aData, aLen);
	s_tx_slots[s_tx_write_index].len = aLen;
	s_tx_slots[s_tx_write_index].offset = 0U;
	s_tx_write_index = (uint8_t)((s_tx_write_index + 1U) % COMM_LWIP_TCP_TX_SLOT_NUM);
	++s_tx_count;

	comm_lwip_tcp_pump();
	return 0;
}


/**
 * @brief 查询发送队列是否还有空位可提交一帧。
 */
static int comm_lwip_tcp_can_send_impl(uint16_t aLen)
{
	if ((s_client_pcb == 0) || (aLen == 0U) || (aLen > COMM_LWIP_TCP_TX_SLOT_SIZE))
	{
		return 0;
	}

	return (s_tx_count < COMM_LWIP_TCP_TX_SLOT_NUM);
}


/**
 * @brief 查询 TCP 通信桥当前是否已经连接上位机。
 *
 * @retval 1 当前存在可用 TCP 客户端 PCB。
 * @retval 0 当前没有上位机连接。
 */
static int comm_lwip_tcp_is_connected_impl(void)
{
	return (s_client_pcb != 0);
}


static const comm_bridge_t s_comm_lwip_tcp_bridge =
{
	comm_lwip_tcp_send_impl,
	comm_lwip_tcp_can_send_impl,
	comm_lwip_tcp_is_connected_impl
};


/**
 * @brief 获取 lwIP TCP 通信桥实例。
 *
 * @return 指向静态通信桥实例的指针，整个运行期有效。
 *
 * 业务层通过该返回值调用 send/is_connected，不需要知道 TCP PCB 的存在。
 */
const comm_bridge_t *comm_lwip_tcp_get_bridge(void)
{
	return &s_comm_lwip_tcp_bridge;
}


/**
 * @brief 设置当前 TCP 客户端 PCB。
 *
 * @param aPcb TCP accept 成功时传入 newpcb；连接关闭或异常时传入 0。
 *
 * 连接生命周期由 lwIP 管理，本模块只缓存“当前可用连接”的指针。
 */
void comm_lwip_tcp_set_client(struct tcp_pcb *aPcb)
{
	if (aPcb != s_client_pcb)
	{
		comm_lwip_tcp_clear_queue();
	}

	s_client_pcb = aPcb;
}


/**
 * @brief 将 TCP 接收数据转交给业务层。
 *
 * @param aData lwIP 解封装后的 TCP payload。
 * @param aLen  payload 长度，单位字节。
 *
 * 到达本函数时，以太网头、IP 头、TCP 头已经全部由 lwIP 处理完成。
 */
void comm_lwip_tcp_on_receive(const uint8_t *aData, uint16_t aLen)
{
	device_service_on_rx(aData, aLen);
}


/**
 * @brief TCP ACK 回调入口，继续提交发送队列中的剩余数据。
 */
void comm_lwip_tcp_on_sent(void)
{
	comm_lwip_tcp_pump();
}


/**
 * @brief TCP poll 回调入口，兜底推动发送队列。
 */
void comm_lwip_tcp_poll(void)
{
	comm_lwip_tcp_pump();
}
