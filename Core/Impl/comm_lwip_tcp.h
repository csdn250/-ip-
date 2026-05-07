/**
 ****************************************************************************************************
 * @file        comm_lwip_tcp.h
 * @author      Codex
 * @date        2026-05-07
 * @brief       lwIP RAW TCP 通信桥实现接口。
 *
 * 本模块把 lwIP 的 TCP PCB 操作封装为 comm_bridge_t，业务层不直接调用 tcp_write()。
 ****************************************************************************************************
 */

#ifndef COMM_LWIP_TCP_H
#define COMM_LWIP_TCP_H


#include <stdint.h>

#include "lwip/tcp.h"

#include "comm_bridge.h"


/**
 * @brief 获取 lwIP RAW TCP 通信桥实例。
 *
 * 返回值交给 device_service_init() 后，业务层即可通过统一的 comm_bridge_t 接口发送数据。
 *
 * @return 指向静态通信桥实例的指针，整个运行期有效。
 */
const comm_bridge_t *comm_lwip_tcp_get_bridge(void);

/**
 * @brief 设置当前已经连接的 TCP 客户端。
 *
 * @param aPcb 客户端连接成功时传入 newpcb，客户端断开时传入 0 清空连接。
 */
void comm_lwip_tcp_set_client(struct tcp_pcb *aPcb);

/**
 * @brief RAW TCP 接收回调的桥接入口。
 *
 * @param aData 指向 TCP payload。
 * @param aLen  payload 长度，单位字节。
 */
void comm_lwip_tcp_on_receive(const uint8_t *aData, uint16_t aLen);

/**
 * @brief TCP 已发送数据被上位机 ACK 后的桥接入口。
 *
 * lwIP 的 tcp_sent 回调应调用本函数，用于继续从内部发送队列提交后续数据。
 */
void comm_lwip_tcp_on_sent(void);

/**
 * @brief TCP 轮询时的桥接入口。
 *
 * 在没有 ACK 回调但发送窗口已经恢复的情况下，poll 调用可继续推动发送队列。
 */
void comm_lwip_tcp_poll(void);


#endif /* COMM_LWIP_TCP_H */
