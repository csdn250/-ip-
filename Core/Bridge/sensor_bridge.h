/**
 ****************************************************************************************************
 * @file        sensor_bridge.h
 * @author      Codex
 * @date        2026-05-07
 * @brief       传感器采集桥接接口定义。
 *
 * 业务层通过本接口获取采样结果，不直接依赖 ADC DMA 的寄存器和 BSP 细节。
 ****************************************************************************************************
 */

#ifndef SENSOR_BRIDGE_H
#define SENSOR_BRIDGE_H


#include <stdint.h>


#define SENSOR_ADC_CHANNEL_NUM 1U
#define SENSOR_ADC_FRAME_SAMPLE_NUM 1000U


/**
 * @brief 一次业务层可使用的 ADC 采样结果。
 *
 * raw[] 保存 ADC 原始平均值，mv[] 保存换算后的毫伏值。
 * 数组下标与协议输出通道一一对应：
 * raw[0]/mv[0] -> CH16，PA0。
 * raw[1]/mv[1] -> CH15，PA3。
 * raw[2]/mv[2] -> CH18，PA4。
 * raw[3]/mv[3] -> CH19，PA5。
 */
typedef struct
{
	uint16_t raw[SENSOR_ADC_CHANNEL_NUM];
	uint32_t mv[SENSOR_ADC_CHANNEL_NUM];
	uint16_t samples[SENSOR_ADC_FRAME_SAMPLE_NUM];
	uint16_t sample_count;
} adc_sample_t;


/**
 * @brief 传感器采集桥接接口。
 *
 * 业务层只关心“能初始化采集”和“能读出采样结果”，不关心底层是
 * ADC DMA、普通轮询 ADC，还是后续扩展的外部传感器。
 */
typedef struct
{
	/**
	 * @brief 初始化采集硬件和底层缓冲区。
	 */
	void (*init)(void);

	/**
	 * @brief 读取一组已经准备好的采样结果。
	 *
	 * @param aSample 输出采样结果，由调用者提供存储空间。
	 *
	 * @retval 0 读取成功。
	 * @retval <0 当前没有新数据或参数无效。
	 */
	int (*read)(adc_sample_t *aSample);
} sensor_bridge_t;


#endif /* SENSOR_BRIDGE_H */
