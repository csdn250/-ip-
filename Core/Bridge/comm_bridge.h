#ifndef COMM_BRIDGE_H
#define COMM_BRIDGE_H

#include <stdint.h>

/**
 * @brief 通信桥接接口。
 *
 * 设计意图：
 * 业务层只依赖本接口，不直接依赖 lwIP、串口、CAN 或其他具体通信实现。
 * 当前工程的实现是 `comm_lwip_tcp.c`，底层使用 lwIP RAW TCP。
 * 后续如果需要改成 UDP、串口或其他链路，只需要替换本接口的实现，
 * `device_service.c` 中的业务逻辑不需要跟着修改。
 */
typedef struct
{
    /**
     * @brief 发送一段应用层数据。
     *
     * @param data 指向待发送的应用层 payload，不包含 TCP/IP/以太网头。
     * @param len  payload 长度，单位字节。
     *
     * @retval 0 发送请求已成功提交到底层通信栈。
     * @retval <0 发送失败，具体含义由实现层定义。
     */
    int (*send)(const uint8_t *data, uint16_t len);

    /**
     * @brief 查询当前通信链路是否可发送业务数据。
     *
     * TCP 实现下，该接口表示是否已有上位机客户端连接到单片机。
     */
    int (*is_connected)(void);
} comm_bridge_t;

#endif
