#ifndef DEVICE_SERVICE_H
#define DEVICE_SERVICE_H

#include <stdint.h>
#include "comm_bridge.h"
#include "sensor_bridge.h"

/**
 * @brief 初始化业务服务层。
 *
 * 本函数完成业务层和底层实现层之间的绑定：
 * - comm：通信桥，例如当前工程中的 lwIP TCP 实现。
 * - sensor：采集桥，例如当前工程中的 ADC DMA 实现。
 *
 * 初始化完成后，业务层只通过桥接接口访问底层资源，
 * 不直接调用 `tcp_write()` 或 ADC DMA 细节函数。
 */
void device_service_init(const comm_bridge_t *comm, const sensor_bridge_t *sensor);

/**
 * @brief 业务层周期调度入口。
 *
 * 主循环需要高频调用本函数。当前实现负责：
 * - 判断 TCP 是否已经连接。
 * - 按固定周期读取 ADC 采样结果。
 * - 格式化应用层文本帧。
 * - 通过通信桥发送给上位机。
 */
void device_service_poll(void);

/**
 * @brief 上位机数据接收入口。
 *
 * 调用边界：
 * - 本函数接收到的是 lwIP 解封装后的 TCP payload。
 * - data 中不包含 Ethernet/IP/TCP 头。
 * - 当前阶段为了验证链路，收到的数据会原样回吐给上位机。
 *
 * 后续扩展：
 * 可以在这里接入命令解析模块，例如 `GET_NET\r\n`、`SET_NET,...\r\n`。
 */
void device_service_on_rx(const uint8_t *data, uint16_t len);

#endif
