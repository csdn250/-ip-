/**
 ****************************************************************************************************
 * @file        device_service.c
 * @author      Codex
 * @date        2026-05-07
 * @brief       设备业务服务层实现。
 ****************************************************************************************************
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stm32h7xx_hal.h"

#include "device_service.h"
#include "lwip_comm.h"
#include "net_config.h"
#include "protocol_text.h"


/* ADC 主动上报周期。该周期属于业务策略，不属于 ADC 驱动或 TCP 驱动。 */
#define DEVICE_SERVICE_ADC_SEND_INTERVAL  2000U
#define DEVICE_SERVICE_RX_LINE_SIZE       192U


static const comm_bridge_t *s_comm;
static const sensor_bridge_t *s_sensor;
static uint32_t s_adc_send_tick;
static char s_rx_line[DEVICE_SERVICE_RX_LINE_SIZE];
static uint16_t s_rx_line_len;


static void device_service_send_text(const char *aText)
{
	if ((s_comm != 0) && (s_comm->send != 0) && (aText != 0))
	{
		s_comm->send((const uint8_t *)aText, (uint16_t)strlen(aText));
	}
}


static void device_service_reboot_after_reply(void)
{
	/* 给 tcp_write()/tcp_output() 留出发送时间，避免上位机还没收到 OK 就复位。 */
	HAL_Delay(300U);
	NVIC_SystemReset();
}


static int device_service_parse_ipv4(const char *aText, uint8_t aIp[4])
{
	char *pEnd;
	unsigned long value;
	uint8_t i;

	if ((aText == 0) || (aIp == 0))
	{
		return -1;
	}

	for (i = 0U; i < 4U; ++i)
	{
		value = strtoul(aText, &pEnd, 10);

		if ((pEnd == aText) || (value > 255UL))
		{
			return -1;
		}

		aIp[i] = (uint8_t)value;

		if (i < 3U)
		{
			if (*pEnd != '.')
			{
				return -1;
			}

			aText = pEnd + 1;
		} else {
			if ((*pEnd != '\0') && (*pEnd != ',') && (*pEnd != '\r') && (*pEnd != '\n'))
			{
				return -1;
			}
		}
	}

	return 0;
}


static int device_service_parse_field_ip(const char *aLine, const char *aKey, uint8_t aIp[4])
{
	const char *pField = strstr(aLine, aKey);

	if (pField == 0)
	{
		return -1;
	}

	pField += strlen(aKey);
	return device_service_parse_ipv4(pField, aIp);
}


static void device_service_format_current_net(char *aBuffer, uint16_t aSize)
{
	snprintf(aBuffer,
			 aSize,
			 "NET,IP=%d.%d.%d.%d,MASK=%d.%d.%d.%d,GW=%d.%d.%d.%d\r\n",
			 g_lwipdev.ip[0],
			 g_lwipdev.ip[1],
			 g_lwipdev.ip[2],
			 g_lwipdev.ip[3],
			 g_lwipdev.netmask[0],
			 g_lwipdev.netmask[1],
			 g_lwipdev.netmask[2],
			 g_lwipdev.netmask[3],
			 g_lwipdev.gateway[0],
			 g_lwipdev.gateway[1],
			 g_lwipdev.gateway[2],
			 g_lwipdev.gateway[3]);
}


static void device_service_apply_config_to_ram(const net_config_t *aConfig)
{
	if (aConfig == 0)
	{
		return;
	}

	memcpy(g_lwipdev.ip, aConfig->ip, sizeof(g_lwipdev.ip));
	memcpy(g_lwipdev.netmask, aConfig->netmask, sizeof(g_lwipdev.netmask));
	memcpy(g_lwipdev.gateway, aConfig->gateway, sizeof(g_lwipdev.gateway));
}


static void device_service_handle_get_net(void)
{
	char reply[128];

	device_service_format_current_net(reply, sizeof(reply));
	device_service_send_text(reply);
}


static void device_service_handle_set_net(const char *aLine)
{
	net_config_t config;

	if ((device_service_parse_field_ip(aLine, "IP=", config.ip) != 0) ||
		(device_service_parse_field_ip(aLine, "MASK=", config.netmask) != 0) ||
		(device_service_parse_field_ip(aLine, "GW=", config.gateway) != 0))
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
	device_service_send_text("OK,NET_SAVED,REBOOTING\r\n");
	device_service_reboot_after_reply();
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
	device_service_send_text("OK,NET_RESET,REBOOTING\r\n");
	device_service_reboot_after_reply();
}


