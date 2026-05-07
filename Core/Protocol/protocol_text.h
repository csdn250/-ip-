#ifndef PROTOCOL_TEXT_H
#define PROTOCOL_TEXT_H

#include <stdint.h>
#include "sensor_bridge.h"

/**
 * @brief 将 ADC 采样结果格式化为上位机可解析的文本帧。
 *
 * 应用层帧格式：
 *   CH16=<raw>,V=<x.xxx>V;CH15=<raw>,V=<x.xxx>V;CH18=<raw>,V=<x.xxx>V;CH19=<raw>,V=<x.xxx>V\r\n
 *
 * 说明：
 * - 本函数只生成 TCP payload，不包含 TCP/IP/以太网头。
 * - `\r\n` 是应用层帧结束符，上位机必须按该结束符拆包。
 * - TCP 是字节流，上位机不能假设一次 recv() 就等于一帧。
 *
 * @param sample   输入采样结果。
 * @param buf      输出缓冲区。
 * @param buf_size 输出缓冲区长度，当前实现中调用方应保证空间足够。
 *
 * @return 实际写入的字符串长度，不包含字符串结束符 `\0`；小于 0 表示参数错误。
 */
int protocol_text_format_adc(const adc_sample_t *sample, char *buf, uint16_t buf_size);

#endif
