/**
 ****************************************************************************************************
 * @file        protocol_text.c
 * @author      Codex
 * @date        2026-05-07
 * @brief       应用层文本协议封装实现。
 *
 * 本文件只负责生成上位机可解析的 TCP payload，不参与 TCP/IP/以太网头封装。
 ****************************************************************************************************
 */

#include <stdio.h>

#include "protocol_text.h"


static void protocol_text_write_u16_le(uint8_t *aBuffer, uint16_t aValue)
{
	aBuffer[0] = (uint8_t)(aValue & 0xFFU);
	aBuffer[1] = (uint8_t)((aValue >> 8U) & 0xFFU);
}


static void protocol_text_write_u32_le(uint8_t *aBuffer, uint32_t aValue)
{
	aBuffer[0] = (uint8_t)(aValue & 0xFFUL);
	aBuffer[1] = (uint8_t)((aValue >> 8UL) & 0xFFUL);
	aBuffer[2] = (uint8_t)((aValue >> 16UL) & 0xFFUL);
	aBuffer[3] = (uint8_t)((aValue >> 24UL) & 0xFFUL);
}


/**
 * @brief 生成 ADC 文本协议帧。
 *
 * @param aSample 输入 ADC 采样结果。
 * @param aBuffer 输出字符串缓冲区，由调用者提供。
 * @param aSize   输出缓冲区长度，单位字节。
 *
 * @return 实际写入字符数，不包含字符串结束符 `\0`；返回负数表示参数非法。
 *
 * 输出格式：
 * `CH16=<raw>,V=<x.xxx>V;CH15=<raw>,V=<x.xxx>V;CH18=<raw>,V=<x.xxx>V;CH19=<raw>,V=<x.xxx>V\r\n`
 *
 * 上位机必须以 `\r\n` 作为一帧结束符处理粘包和拆包。
 */
int protocol_text_format_adc(const adc_sample_t *aSample,
							 uint32_t aSeq,
							 char *aBuffer,
							 uint16_t aSize)
{
	if ((aSample == 0) || (aBuffer == 0) || (aSize == 0U))
	{
		return -1;
	}

#if (SENSOR_ADC_CHANNEL_NUM == 1U)
	return snprintf(aBuffer,
					aSize,
					"SEQ=%lu;CH19=%u,V=%lu.%03luV\r\n",
					(unsigned long)aSeq,
					aSample->raw[0],
					(unsigned long)(aSample->mv[0] / 1000U),
					(unsigned long)(aSample->mv[0] % 1000U));
#else
	return snprintf(aBuffer,
					aSize,
					"SEQ=%lu;CH16=%u,V=%lu.%03luV;CH15=%u,V=%lu.%03luV;CH18=%u,V=%lu.%03luV;CH19=%u,V=%lu.%03luV\r\n",
					(unsigned long)aSeq,
					aSample->raw[0],
					(unsigned long)(aSample->mv[0] / 1000U),
					(unsigned long)(aSample->mv[0] % 1000U),
					aSample->raw[1],
					(unsigned long)(aSample->mv[1] / 1000U),
					(unsigned long)(aSample->mv[1] % 1000U),
					aSample->raw[2],
					(unsigned long)(aSample->mv[2] / 1000U),
					(unsigned long)(aSample->mv[2] % 1000U),
					aSample->raw[3],
					(unsigned long)(aSample->mv[3] / 1000U),
					(unsigned long)(aSample->mv[3] % 1000U));
#endif /* SENSOR_ADC_CHANNEL_NUM == 1U */
}


int protocol_text_format_adc_binary(const adc_sample_t *aSample,
									uint32_t aSeq,
									uint8_t *aBuffer,
									uint16_t aSize)
{
	uint32_t checksum;
	uint16_t i;
	uint16_t offset;

	if ((aSample == 0) || (aBuffer == 0) ||
		(aSample->sample_count != SENSOR_ADC_FRAME_SAMPLE_NUM) ||
		(aSize < PROTOCOL_ADC_BINARY_FRAME_SIZE))
	{
		return -1;
	}

	aBuffer[0] = 'A';
	aBuffer[1] = 'D';
	aBuffer[2] = 'C';
	aBuffer[3] = '1';
	protocol_text_write_u32_le(&aBuffer[4], aSeq);
	protocol_text_write_u16_le(&aBuffer[8], aSample->sample_count);
	protocol_text_write_u16_le(&aBuffer[10], 19U);
	protocol_text_write_u32_le(&aBuffer[12], PROTOCOL_ADC_SAMPLE_RATE_HZ);

	checksum = 0UL;
	offset = PROTOCOL_ADC_BINARY_HEADER_SIZE;

	for (i = 0U; i < aSample->sample_count; ++i)
	{
		checksum += aSample->samples[i];
		protocol_text_write_u16_le(&aBuffer[offset], aSample->samples[i]);
		offset += 2U;
	}

	protocol_text_write_u32_le(&aBuffer[16], checksum);
	return PROTOCOL_ADC_BINARY_FRAME_SIZE;
}
