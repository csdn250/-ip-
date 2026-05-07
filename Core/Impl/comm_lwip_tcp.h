#ifndef COMM_LWIP_TCP_H
#define COMM_LWIP_TCP_H

#include <stdint.h>
#include "lwip/tcp.h"
#include "comm_bridge.h"

/**
 * @brief 获取 lwIP RAW TCP 通信桥实例。
 *
 * 返回值交给 `device_service_init()` 后，业务层即可通过统一的
 * `comm_bridge_t` 接口发送数据，而不需要直接持有 `struct tcp_pcb`。
 */
const comm_bridge_t *comm_lwip_tcp_get_bridge(void);

/**
 * @brief 设置当前已连接的 TCP 客户端。
 *
 * 调用时机：
 * - 客户端连接成功时，由 `lwip_tcp_server_accept()` 传入 newpcb。
 * - 客户端断开时，传入 NULL 清空连接。
 */
void comm_lwip_tcp_set_client(struct tcp_pcb *pcb);

/**
 * @brief RAW TCP 接收回调的桥接入口。
 *
 * data/len 是 TCP payload 数据，上层可直接按应用协议解析。
 */
void comm_lwip_tcp_on_receive(const uint8_t *data, uint16_t len);

#endif
