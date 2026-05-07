/**
 ****************************************************************************************************
 * @file        main.c
 * @author      ALIENTEK / Codex
 * @date        2026-05-07
 * @brief       lwIP RAW TCP ADC 桥接实验主程序。
 *
 * 本文件负责系统启动、网络初始化、TCP Server 启动和业务层周期调度。
 ****************************************************************************************************
 */

#include <stdio.h>

#include "sys.h"
#include "usart.h"
#include "delay.h"
#include "led.h"
#include "lcd.h"
#include "key.h"
#include "adc.h"
#include "mpu.h"
#include "ltdc.h"
#include "sdram.h"
#include "MALLOC/malloc.h"
#include "pcf8574.h"
#include "lwipopts.h"
#include "lwip_comm.h"
#include "lwip_demo.h"
#include "comm_lwip_tcp.h"
#include "comm_lwip_udp_discover.h"
#include "device_service.h"
#include "net_config.h"
#include "sensor_adc_dma.h"


/**
 * @brief 加载网络实验界面。
 *
 * @param mode bit0 为 1 时加载标题区域；bit1 为 1 时加载网络状态区域。
 */
void lwip_test_ui(uint8_t mode)
{
	uint8_t speed;
	uint8_t buf[40];

	if (mode & (1U << 0))
	{
		lcd_show_string(6, 10, 200, 32, 32, "STM32", DARKBLUE);
		lcd_show_string(6, 40, lcddev.width, 24, 24, "lwIP TcpServer Test", DARKBLUE);
		lcd_show_string(6, 72, lcddev.width - 12, 24, 24, "ATOM@ALIENTEK", DARKBLUE);
	}

	if (mode & (1U << 1))
	{
		lcd_show_string(6, 110, lcddev.width - 12, 24, 24, "lwIP Init OK", MAGENTA);

		if (g_lwipdev.dhcpstatus == 2)
		{
			sprintf((char *)buf,
					"DHCP:%d.%d.%d.%d",
					g_lwipdev.ip[0],
					g_lwipdev.ip[1],
					g_lwipdev.ip[2],
					g_lwipdev.ip[3]);
		} else {
			sprintf((char *)buf,
					"IP:%d.%d.%d.%d",
					g_lwipdev.ip[0],
					g_lwipdev.ip[1],
					g_lwipdev.ip[2],
					g_lwipdev.ip[3]);
		}

		lcd_show_string(6, 140, lcddev.width - 12, 24, 24, (char *)buf, MAGENTA);

		speed = ethernet_chip_get_speed();

		if (speed)
		{
			lcd_show_string(6, 170, lcddev.width - 12, 24, 24, "Speed:100M", MAGENTA);
		} else {
			lcd_show_string(6, 170, lcddev.width - 12, 24, 24, "Speed:10M", MAGENTA);
		}
	}
}


/**
 * @brief 主程序入口。
 *
 * 执行流程：
 * 1. 初始化 MPU、Cache、HAL、系统时钟和基础外设。
 * 2. 处理“忘记 IP”现场恢复入口，长按 KEY0 可清除 Flash 中保存的网络配置。
 * 3. 绑定业务层需要的通信桥和采集桥。
 * 4. 初始化 lwIP 协议栈和以太网网卡。
 * 5. 启动 RAW TCP Server，监听上位机连接。
 * 6. 在主循环中持续调度 lwIP 和业务层任务。
 */
int main(void)
{
	uint8_t key;

	Write_Through();
	mpu_memory_protection();
	sys_cache_enable();
	HAL_Init();
	sys_stm32_clock_init(192, 5, 2, 4);
	delay_init(480);
	usart_init(115200);
	sdram_init();
	led_init();
	lcd_init();
	key_init();

	/* 忘记设备 IP 时的现场恢复入口：上电后持续按住 KEY0 约 2 秒。 */
	if (KEY0 == 0)
	{
		delay_ms(2000);

		if (KEY0 == 0)
		{
			net_config_clear();
			lcd_clear(WHITE);
			lcd_show_string(6, 110, lcddev.width - 12, 24, 24, "Net reset", RED);
			lcd_show_string(6, 140, lcddev.width - 12, 24, 24, "IP:192.168.1.30", RED);
			delay_ms(800);
		}
	}

	/* 业务层只拿到桥接接口，不直接依赖 lwIP 或 ADC DMA 的实现细节。 */
	device_service_init(comm_lwip_tcp_get_bridge(), sensor_adc_dma_get_bridge());

	while (pcf8574_init())
	{
		lcd_show_string(30, 170, 200, 16, 16, "PCF8574 Check Failed!", RED);
		delay_ms(500);
		lcd_show_string(30, 170, 200, 16, 16, "Please Check!      ", RED);
		delay_ms(500);
		LED0_TOGGLE();
	}

	my_mem_init(SRAMIN);
	my_mem_init(SRAMEX);
	my_mem_init(SRAMDTCM);

	lwip_test_ui(1);
	lcd_show_string(6, 110, 200, 16, 16, "lwIP Init !!", BLUE);

	while (lwip_comm_init() != 0)
	{
		lcd_show_string(6, 110, 200, 16, 16, "lwIP Init failed!!", BLUE);
		delay_ms(500);
		lcd_fill(6, 50, 230, 66, WHITE);
		lcd_show_string(6, 110, 200, 16, 16, "Retrying...       ", BLUE);
		delay_ms(500);
		LED1_TOGGLE();
	}

#if LWIP_DHCP
	lcd_show_string(6, 130, 200, 16, 16, "DHCP IP configing... ", BLUE);

	while ((g_lwipdev.dhcpstatus != 2) && (g_lwipdev.dhcpstatus != 0xFF))
	{
		lwip_pkt_handle();
		lwip_periodic_handle();
	}
#endif /* LWIP_DHCP */

	/* 启动 UDP 发现服务：上位机可广播搜索设备真实 IP，再发起 TCP 连接。 */
	if (comm_lwip_udp_discover_init() != 0)
	{
		printf("UDP discover init failed.\r\n");
	}

	/* 创建 TCP PCB，绑定当前配置的 TCP 端口，并注册 accept/recv/sent/error/poll 回调。 */
	lwip_demo();
	lwip_test_ui(2);

	while (1)
	{
		lwip_periodic_handle();
		lwip_pkt_handle();

		key = key_scan(0);

		if (key == KEY1_PRES)
		{
			if (g_lwip_send_flag & (1U << 5))
			{
				printf("TCP connected, ignore duplicate connect.\r\n");
			} else {
				lwip_demo();
			}
		}

		delay_ms(10);
	}
}
