/**
 ****************************************************************************************************
 * @file        device_service.h
 * @author      Codex
 * @date        2026-05-07
 * @brief       设备业务服务层接口。
 *
 * 本模块负责把“上位机命令、网络配置、ADC 周期上报”组织成应用逻辑。
 * 它只依赖桥接接口，不直接依赖 lwIP PCB 或 ADC DMA 缓冲区。
 ****************************************************************************************************
 */

#ifndef DEVICE_SERVICE_H
#define DEVICE_SERVICE_H


#include <stdint.h>

#include "comm_bridge.h"
#include "sensor_bridge.h"


/**
 * @brief 初始化业务服务层。
 *
 * @param aComm   通信桥接接口，由调用者保证其生命周期覆盖整个运行期。
 * @param aSensor 采集桥接接口，由调用者保证其生命周期覆盖整个运行期。
 */
void device_service_init(const comm_bridge_t *aComm, const sensor_bridge_t *aSensor);

/**
 * @brief 业务层周期调度入口。
 *
 * 主循环需要高频调用本函数。当前实现负责：
 * 1. 判断 TCP 是否已经连接。
 * 2. 按固定周期读取 ADC 采样结果。
 * 3. 格式化应用层文本帧。
 * 4. 通过通信桥发送给上位机。
 */
void device_service_poll(void);

/**
 * @brief 上位机数据接收入口。
 *
 * @param aData 指向 lwIP 解封装后的 TCP payload，不包含 Ethernet/IP/TCP 头。
 * @param aLen  payload 长度，单位字节。
 *
 * 本函数负责应用层命令解析。TCP 是字节流，所以这里会按 \n 聚合命令行。
 */
void device_service_on_rx(const uint8_t *aData, uint16_t aLen);


#endif /* DEVICE_SERVICE_H */
