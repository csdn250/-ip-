/**
 ****************************************************************************************************
 * @file        sensor_adc_dma.h
 * @author      Codex
 * @date        2026-05-07
 * @brief       ADC DMA 采集桥实现接口。
 *
 * 当前实现使用 ADC1 多通道 DMA：
 * CH16 -> PA0，CH15 -> PA3，CH18 -> PA4，CH19 -> PA5。
 ****************************************************************************************************
 */

#ifndef SENSOR_ADC_DMA_H
#define SENSOR_ADC_DMA_H


#include "sensor_bridge.h"


/**
 * @brief 获取 ADC DMA 采集桥实例。
 *
 * @return 指向静态采集桥实例的指针，整个运行期有效。
 */
const sensor_bridge_t *sensor_adc_dma_get_bridge(void);


#endif /* SENSOR_ADC_DMA_H */
