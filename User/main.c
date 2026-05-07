

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
#include "./MALLOC/malloc.h"
#include "pcf8574.h"
#include "lwip_comm.h"
#include "lwipopts.h"
#include "lwip_demo.h"
#include "device_service.h"
#include "net_config.h"
#include "comm_lwip_tcp.h"
#include "sensor_adc_dma.h"


/**
 * @breif       加载UI
 * @param       mode :  bit0:0,不加载;1,加载前半部分UI
 *                      bit1:0,不加载;1,加载后半部分UI
 * @retval      无
 */
void lwip_test_ui(uint8_t mode)
{
    uint8_t speed;
    uint8_t buf[30];
    
    if(mode & 1<< 0)
    {
        lcd_show_string(6, 10, 200, 32, 32, "STM32", DARKBLUE);
        lcd_show_string(6, 40, lcddev.width, 24, 24, "lwIP TcpServer Test", DARKBLUE);
        lcd_show_string(6, 70, 200, 16, 16, "ATOM@ALIENTEK", DARKBLUE);
    }
    
    if(mode & 1 << 1)
    {
        lcd_show_string(6, 110, 200, 16, 16, "lwIP Init Successed", MAGENTA);
        
        if(g_lwipdev.dhcpstatus == 2)
        {
            sprintf((char*)buf,"DHCP IP:%d.%d.%d.%d",g_lwipdev.ip[0],g_lwipdev.ip[1],g_lwipdev.ip[2],g_lwipdev.ip[3]);      /* 显示动态IP地址 */
        }
        else
        {
            sprintf((char*)buf,"Static IP:%d.%d.%d.%d",g_lwipdev.ip[0],g_lwipdev.ip[1],g_lwipdev.ip[2],g_lwipdev.ip[3]);    /* 打印静态IP地址 */
        }
        
        lcd_show_string(6, 130, 200, 16, 16, (char*)buf, MAGENTA);
        
        speed = ethernet_chip_get_speed();                                                                                  /* 得到网速 */
        
        if(speed)
        {
            lcd_show_string(6, 150, 200, 16, 16, "Ethernet Speed:100M", MAGENTA);
        }
        else
        {
            lcd_show_string(6, 150, 200, 16, 16, "Ethernet Speed:10M", MAGENTA);
        }
    }
}

/**
 * @brief 主程序入口。
 *
 * 执行顺序说明：
 * 1. 初始化 MPU、Cache、HAL、系统时钟和基础外设。
 * 2. 绑定业务层所需的通信桥和传感器桥。
 * 3. 初始化 lwIP 协议栈和以太网网卡。
 * 4. 启动 RAW TCP Server，监听上位机连接。
 * 5. 在循环中持续调度 lwIP 和业务层任务。
 */
int main(void)
{
    uint8_t key;
    
    Write_Through();
    mpu_memory_protection();                 /* 保护相关存储区域 */
    sys_cache_enable();                      /* 打开L1-Cache */
    HAL_Init();                              /* 初始化HAL库 */
    sys_stm32_clock_init(192, 5, 2, 4);      /* 设置时钟, 480Mhz */
    delay_init(480);                         /* 延时初始化 */
    usart_init(115200);                      /* 串口初始化 */
    sdram_init();                            /* 初始化SDRAM */
    led_init();                              /* 初始化LED */
    lcd_init();                              /* 初始化LCD */
    key_init();                              /* 初始化KEY */

    /* 忘记设备 IP 时的现场恢复入口：
     * 上电后持续按住 KEY0 约 2 秒，会擦除 Flash 中保存的网络配置。
     * 擦除后本次启动和后续上电都会回到默认静态 IP: 192.168.1.30。
     */
    if (KEY0 == 0)
    {
        delay_ms(2000);
        if (KEY0 == 0)
        {
            net_config_clear();
            lcd_clear(WHITE);
            lcd_show_string(6, 110, 230, 16, 16, "Net config reset", RED);
            lcd_show_string(6, 130, 230, 16, 16, "Default IP:192.168.1.30", RED);
            delay_ms(800);
        }
    }

    /* 绑定业务层依赖的两个底层实现：
     * - comm_lwip_tcp_get_bridge(): 通过 lwIP RAW TCP 与上位机通信。
     * - sensor_adc_dma_get_bridge(): 通过 ADC DMA 获取实时电压数据。
     * 业务层只依赖桥接接口，不直接依赖 lwIP 或 ADC DMA 细节。
     */
    device_service_init(comm_lwip_tcp_get_bridge(), sensor_adc_dma_get_bridge()); /* 初始化业务桥接层 */
    
    while (pcf8574_init())                   /* 检测不到PCF8574 */
    {
        lcd_show_string(30, 170, 200, 16, 16, "PCF8574 Check Failed!", RED);
        delay_ms(500);
        lcd_show_string(30, 170, 200, 16, 16, "Please Check!      ", RED);
        delay_ms(500);
        LED0_TOGGLE();                       /* 红灯闪烁 */
    }
    
    my_mem_init(SRAMIN);                     /* 初始化内部内存池 */
    my_mem_init(SRAMEX);                     /* 初始化外部内存池 */
    my_mem_init(SRAMDTCM);                   /* 初始化CCM内存池 */
    
    lwip_test_ui(1);                         /* 加载前半部分UI */

    lcd_show_string(6, 110, 200, 16, 16, "lwIP Init !!", BLUE);
    
    /* 初始化 lwIP 协议栈、以太网驱动和 netif。
     * 失败时保持重试，避免网络硬件尚未就绪导致系统继续运行。
     */
    while (lwip_comm_init() != 0)
    {
        lcd_show_string(6, 110, 200, 16, 16, "lwIP Init failed!!", BLUE);
        delay_ms(500);
        lcd_fill(6, 50, 200 + 30, 50 + 16, WHITE);
        lcd_show_string(6, 110, 200, 16, 16, "Retrying...       ", BLUE);
        delay_ms(500);
        LED1_TOGGLE();
    }
    
#if LWIP_DHCP
    lcd_show_string(6, 130, 200, 16, 16, "DHCP IP configing... ", BLUE);    /* 开始DHCP */
    
    /* DHCP 期间仍必须持续调用 lwip_pkt_handle()/lwip_periodic_handle()。
     * DHCP 报文收发、超时处理都依赖这两个轮询入口推进。
     */
    while ((g_lwipdev.dhcpstatus != 2) && (g_lwipdev.dhcpstatus != 0XFF))   /* 等待DHCP获取成功/超时溢出 */
    {
        lwip_pkt_handle();
        lwip_periodic_handle();
    }
#endif
    
    /* 启动 TCP Server。
     * 进入该函数后会创建 TCP PCB，绑定 8080 端口，并注册 accept/recv 回调。
     */
    lwip_demo();                /* lwIP程序入口 */
    
    lwip_test_ui(2);            /* 加载后半部分UI */
    
    while(1)
    {
        lwip_periodic_handle(); /* LWIP轮询任务 */
        lwip_pkt_handle();
        
        key = key_scan(0);
        
        if (key == KEY1_PRES)
        {
            if ((g_lwip_send_flag & 1 << 5)) 
            {
                printf("TCP连接已经建立,不能重复连接\r\n"); /* 如果连接成功,不做任何处理 */
            }
            else 
            {
                lwip_demo();  /* 当断开连接后,调用lwip_demo()函数 */
            }
        }
        
        delay_ms(10);

    }
}