static void device_service_handle_line(char *aLine)
{
	char *pEnd = aLine + strlen(aLine);

	while ((pEnd > aLine) && ((pEnd[-1] == '\r') || (pEnd[-1] == '\n') || (pEnd[-1] == ' ')))
	{
		--pEnd;
		*pEnd = '\0';
	}

	if (aLine[0] == '\0')
	{
		return;
	}

	if ((strcmp(aLine, "GET_NET") == 0) || (strcmp(aLine, "GET_NET?") == 0))
	{
		device_service_handle_get_net();
	} else if (strncmp(aLine, "SET_NET,", 8U) == 0) {
		device_service_handle_set_net(aLine);
	} else if ((strcmp(aLine, "RESET_NET") == 0) || (strcmp(aLine, "FACTORY_NET") == 0)) {
		device_service_handle_reset_net();
	} else if (strcmp(aLine, "SAVE_NET") == 0) {
		device_service_send_text("OK,SET_NET_AUTO_SAVES\r\n");
	} else if (strcmp(aLine, "REBOOT") == 0) {
		device_service_send_text("OK,REBOOTING\r\n");
		device_service_reboot_after_reply();
	} else if (strcmp(aLine, "PING") == 0) {
		device_service_send_text("PONG\r\n");
	} else if (strncmp(aLine, "ECHO,", 5U) == 0) {
		device_service_send_text(aLine + 5);
		device_service_send_text("\r\n");
	} else {
		device_service_send_text("ERR,UNKNOWN_CMD\r\n");
	}
}


void device_service_init(const comm_bridge_t *aComm, const sensor_bridge_t *aSensor)
{
	/* 只保存接口，不保存具体实现细节，便于后续替换通信或采集实现。 */
	s_comm = aComm;
	s_sensor = aSensor;
	s_adc_send_tick = HAL_GetTick();
	s_rx_line_len = 0U;

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

	/* 防御式检查：底层桥接未绑定时直接返回，避免访问空指针。 */
	if ((s_comm == 0) || (s_sensor == 0) ||
		(s_comm->send == 0) || (s_comm->is_connected == 0) ||
		(s_sensor->read == 0))
	{
		return;
	}

	if (!s_comm->is_connected())
	{
		return;
	}

	if ((HAL_GetTick() - s_adc_send_tick) < DEVICE_SERVICE_ADC_SEND_INTERVAL)
	{
		return;
	}

	s_adc_send_tick = HAL_GetTick();

	/* 仅当 DMA 已经完成一批采集时才会读取成功。 */
	if (s_sensor->read(&sample) != 0)
	{
		return;
	}

	len = protocol_text_format_adc(&sample, frame, sizeof(frame));

	if ((len > 0) && (len < (int)sizeof(frame)))
	{
		s_comm->send((const uint8_t *)frame, (uint16_t)len);
	}
}


void device_service_on_rx(const uint8_t *aData, uint16_t aLen)
{
	uint16_t i;
	uint8_t has_line_delimiter = 0U;
	uint8_t maybe_command = 0U;

	/* aData 是 TCP payload，以太网头、IP 头、TCP 头已经由 lwIP 处理完成。 */
	if ((s_comm == 0) || (s_comm->send == 0) || (aData == 0) || (aLen == 0U))
	{
		return;
	}

	for (i = 0U; i < aLen; ++i)
	{
		if (aData[i] == '\n')
		{
			has_line_delimiter = 1U;
			break;
		}
	}

	if ((aLen >= 3U) &&
		((memcmp(aData, "GET", 3U) == 0) ||
		 (memcmp(aData, "SET", 3U) == 0) ||
		 (memcmp(aData, "RES", 3U) == 0) ||
		 (memcmp(aData, "FAC", 3U) == 0) ||
		 (memcmp(aData, "SAV", 3U) == 0) ||
		 (memcmp(aData, "REB", 3U) == 0) ||
		 (memcmp(aData, "PIN", 3U) == 0) ||
		 (memcmp(aData, "ECH", 3U) == 0)))
	{
		maybe_command = 1U;
	}

	/* 兼容网络调试助手直接发送 test 这类无换行文本的回显测试。 */
	if ((has_line_delimiter == 0U) && (maybe_command == 0U) && (s_rx_line_len == 0U))
	{
		s_comm->send(aData, aLen);
		return;
	}

	/* TCP 是字节流，这里按 \n 聚合成应用层命令行。 */
	for (i = 0U; i < aLen; ++i)
	{
		if (s_rx_line_len >= (DEVICE_SERVICE_RX_LINE_SIZE - 1U))
		{
			s_rx_line_len = 0U;
			device_service_send_text("ERR,CMD_TOO_LONG\r\n");
			continue;
		}

		s_rx_line[s_rx_line_len] = (char)aData[i];
		++s_rx_line_len;

		if (aData[i] == '\n')
		{
			s_rx_line[s_rx_line_len] = '\0';
			device_service_handle_line(s_rx_line);
			s_rx_line_len = 0U;
		}
	}
}
