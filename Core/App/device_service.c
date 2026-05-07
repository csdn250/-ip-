#include "device_service.h"
#include "lwip_comm.h"
#include "net_config.h"
#include "protocol_text.h"
#include "stm32h7xx_hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ADC 主动上传周期。该周期属于业务策略，不属于 ADC 驱动或 TCP 驱动。 */
#define DEVICE_SERVICE_ADC_SEND_INTERVAL 500U
#define DEVICE_SERVICE_RX_LINE_SIZE      192U

/* 业务层持有的桥接接口。使用指针保存，便于后续替换通信或采集实现。 */
static const comm_bridge_t *s_comm;
static const sensor_bridge_t *s_sensor;
static uint32_t s_adc_send_tick;
static char s_rx_line[DEVICE_SERVICE_RX_LINE_SIZE];
static uint16_t s_rx_line_len;

static void device_service_send_text(const char *text)
{
    if ((s_comm != 0) && (s_comm->send != 0) && (text != 0))
    {
        s_comm->send((const uint8_t *)text, (uint16_t)strlen(text));
    }
}

static int device_service_parse_ipv4(const char *text, uint8_t ip[4])
{
    char *end;
    unsigned long value;
    uint8_t i;

    if ((text == 0) || (ip == 0))
    {
        return -1;
    }

    for (i = 0; i < 4U; i++)
    {
        value = strtoul(text, &end, 10);
        if ((end == text) || (value > 255UL))
        {
            return -1;
        }

        ip[i] = (uint8_t)value;

        if (i < 3U)
        {
            if (*end != '.')
            {
                return -1;
            }
            text = end + 1;
        }
        else
        {
            if ((*end != '\0') && (*end != ',') && (*end != '\r') && (*end != '\n'))
            {
                return -1;
            }
        }
    }

    return 0;
}

static int device_service_parse_field_ip(const char *line, const char *key, uint8_t ip[4])
{
    const char *field = strstr(line, key);

    if (field == 0)
    {
        return -1;
    }

    field += strlen(key);
    return device_service_parse_ipv4(field, ip);
}

static void device_service_format_current_net(char *out, uint16_t out_size)
{
    snprintf(out,
             out_size,
             "NET,IP=%d.%d.%d.%d,MASK=%d.%d.%d.%d,GW=%d.%d.%d.%d\r\n",
             g_lwipdev.ip[0], g_lwipdev.ip[1], g_lwipdev.ip[2], g_lwipdev.ip[3],
             g_lwipdev.netmask[0], g_lwipdev.netmask[1], g_lwipdev.netmask[2], g_lwipdev.netmask[3],
             g_lwipdev.gateway[0], g_lwipdev.gateway[1], g_lwipdev.gateway[2], g_lwipdev.gateway[3]);
}

static void device_service_apply_config_to_ram(const net_config_t *config)
{
    if (config == 0)
    {
        return;
    }

    memcpy(g_lwipdev.ip, config->ip, sizeof(g_lwipdev.ip));
    memcpy(g_lwipdev.netmask, config->netmask, sizeof(g_lwipdev.netmask));
    memcpy(g_lwipdev.gateway, config->gateway, sizeof(g_lwipdev.gateway));
}

static void device_service_handle_get_net(void)
{
    char reply[128];

    device_service_format_current_net(reply, sizeof(reply));
    device_service_send_text(reply);
}

static void device_service_handle_set_net(const char *line)
{
    net_config_t config;

    if ((device_service_parse_field_ip(line, "IP=", config.ip) != 0) ||
        (device_service_parse_field_ip(line, "MASK=", config.netmask) != 0) ||
        (device_service_parse_field_ip(line, "GW=", config.gateway) != 0))
    {
        device_service_send_text("ERR,BAD_NET_FORMAT\r\n");
        return;
    }

    if (!net_config_validate(&config))
    {
        device_service_send_text("ERR,BAD_NET_VALUE\r\n");
        return;
    }

    if (net_config_save(&config) != 0)
    {
        device_service_send_text("ERR,FLASH_WRITE_FAILED\r\n");
        return;
    }

    device_service_apply_config_to_ram(&config);
    device_service_send_text("OK,NET_SAVED,REBOOT_REQUIRED\r\n");
}

