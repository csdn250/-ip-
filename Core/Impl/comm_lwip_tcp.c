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


#define COMM_LWIP_TCP_TX_SLOT_NUM   8U
#define COMM_LWIP_TCP_TX_SLOT_SIZE  2048U


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

	if (aLen > tcp_sndbuf(s_client_pcb))
	{
		return -2;
	}

	/* tcp_write() 只接收应用层 payload，后续封装由 lwIP 和以太网驱动完成。 */
	if (tcp_write(s_client_pcb, aData, aLen, TCP_WRITE_FLAG_COPY) != ERR_OK)
	{
		return -3;
	}

	tcp_output(s_client_pcb);
	return 0;
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
