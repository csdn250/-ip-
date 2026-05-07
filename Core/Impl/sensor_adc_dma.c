/**
 ****************************************************************************************************
 * @file        sensor_adc_dma.c
 * @author      Codex
 * @date        2026-05-07
 * @brief       ADC DMA 采集桥实现。
 ****************************************************************************************************
 */

#include "stm32h743xx.h"

#include "adc.h"
#include "sensor_adc_dma.h"


#define SENSOR_ADC_SAMPLE_NUM    50U
#define SENSOR_ADC_DMA_BUF_SIZE  (SENSOR_ADC_CHANNEL_NUM * SENSOR_ADC_SAMPLE_NUM)
#define SENSOR_ADC_REF_MV        3300U
#define SENSOR_ADC_MAX_VALUE     65535U


/* DMA 按通道扫描顺序循环写入该缓冲区，业务层不直接访问。 */
static uint16_t s_adc_dma_buf[SENSOR_ADC_DMA_BUF_SIZE];

/* 由 BSP ADC DMA 中断置位：0 表示未完成，非 0 表示已有一批完整采样。 */
extern uint8_t g_adc_dma_sta;


static void sensor_adc_dma_init_impl(void)
{
	/* 将 ADC1 数据寄存器作为 DMA 源地址，将本模块缓冲区作为 DMA 目标地址。 */
	adc_nch_dma_init((uint32_t)&ADC1->DR, (uint32_t)s_adc_dma_buf);
	adc_dma_enable(SENSOR_ADC_DMA_BUF_SIZE);
}


static int sensor_adc_dma_read_impl(adc_sample_t *aSample)
{
	uint16_t i;
	uint16_t j;
	uint32_t sum;

	if ((aSample == 0) || (g_adc_dma_sta == 0U))
	{
		return -1;
	}

	/* STM32H7 开启 D-Cache 后，读取 DMA 缓冲区前需要让缓存失效。 */
	SCB_InvalidateDCache();

	/* 缓冲区顺序：CH16、CH15、CH18、CH19 循环排列。 */
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

	/* 当前批次已经被业务层取走，清除标志并启动下一轮 DMA 采集。 */
	g_adc_dma_sta = 0U;
	adc_dma_enable(SENSOR_ADC_DMA_BUF_SIZE);

	return 0;
}


static const sensor_bridge_t s_sensor_adc_dma_bridge =
{
	sensor_adc_dma_init_impl,
	sensor_adc_dma_read_impl
};


const sensor_bridge_t *sensor_adc_dma_get_bridge(void)
{
	return &s_sensor_adc_dma_bridge;
}
