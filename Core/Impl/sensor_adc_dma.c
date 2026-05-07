/**
 ****************************************************************************************************
 * @file        sensor_adc_dma.c
 * @author      Codex
 * @date        2026-05-07
 * @brief       ADC DMA 采集桥实现。
 *
 * 本文件把 BSP ADC DMA 多通道采集封装成 `sensor_bridge_t`，供业务层周期读取。
 ****************************************************************************************************
 */

#include "stm32h743xx.h"

#include "adc.h"
#include "sensor_adc_dma.h"


#define SENSOR_ADC_SAMPLE_NUM    SENSOR_ADC_FRAME_SAMPLE_NUM
#define SENSOR_ADC_DMA_BUF_SIZE  (SENSOR_ADC_CHANNEL_NUM * SENSOR_ADC_SAMPLE_NUM)
#define SENSOR_ADC_REF_MV        3300U
#define SENSOR_ADC_MAX_VALUE     65535U


/* DMA 按通道扫描顺序循环写入该缓冲区，业务层不直接访问。 */
static uint16_t s_adc_dma_buf[SENSOR_ADC_DMA_BUF_SIZE];

/* 由 BSP ADC DMA 中断置位：0 表示未完成，非 0 表示已有一批完整采样。 */
extern uint8_t g_adc_dma_sta;


/**
 * @brief 初始化 ADC DMA 采集硬件。
 *
 * 本函数由 `device_service_init()` 间接调用。它配置 ADC1 数据寄存器作为 DMA
 * 源地址，并启动一轮多通道 DMA 采集。
 */
static void sensor_adc_dma_init_impl(void)
{
#if (SENSOR_ADC_CHANNEL_NUM == 1U)
	adc_dma_init((uint32_t)&ADC1->DR, (uint32_t)s_adc_dma_buf);
#else
	adc_nch_dma_init((uint32_t)&ADC1->DR, (uint32_t)s_adc_dma_buf);
#endif /* SENSOR_ADC_CHANNEL_NUM == 1U */
	adc_dma_enable(SENSOR_ADC_DMA_BUF_SIZE);
}


/**
 * @brief 读取一批 ADC DMA 采样结果。
 *
 * @param aSample 输出采样结果，由调用者提供存储空间。
 *
 * @retval 0  读取成功。
 * @retval -1 参数为空或 DMA 尚未完成一批采集。
 *
 * 采样缓冲区排列顺序为：
 * CH16、CH15、CH18、CH19、CH16、CH15、CH18、CH19 ...
 *
 * 本函数会对每个通道做一帧采样平均，并把单通道原始采样块交给业务层发送。
 */
static int sensor_adc_dma_read_impl(adc_sample_t *aSample)
{
	uint16_t i;
	uint16_t j;
	uint32_t sum;

	if ((aSample == 0) || (g_adc_dma_sta == 0U))
	{
		return -1;
	}

	/* STM32H7 开启 D-Cache 后，读取 DMA 缓冲区前必须让缓存失效。 */
	SCB_InvalidateDCache();

	for (j = 0U; j < SENSOR_ADC_CHANNEL_NUM; ++j)
	{
		sum = 0U;

		for (i = 0U; i < SENSOR_ADC_SAMPLE_NUM; ++i)
		{
			sum += s_adc_dma_buf[(SENSOR_ADC_CHANNEL_NUM * i) + j];
		}

		aSample->raw[j] = (uint16_t)(sum / SENSOR_ADC_SAMPLE_NUM);
		aSample->mv[j] = ((uint32_t)aSample->raw[j] * SENSOR_ADC_REF_MV) / SENSOR_ADC_MAX_VALUE;
	}

#if (SENSOR_ADC_CHANNEL_NUM == 1U)
	for (i = 0U; i < SENSOR_ADC_SAMPLE_NUM; ++i)
	{
		aSample->samples[i] = s_adc_dma_buf[i];
	}

	aSample->sample_count = SENSOR_ADC_SAMPLE_NUM;
#else
	aSample->sample_count = 0U;
#endif /* SENSOR_ADC_CHANNEL_NUM == 1U */

	/* 当前批次已经取走，清除完成标志并启动下一轮 DMA。 */
	g_adc_dma_sta = 0U;
	adc_dma_enable(SENSOR_ADC_DMA_BUF_SIZE);

	return 0;
}


static const sensor_bridge_t s_sensor_adc_dma_bridge =
{
	sensor_adc_dma_init_impl,
	sensor_adc_dma_read_impl
};


/**
 * @brief 获取 ADC DMA 采集桥实例。
 *
 * @return 指向静态采集桥实例的指针，整个运行期有效。
 */
const sensor_bridge_t *sensor_adc_dma_get_bridge(void)
{
	return &s_sensor_adc_dma_bridge;
}