static void device_service_handle_reset_net(void)
{
    net_config_t config;

    if (net_config_clear() != 0)
    {
        device_service_send_text("ERR,FLASH_ERASE_FAILED\r\n");
        return;
    }

    net_config_get_default(&config);
    device_service_apply_config_to_ram(&config);
    device_service_send_text("OK,NET_RESET,REBOOT_REQUIRED\r\n");
}

static void device_service_handle_line(char *line)
{
    char *end = line + strlen(line);

    while ((end > line) && ((end[-1] == '\r') || (end[-1] == '\n') || (end[-1] == ' ')))
    {
        end--;
        *end = '\0';
    }

    if ((strcmp(line, "GET_NET") == 0) || (strcmp(line, "GET_NET?") == 0))
    {
        device_service_handle_get_net();
    }
    else if (strncmp(line, "SET_NET,", 8) == 0)
    {
        device_service_handle_set_net(line);
    }
    else if ((strcmp(line, "RESET_NET") == 0) || (strcmp(line, "FACTORY_NET") == 0))
    {
        device_service_handle_reset_net();
    }
    else if (strcmp(line, "SAVE_NET") == 0)
    {
        device_service_send_text("OK,SET_NET_ALREADY_SAVES\r\n");
    }
    else if (strcmp(line, "REBOOT") == 0)
    {
        device_service_send_text("OK,REBOOTING\r\n");
        HAL_Delay(100);
        NVIC_SystemReset();
    }
    else if (strcmp(line, "PING") == 0)
    {
        device_service_send_text("PONG\r\n");
    }
    else
    {
        device_service_send_text("ERR,UNKNOWN_CMD\r\n");
    }
}

void device_service_init(const comm_bridge_t *comm, const sensor_bridge_t *sensor)
{
    /* 只保存接口，不保存具体实现细节。
     * 这样业务层不需要知道底层是 lwIP RAW TCP 还是其他通信方式。
     */
    s_comm = comm;
    s_sensor = sensor;
    s_adc_send_tick = HAL_GetTick();

    if ((s_sensor != 0) && (s_sensor->init != 0))
    {
        s_sensor->init();
    }
}

void device_service_poll(void)
{
    adc_sample_t sample;
    char frame[192];
    int len;

    /* 防御式检查：
     * 业务层允许桥接实现为空，避免初始化顺序异常时直接访问空指针。
     */
    if ((s_comm == 0) || (s_sensor == 0) ||
        (s_comm->send == 0) || (s_comm->is_connected == 0) ||
        (s_sensor->read == 0))
    {
        return;
    }

    if (!s_comm->is_connected())
    {
        /* 没有上位机连接时不主动读取并发送 ADC，避免无意义占用发送缓冲。 */
        return;
    }

    if ((HAL_GetTick() - s_adc_send_tick) < DEVICE_SERVICE_ADC_SEND_INTERVAL)
    {
        return;
    }

    s_adc_send_tick = HAL_GetTick();

    /* sensor->read() 只在 DMA 已经完成一轮采集时返回成功。
     * 如果当前还没有新数据，本轮业务调度直接退出。
     */
    if (s_sensor->read(&sample) != 0)
    {
        return;
    }

    /* 业务层不直接调用 lwIP。
     * 这里先生成上位机协议帧，再通过 comm_bridge_t 发送。
     */
    len = protocol_text_format_adc(&sample, frame, sizeof(frame));
    if ((len > 0) && (len < (int)sizeof(frame)))
    {
        s_comm->send((const uint8_t *)frame, (uint16_t)len);
    }
}

void device_service_on_rx(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    /* data 是 TCP payload，不是原始以太网帧。
     * 以太网头、IP 头、TCP 头已经由 lwIP 和协议栈处理完成。
     */
    if ((s_comm == 0) || (s_comm->send == 0) || (data == 0) || (len == 0))
    {
        return;
    }

    /* TCP 是字节流，不保证上位机一次 send 对应本函数一次完整命令。
     * 这里按 \n 聚合成应用层命令行，再交给命令处理函数。
     */
    for (i = 0; i < len; i++)
    {
        if (s_rx_line_len >= (DEVICE_SERVICE_RX_LINE_SIZE - 1U))
        {
            s_rx_line_len = 0;
            device_service_send_text("ERR,CMD_TOO_LONG\r\n");
            continue;
        }

        s_rx_line[s_rx_line_len++] = (char)data[i];

        if (data[i] == '\n')
        {
            s_rx_line[s_rx_line_len] = '\0';
            device_service_handle_line(s_rx_line);
            s_rx_line_len = 0;
        }
    }
}
