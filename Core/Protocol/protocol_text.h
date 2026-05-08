/**
 ****************************************************************************************************
 * @file        protocol_text.h
 * @author      Codex
 * @date        2026-05-07
 * @brief       应用层文本协议封装接口。
 *
 * 本文件定义单片机发给上位机的 TCP payload 格式。TCP/IP/以太网头由 lwIP
 * 和以太网驱动自动封装，不在本层处理。
 ****************************************************************************************************
 */

#ifndef PROTOCOL_TEXT_H
#define PROTOCOL_TEXT_H


#include <stdint.h>

#include "sensor_bridge.h"


#define PROTOCOL_ADC_SAMPLE_RATE_HZ      1280000UL
#define PROTOCOL_ADC_BINARY_HEADER_SIZE  20U
#define PROTOCOL_ADC_BINARY_FRAME_SIZE   (PROTOCOL_ADC_BINARY_HEADER_SIZE + \
										  (SENSOR_ADC_FRAME_SAMPLE_NUM * 2U))


/**
 * @brief 将 ADC 采样结果格式化为上位机可解析的文本帧。
 *
 * 应用层帧格式：
 * CH16=<raw>,V=<x.xxx>V;CH15=<raw>,V=<x.xxx>V;CH18=<raw>,V=<x.xxx>V;CH19=<raw>,V=<x.xxx>V\r\n
 *
 * NOTE：本函数只生成 TCP payload，不包含 TCP/IP/以太网头。
 * NOTE：\r\n 是应用层帧结束符，上位机必须按该结束符处理粘包和拆包。
 *
 * @param aSample  输入采样结果。
 * @param aBuffer  输出缓冲区，由调用者分配和释放。
 * @param aSize    输出缓冲区长度。
 *
 * @return 实际写入长度，不包含字符串结束符；小于 0 表示参数错误。
 */
int protocol_text_format_adc(const adc_sample_t *aSample,
							 uint32_t aSeq,
							 char *aBuffer,
							 uint16_t aSize);


int protocol_text_format_adc_binary(const adc_sample_t *aSample,
									uint32_t aSeq,
									uint8_t *aBuffer,
									uint16_t aSize);


#endif /* PROTOCOL_TEXT_H */
