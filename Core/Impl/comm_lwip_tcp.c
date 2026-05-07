#include "comm_lwip_tcp.h"
#include "device_service.h"

/* 当前 TCP 客户端连接。
 * 该指针由 lwIP RAW TCP accept/close 回调维护，业务层不直接访问。
 */
static struct tcp_pcb *s_client_pcb;

static int comm_lwip_tcp_send_impl(const uint8_t *data, uint16_t len)
{
    if ((s_client_pcb == 0) || (data == 0) || (len == 0))
    {
        return -1;
    }

    if (len > tcp_sndbuf(s_client_pcb))
    {
        /* lwIP 当前发送缓冲不足。
         * 这里直接返回失败，避免阻塞业务层；后续如需可靠排队，可增加发送队列。
         */
        return -2;
    }

    /* tcp_write() 只接收应用层 payload。
     * 后续 TCP 头、IP 头、以太网头由 lwIP 和 ethernetif 自动封装。
     * 上位机 socket recv() 收到的也正是这里传入的 payload。
     */
    if (tcp_write(s_client_pcb, data, len, TCP_WRITE_FLAG_COPY) != ERR_OK)
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

void comm_lwip_tcp_set_client(struct tcp_pcb *pcb)
{
    /* 连接生命周期由 lwIP 管理。
     * 本模块只缓存当前可用连接，供 comm_bridge_t::send 使用。
     */
    s_client_pcb = pcb;
}

void comm_lwip_tcp_on_receive(const uint8_t *data, uint16_t len)
{
    /* 到达本函数时，lwIP 已经完成 Ethernet/IP/TCP 解封装。
     * data 指向应用层数据，可交给业务层按文本协议处理。
     */
    device_service_on_rx(data, len);
}
