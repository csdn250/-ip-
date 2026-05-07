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
#define DEVICE_SERVICE_ADC_SEND_INTERVAL  0U
#define DEVICE_SERVICE_RX_LINE_SIZE       192U


static const comm_bridge_t *s_comm;
static const sensor_bridge_t *s_sensor;
static uint32_t s_adc_send_tick;
static uint32_t s_adc_frame_seq;
static adc_sample_t s_adc_sample;
static uint8_t s_adc_frame[PROTOCOL_ADC_BINARY_FRAME_SIZE];
static char s_rx_line[DEVICE_SERVICE_RX_LINE_SIZE];
static uint16_t s_rx_line_len;


/**
 * @brief 通过当前通信桥发送一段以 `\0` 结尾的文本。
 *
 * @param aText 待发送字符串，由调用者维护生命周期，本函数不保存该指针。
 *
 * 本函数是业务层内部的统一文本发送出口。调用前会检查通信桥、发送函数和
 * 字符串指针是否有效，避免初始化顺序异常时访问空指针。
 */
static void device_service_send_text(const char *aText)
{
	if ((s_comm != 0) && (s_comm->send != 0) && (aText != 0))
	{
		s_comm->send((const uint8_t *)aText, (uint16_t)strlen(aText));
	}
}


/**
 * @brief 在回复上位机后执行软件复位。
 *
 * SET_NET、RESET_NET、REBOOT 等命令需要先让上位机收到确认消息，再让单片机
 * 复位重新初始化 lwIP。这里固定延时 300 ms，给 tcp_write()/tcp_output()
 * 留出发送时间。
 */
static void device_service_reboot_after_reply(void)
{
	/* 给 tcp_write()/tcp_output() 留出发送时间，避免上位机还没收到 OK 就复位。 */
	HAL_Delay(300U);
	NVIC_SystemReset();
}


/**
 * @brief 解析点分十进制 IPv4 字符串。
 *
 * @param aText 输入字符串，格式示例为 `192.168.1.30`。
 * @param aIp   输出 IP 数组，长度必须至少为 4 字节，由调用者提供。
 *
 * @retval 0 解析成功。
 * @retval <0 输入为空、格式错误或字段超出 0~255。
 */
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


/**
 * @brief 从命令行中解析一个 IPv4 字段。
 *
 * @param aLine 输入命令行，例如 `SET_NET,IP=192.168.1.50,...`。
 * @param aKey  字段关键字，例如 `IP=`、`MASK=`、`GW=`。
 * @param aIp   输出 IP 数组，长度必须至少为 4 字节。
 *
 * @retval 0 解析成功。
 * @retval <0 未找到字段或字段格式非法。
 */
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


/**
 * @brief 从命令行中解析一个 16 位无符号整数字段。
 *
 * @param aLine     输入命令行。
 * @param aKey      字段关键字，例如 `PORT=` 或 `TCP=`。
 * @param aValue    输出数值，由调用者提供存储空间。
 * @param aRequired 是否必填；非 0 表示缺少字段时报错。
 *
 * @retval 0 字段存在且解析成功。
 * @retval 1 字段不存在但允许省略。
 * @retval <0 字段缺失且必填，或字段格式非法。
 */
static int device_service_parse_field_u16(const char *aLine,
										  const char *aKey,
										  uint16_t *aValue,
										  uint8_t aRequired)
{
	const char *pField = strstr(aLine, aKey);
	char *pEnd;
	unsigned long value;

	if (pField == 0)
	{
		return (aRequired != 0U) ? -1 : 1;
	}

	pField += strlen(aKey);
	value = strtoul(pField, &pEnd, 10);

	if ((pEnd == pField) || (value > 65535UL))
	{
		return -1;
	}

	if ((*pEnd != '\0') && (*pEnd != ',') && (*pEnd != '\r') && (*pEnd != '\n'))
	{
		return -1;
	}

	*aValue = (uint16_t)value;
	return 0;
}


/**
 * @brief 格式化当前网络配置回复帧。
 *
 * @param aBuffer 输出缓冲区，由调用者提供。
 * @param aSize   输出缓冲区长度，单位字节。
 *
 * 生成的数据会通过 TCP 发送给上位机，格式如下：
 * `NET,IP=...,MASK=...,GW=...,TCP=...\r\n`。
 */
static void device_service_format_current_net(char *aBuffer, uint16_t aSize)
{
	snprintf(aBuffer,
			 aSize,
			 "NET,IP=%d.%d.%d.%d,MASK=%d.%d.%d.%d,GW=%d.%d.%d.%d,TCP=%u\r\n",
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
			 g_lwipdev.gateway[3],
			 g_lwipdev.tcp_port);
}


/**
 * @brief 将网络配置同步到 lwIP 运行态结构体。
 *
 * @param aConfig 待同步配置，本函数只读取其内容，不保存指针。
 *
 * NOTE：该函数只更新 RAM 中的 g_lwipdev。真正让 lwIP netif 和 TCP Server
 * 使用新配置，需要软件复位后重新初始化网络。
 */
