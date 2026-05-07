/**
 ****************************************************************************************************
 * @file        protocol_text.c
 * @author      Codex
 * @date        2026-05-07
 * @brief       应用层文本协议封装实现。
 ****************************************************************************************************
 */

#include <stdio.h>

#include "protocol_text.h"


/**
 * @brief 生成 ADC 文本协议帧。
 *
 * 该函数是“固件协议”和“上位机解析代码”的对齐点。上位机 socket recv()
 * 最终看到的应用层数据就是本函数生成的字符串。
 */
int protocol_text_format_adc(const adc_sample_t *aSample, char *aBuffer, uint16_t aSize)
{
	if ((aSample == 0) || (aBuffer == 0) || (aSize == 0U))
	{
		return -1;
	}

	/* NOTE：当前工程使用固定长度业务帧，调用方提供的 192 字节缓冲区足够容纳。 */
	return snprintf(aBuffer,
					aSize,
					"CH16=%u,V=%lu.%03luV;CH15=%u,V=%lu.%03luV;CH18=%u,V=%lu.%03luV;CH19=%u,V=%lu.%03luV\r\n",
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
}
