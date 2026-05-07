#include "protocol_text.h"
#include <stdio.h>

/**
 * @brief 生成 ADC 文本协议帧。
 *
 * 该函数是“固件协议”和“上位机解析代码”的对齐点。
 * 如果以后需要调整字段名、增加时间戳、增加设备 ID 或改成 JSON/二进制协议，
 * 应优先从这里修改，并同步更新 `PROTOCOL_FOR_PC.md`。
 */
int protocol_text_format_adc(const adc_sample_t *sample, char *buf, uint16_t buf_size)
{
    if ((sample == 0) || (buf == 0) || (buf_size == 0))
    {
        return -1;
    }

    (void)buf_size;

    /* 该字符串就是上位机 socket recv() 看到的应用层数据。
     * 注意：
     * - 这里没有 TCP 头、IP 头、以太网头。
     * - `;` 用于分隔通道。
     * - `,` 用于分隔同一通道内的原始值和电压值。
     * - `\r\n` 用于标记一帧结束，上位机必须以此做粘包/拆包。
     */
    return sprintf(buf,
                   "CH16=%u,V=%lu.%03luV;CH15=%u,V=%lu.%03luV;CH18=%u,V=%lu.%03luV;CH19=%u,V=%lu.%03luV\r\n",
                   sample->raw[0], (unsigned long)(sample->mv[0] / 1000), (unsigned long)(sample->mv[0] % 1000),
                   sample->raw[1], (unsigned long)(sample->mv[1] / 1000), (unsigned long)(sample->mv[1] % 1000),
                   sample->raw[2], (unsigned long)(sample->mv[2] / 1000), (unsigned long)(sample->mv[2] % 1000),
                   sample->raw[3], (unsigned long)(sample->mv[3] / 1000), (unsigned long)(sample->mv[3] % 1000));
}
