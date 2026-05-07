#include "sensor_adc_dma.h"
#include "adc.h"
#include "stm32h743xx.h"

#define SENSOR_ADC_SAMPLE_NUM 50U
#define SENSOR_ADC_DMA_BUF_SIZE (SENSOR_ADC_CHANNEL_NUM * SENSOR_ADC_SAMPLE_NUM)
#define SENSOR_ADC_REF_MV 3300U
#define SENSOR_ADC_MAX_VALUE 65535U

/* ADC DMA 原始采样缓冲区。
 * DMA 按通道扫描顺序循环写入，业务层不会直接访问该缓冲区。
 */
static uint16_t s_adc_dma_buf[SENSOR_ADC_DMA_BUF_SIZE];

/* 由 BSP ADC DMA 中断置位：
 * 0 表示 DMA 采集尚未完成，1 表示缓冲区中已有一批完整采样。
 */
extern uint8_t g_adc_dma_sta;

static void sensor_adc_dma_init_impl(void)
{
    /* 将 ADC1 数据寄存器作为 DMA 源地址，将本模块缓冲区作为 DMA 目标地址。 */
    adc_nch_dma_init((uint32_t)&ADC1->DR, (uint32_t)s_adc_dma_buf);
    adc_dma_enable(SENSOR_ADC_DMA_BUF_SIZE);
}

static int sensor_adc_dma_read_impl(adc_sample_t *sample)
{
    uint16_t i;
    uint16_t j;
    uint32_t sum;

    if ((sample == 0) || (g_adc_dma_sta == 0))
    {
        return -1;
    }

    /* STM32H7 开启 D-Cache 后，DMA 写入内存的数据可能仍在缓存外。
     * 读取 DMA 缓冲区前需要失效 D-Cache，确保 CPU 读到的是 DMA 最新结果。
     */
    SCB_InvalidateDCache();

    /* DMA 缓冲区排列顺序：
     * CH16, CH15, CH18, CH19, CH16, CH15, CH18, CH19...
     *
     * 这里对每个通道取 SENSOR_ADC_SAMPLE_NUM 次采样求平均，
     * 降低瞬时噪声对上位机显示结果的影响。
     */
    for (j = 0; j < SENSOR_ADC_CHANNEL_NUM; j++)
    {
        sum = 0;

        for (i = 0; i < SENSOR_ADC_SAMPLE_NUM; i++)
        {
            sum += s_adc_dma_buf[(SENSOR_ADC_CHANNEL_NUM * i) + j];
        }

        sample->raw[j] = (uint16_t)(sum / SENSOR_ADC_SAMPLE_NUM);
        sample->mv[j] = ((uint32_t)sample->raw[j] * SENSOR_ADC_REF_MV) / SENSOR_ADC_MAX_VALUE;
    }

    /* 本批数据已经被业务层取走，清除完成标志并启动下一轮 DMA 采集。 */
    g_adc_dma_sta = 0;
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

