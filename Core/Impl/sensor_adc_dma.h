#ifndef SENSOR_ADC_DMA_H
#define SENSOR_ADC_DMA_H

#include "sensor_bridge.h"

/**
 * @brief 获取 ADC DMA 采集桥实例。
 *
 * 当前实现使用 ADC1 多通道 DMA：
 * - CH16 -> PA0
 * - CH15 -> PA3
 * - CH18 -> PA4
 * - CH19 -> PA5
 */
const sensor_bridge_t *sensor_adc_dma_get_bridge(void);

#endif
