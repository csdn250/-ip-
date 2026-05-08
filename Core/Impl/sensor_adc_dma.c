/**
 ****************************************************************************************************
 * @file        sensor_adc_dma.c
 * @author      Codex
 * @date        2026-05-07
 * @brief       ADC DMA continuous acquisition bridge.
 *
 * This module converts the BSP ADC DMA half/full transfer events into a small
 * frame queue. ADC sampling keeps running in circular DMA mode while TCP sends
 * queued frames at its own pace.
 ****************************************************************************************************
 */

#include <stdint.h>
#include <string.h>

#include "stm32h743xx.h"

#include "adc.h"
#include "sensor_adc_dma.h"


#define SENSOR_ADC_SAMPLE_NUM       SENSOR_ADC_FRAME_SAMPLE_NUM
#define SENSOR_ADC_DMA_BLOCK_NUM    2U
#define SENSOR_ADC_DMA_BLOCK_SIZE   (SENSOR_ADC_CHANNEL_NUM * SENSOR_ADC_SAMPLE_NUM)
#define SENSOR_ADC_DMA_BUF_SIZE     (SENSOR_ADC_DMA_BLOCK_NUM * SENSOR_ADC_DMA_BLOCK_SIZE)
#define SENSOR_ADC_QUEUE_DEPTH      6U
#define SENSOR_ADC_REF_MV           3300U
#define SENSOR_ADC_MAX_VALUE        65535U
#define SENSOR_ADC_CACHE_LINE_SIZE  32U


typedef struct
{
	uint16_t samples[SENSOR_ADC_SAMPLE_NUM];
	uint32_t capture_seq;
} sensor_adc_frame_slot_t;


static __ALIGNED(32) uint16_t s_adc_dma_buf[SENSOR_ADC_DMA_BUF_SIZE];
static sensor_adc_frame_slot_t s_adc_queue[SENSOR_ADC_QUEUE_DEPTH];
static volatile uint8_t s_adc_queue_read_index;
static volatile uint8_t s_adc_queue_write_index;
static volatile uint8_t s_adc_queue_count;
static volatile uint32_t s_adc_queue_drop_count;


static void sensor_adc_dma_invalidate_block(uint8_t aBlockIndex)
{
	uintptr_t addr;
	uintptr_t start;
	uintptr_t end;
	uint16_t block_offset;

	block_offset = (uint16_t)(aBlockIndex * SENSOR_ADC_DMA_BLOCK_SIZE);
	addr = (uintptr_t)&s_adc_dma_buf[block_offset];
	start = addr & ~((uintptr_t)SENSOR_ADC_CACHE_LINE_SIZE - 1U);
	end = (addr + sizeof(s_adc_queue[0].samples) + SENSOR_ADC_CACHE_LINE_SIZE - 1U) &
		  ~((uintptr_t)SENSOR_ADC_CACHE_LINE_SIZE - 1U);

	SCB_InvalidateDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
}


static void sensor_adc_dma_push_block(uint8_t aBlockIndex, uint32_t aCaptureSeq)
{
	uint16_t block_offset;

	block_offset = (uint16_t)(aBlockIndex * SENSOR_ADC_DMA_BLOCK_SIZE);

	sensor_adc_dma_invalidate_block(aBlockIndex);

	if (s_adc_queue_count >= SENSOR_ADC_QUEUE_DEPTH)
	{
		s_adc_queue_read_index = (uint8_t)((s_adc_queue_read_index + 1U) % SENSOR_ADC_QUEUE_DEPTH);
		--s_adc_queue_count;
		++s_adc_queue_drop_count;
	}

	memcpy(s_adc_queue[s_adc_queue_write_index].samples,
		   &s_adc_dma_buf[block_offset],
		   sizeof(s_adc_queue[s_adc_queue_write_index].samples));

	s_adc_queue[s_adc_queue_write_index].capture_seq = aCaptureSeq;
	s_adc_queue_write_index = (uint8_t)((s_adc_queue_write_index + 1U) % SENSOR_ADC_QUEUE_DEPTH);
	++s_adc_queue_count;
}


void adc_dma_half_transfer_callback(uint32_t seq)
{
	sensor_adc_dma_push_block(0U, seq);
	g_adc_dma_sta &= (uint8_t)(~ADC_DMA_STA_HALF_READY);
}


void adc_dma_full_transfer_callback(uint32_t seq)
{
	sensor_adc_dma_push_block(1U, seq);
	g_adc_dma_sta &= (uint8_t)(~ADC_DMA_STA_FULL_READY);
}


static void sensor_adc_dma_init_impl(void)
{
	s_adc_queue_read_index = 0U;
	s_adc_queue_write_index = 0U;
	s_adc_queue_count = 0U;
	s_adc_queue_drop_count = 0UL;

	g_adc_dma_sta = 0U;
	g_adc_dma_block_seq = 0UL;
	g_adc_dma_half_seq = 0UL;
	g_adc_dma_full_seq = 0UL;
	g_adc_dma_drop_count = 0UL;

#if (SENSOR_ADC_CHANNEL_NUM == 1U)
	adc_dma_init((uint32_t)&ADC1->DR, (uint32_t)s_adc_dma_buf);
#else
	adc_nch_dma_init((uint32_t)&ADC1->DR, (uint32_t)s_adc_dma_buf);
#endif /* SENSOR_ADC_CHANNEL_NUM == 1U */
	adc_dma_enable(SENSOR_ADC_DMA_BUF_SIZE);
}


static int sensor_adc_dma_read_impl(adc_sample_t *aSample)
{
	uint16_t i;
	uint32_t sum;
	uint32_t capture_seq;

	if (aSample == 0)
	{
		return -1;
	}

	HAL_NVIC_DisableIRQ(ADC_ADCX_DMASx_IRQn);

	if (s_adc_queue_count == 0U)
	{
		HAL_NVIC_EnableIRQ(ADC_ADCX_DMASx_IRQn);
		return -1;
	}

	memcpy(aSample->samples,
		   s_adc_queue[s_adc_queue_read_index].samples,
		   sizeof(aSample->samples));
	capture_seq = s_adc_queue[s_adc_queue_read_index].capture_seq;
	s_adc_queue_read_index = (uint8_t)((s_adc_queue_read_index + 1U) % SENSOR_ADC_QUEUE_DEPTH);
	--s_adc_queue_count;

	HAL_NVIC_EnableIRQ(ADC_ADCX_DMASx_IRQn);

	sum = 0UL;

	for (i = 0U; i < SENSOR_ADC_SAMPLE_NUM; ++i)
	{
		sum += aSample->samples[i];
	}

	aSample->raw[0] = (uint16_t)(sum / SENSOR_ADC_SAMPLE_NUM);
	aSample->mv[0] = ((uint32_t)aSample->raw[0] * SENSOR_ADC_REF_MV) / SENSOR_ADC_MAX_VALUE;
	aSample->sample_count = SENSOR_ADC_SAMPLE_NUM;
	aSample->capture_seq = capture_seq;
	aSample->drop_count = s_adc_queue_drop_count + g_adc_dma_drop_count;

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