static void device_service_apply_config_to_ram(const net_config_t *aConfig)
{
	if (aConfig == 0)
	{
		return;
	}

	memcpy(g_lwipdev.ip, aConfig->ip, sizeof(g_lwipdev.ip));
	memcpy(g_lwipdev.netmask, aConfig->netmask, sizeof(g_lwipdev.netmask));
	memcpy(g_lwipdev.gateway, aConfig->gateway, sizeof(g_lwipdev.gateway));
	g_lwipdev.tcp_port = aConfig->tcp_port;
}


/**
 * @brief 处理查询当前网络配置命令。
 *
 * 命令来源为 `GET_NET\r\n` 或 `GET_NET?\r\n`。回复中包含当前 IP、子网掩码、
 * 网关和 TCP 服务端口。
 */
static void device_service_handle_get_net(void)
{
	char reply[128];

	device_service_format_current_net(reply, sizeof(reply));
	device_service_send_text(reply);
}


/**
 * @brief 处理上位机修改网络配置命令。
 *
 * @param aLine 完整命令行，示例：
 * `SET_NET,IP=192.168.1.50,MASK=255.255.255.0,GW=192.168.1.1,PORT=8081`。
 *
 * 执行流程：
 * 1. 解析 IP、MASK、GW。
 * 2. 解析可选的 PORT= 或 TCP= 字段。
 * 3. 校验配置合法性。
 * 4. 写入 Flash。
 * 5. 回复成功并自动复位。
 */
static void device_service_handle_set_net(const char *aLine)
{
	net_config_t config;

	config.tcp_port = g_lwipdev.tcp_port;

	if ((device_service_parse_field_ip(aLine, "IP=", config.ip) != 0) ||
		(device_service_parse_field_ip(aLine, "MASK=", config.netmask) != 0) ||
		(device_service_parse_field_ip(aLine, "GW=", config.gateway) != 0) ||
		(device_service_parse_field_u16(aLine, "PORT=", &config.tcp_port, 0U) < 0) ||
		(device_service_parse_field_u16(aLine, "TCP=", &config.tcp_port, 0U) < 0))
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


/**
 * @brief 处理恢复默认网络配置命令。
 *
 * 命令来源为 `RESET_NET\r\n` 或 `FACTORY_NET\r\n`。本函数会擦除 Flash 中保存
 * 的网络配置，然后把 RAM 中的配置恢复为默认值，最后回复并自动复位。
 */
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


/**
 * @brief 分发一条完整应用层命令。
 *
 * @param aLine 可修改的命令行缓冲区。函数会原地去掉末尾空格、`\r`、`\n`。
 *
 * 本函数只处理已经按行聚合完成的命令。TCP 粘包和拆包处理放在
 * device_service_on_rx() 中完成。
 */
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


/**
 * @brief 初始化设备业务服务层。
 *
 * @param aComm   通信桥实例，当前由 comm_lwip_tcp_get_bridge() 提供。
 * @param aSensor 采集桥实例，当前由 sensor_adc_dma_get_bridge() 提供。
 *
 * 本函数只保存桥接接口，并调用采集桥初始化函数。业务层不直接依赖 TCP PCB
 * 或 ADC DMA 缓冲区。
 */
void device_service_init(const comm_bridge_t *aComm, const sensor_bridge_t *aSensor)
{
	/* 只保存接口，不保存具体实现细节，便于后续替换通信或采集实现。 */
	s_comm = aComm;
	s_sensor = aSensor;
	s_adc_send_tick = HAL_GetTick();
	s_adc_frame_seq = 0U;
	s_rx_line_len = 0U;

	if ((s_sensor != 0) && (s_sensor->init != 0))
	{
		s_sensor->init();
	}
}


/**
 * @brief 业务层周期调度函数。
 *
 * 主循环或 TCP Server 循环需要持续调用本函数。当前职责：
 * 1. 判断上位机是否已经建立 TCP 连接。
 * 2. 按 2 秒周期读取 ADC DMA 采样结果。
 * 3. 调用协议层生成文本帧。
 * 4. 通过通信桥发送给上位机。
 */
void device_service_poll(void)
{
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
	if (s_sensor->read(&s_adc_sample) != 0)
	{
		return;
	}

	len = protocol_text_format_adc_binary(&s_adc_sample,
										  s_adc_frame_seq,
										  s_adc_frame,
										  sizeof(s_adc_frame));

	if ((len > 0) && (len <= (int)sizeof(s_adc_frame)))
	{
		(void)s_comm->send(s_adc_frame, (uint16_t)len);
		++s_adc_frame_seq;
	}
}


/**
 * @brief 处理 lwIP 解封装后的 TCP payload。
 *
 * @param aData TCP payload 数据，不包含 Ethernet/IP/TCP 头。
 * @param aLen  payload 长度，单位字节。
 *
 * TCP 是字节流，不保证一包就是一条命令。本函数负责：
 * - 兼容无换行普通文本的回显测试。
 * - 识别命令前缀。
 * - 按 `\n` 聚合命令行。
 * - 调用 device_service_handle_line() 执行业务命令。
 */
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
