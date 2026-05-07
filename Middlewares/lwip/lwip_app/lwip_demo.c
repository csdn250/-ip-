/**
 ****************************************************************************************************
 * @file        lwip_demo
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2022-8-01
 * @brief       lwIP RAW TCPServer 实验
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 阿波罗 H743开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */
 
#include <stdint.h>
#include <stdio.h>
#include "lcd.h"
#include "./MALLOC/malloc.h"
#include "key.h"
#include "delay.h"
#include "led.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip/tcp.h"
#include "lwip_demo.h"
#include "lwip_comm.h"
#include "comm_lwip_tcp.h"
#include "device_service.h"
#include "stdio.h"
#include "string.h"


#define LWIP_DEMO_RX_BUFSIZE         2000   /* 定义最大接收数据长度 */
#define LWIP_DEMO_PORT               8080   /* 定义连接的目的/本地端口号 */

/* 接收数据缓冲区 */
uint8_t g_lwip_demo_recvbuf[LWIP_DEMO_RX_BUFSIZE];  
/* 发送数据内容 */
char *g_lwip_demo_sendbuf = "ALIENTEK DATA\r\n";

/* TCP Server 测试全局状态标记变量
 * bit7:0,没有数据要发送;1,有数据要发送
 * bit6:0,没有收到数据;1,收到数据了.
 * bit5:0,没有客户端连接上;1,有客户端连接上了.
 * bit4~0:保留 */
uint8_t g_lwip_send_flag;
static struct tcp_pcb *g_lwip_tcp_client_pcb;         /* 当前已连接的TCP客户端 */

/* tcp服务器连接状态 */
enum tcp_server_states
{
    ES_TCPSERVER_NONE = 0,                  /* 初始化 */
    ES_TCPSERVER_ACCEPTED,                  /* 连接状态 */
    ES_TCPSERVER_CLOSING,                   /* 关闭连接状态 */
};

/* LWIP回调函数使用的结构体 */
struct tcp_server_struct
{
    uint8_t state;                          /* 当前状态 */
    struct tcp_pcb *pcb;                    /* 指向TCP控制块 */
    struct pbuf *p;                         /* 指向接收/传输的pbuf */
};

/**
 * @brief TCP 客户端连接建立回调。
 *
 * 上位机连接 MCU_IP:8080 成功后，lwIP 调用本函数。
 * 本函数负责注册该连接后续的 recv/sent/error/poll 回调，
 * 并把 newpcb 交给 comm_lwip_tcp 桥接层保存。
 */
err_t lwip_tcp_server_accept(void *arg,struct tcp_pcb *newpcb,err_t err);
/**
 * @brief TCP 数据接收回调。
 *
 * 调用边界：
 * - 进入本函数时，lwIP 已经完成 TCP/IP 协议解析。
 * - pbuf 中保存的是上位机发送的 TCP payload。
 * - 本函数只做 pbuf 链复制和桥接转发，不解析业务命令。
 * - 业务命令解析应放到 device_service_on_rx()。
 */
err_t lwip_tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
void lwip_tcp_server_error(void *arg,err_t err);
err_t lwip_tcp_server_usersent(struct tcp_pcb *tpcb);
err_t lwip_tcp_server_poll(void *arg, struct tcp_pcb *tpcb);
err_t lwip_tcp_server_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);
void lwip_tcp_server_senddata(struct tcp_pcb *tpcb, struct tcp_server_struct *es);
void lwip_tcp_server_connection_close(struct tcp_pcb *tpcb, struct tcp_server_struct *es);
void lwip_tcp_server_remove_timewait(void);

/**
 * @brief RAW TCP Server 接入层入口。
 *
 * 本函数只负责网络接入和 lwIP 回调注册：
 * - 创建 TCP PCB。
 * - 绑定本地 8080 端口。
 * - 进入监听状态。
 * - 注册 accept/recv/sent/error/poll 回调。
 *
 * 业务逻辑不应继续堆在本文件中，应该放到 Core/App/device_service.c。
 */
void lwip_demo(void)
{
    err_t err;
    struct tcp_pcb *tcppcbnew;          /* 定义一个TCP服务器控制块 */
    struct tcp_pcb *tcppcbconn;         /* 定义一个TCP服务器控制块 */

    char *tbuf;
    uint8_t key;
    uint8_t res = 0;
    uint8_t t = 0;

    lcd_clear(WHITE);                   /* 清屏 */
    g_point_color = RED;
    lcd_show_string(5, 24, lcddev.width - 10, 32, 32, "STM32", g_point_color);
    lcd_show_string(5, 62, lcddev.width - 10, 24, 24, "TCP Server", g_point_color);
    lcd_show_string(5, 92, lcddev.width - 10, 24, 24, "KEY0:Send", g_point_color);
    tbuf = mymalloc(SRAMIN, 200);       /* 申请内存 */

    if (tbuf == NULL)return ;           /* 内存申请失败了,直接退出 */

    sprintf((char *)tbuf, "Server IP:%d.%d.%d.%d", g_lwipdev.ip[0], g_lwipdev.ip[1], g_lwipdev.ip[2], g_lwipdev.ip[3]); /* 服务器IP */
    lcd_show_string(5, 130, lcddev.width - 10, 24, 24, tbuf, g_point_color);
    sprintf((char *)tbuf, "Server Port:%d", LWIP_DEMO_PORT);                                                            /* 服务器端口号 */
    lcd_show_string(5, 160, lcddev.width - 10, 24, 24, tbuf, g_point_color);
    /* 创建 TCP 控制块。PCB 是 lwIP 用于维护一个 TCP 端点状态的核心对象。 */
    tcppcbnew = tcp_new();              /* 创建一个新的pcb */

    if (tcppcbnew)                      /* 创建成功 */
    {
        /* 绑定本地端口。IP_ADDR_ANY 表示允许上位机连接本机任意可用网卡 IP 的 8080 端口。 */
        err = tcp_bind(tcppcbnew, IP_ADDR_ANY, LWIP_DEMO_PORT); /* 将本地IP与指定的端口号绑定在一起,IP_ADDR_ANY为绑定本地所有的IP地址 */

        if (err == ERR_OK)              /* 绑定完成 */
        {
            tcppcbconn = tcp_listen(tcppcbnew);                 /* 设置tcppcb进入监听状态 */
            /* 注册连接回调。上位机 TCP Client 建立连接后，会进入 lwip_tcp_server_accept()。 */
            tcp_accept(tcppcbconn, lwip_tcp_server_accept);     /* 初始化LWIP的tcp_accept的回调函数 */
        }
        else res = 1;
    }
    else res = 1;

    g_point_color = BLUE;

    while (res == 0)
    {
        key = key_scan(0);

        if (key == KEY0_PRES)                                   /* KEY0按下了,发送数据 */
        {
            if (g_lwip_tcp_client_pcb != NULL)lwip_tcp_server_usersent(g_lwip_tcp_client_pcb); /* 发送数据 */
        }

        if (g_lwip_send_flag & 1 << 6)                          /* 是否收到数据 */
        {
            lcd_fill(5, 210, lcddev.width - 1, lcddev.height - 1, WHITE);                                                       /* 清上一次数据 */
            lcd_show_string(5, 210, lcddev.width - 30, lcddev.height - 210, 16, (char *)g_lwip_demo_recvbuf, g_point_color);    /* 显示接收到的数据 */
            g_lwip_send_flag &= ~(1 << 6);                      /* 标记数据已经被处理了 */
        }

        if (g_lwip_send_flag & 1 << 5)                          /* 是否连接上 */
        {
            sprintf((char *)tbuf, "Client IP:%d.%d.%d.%d", g_lwipdev.remoteip[0], g_lwipdev.remoteip[1], g_lwipdev.remoteip[2], g_lwipdev.remoteip[3]); /* 客户端IP */
            lcd_show_string(5, 190, lcddev.width - 10, 24, 24, tbuf, g_point_color);
            g_point_color = RED;
            lcd_show_string(5, 220, lcddev.width - 10, 24, 24, "Receive Data:", g_point_color); /* 提示消息 */
            g_point_color = BLUE;
        }
        else if (g_lwip_send_flag & 1 << 5)
        {
            lcd_fill(5, 170, lcddev.width - 1, lcddev.height - 1, WHITE);   /* 清屏 */
        }
        /* 调度业务层任务：
         * - 判断是否已连接上位机。
         * - 周期性读取 ADC。
         * - 通过通信桥发送应用层文本帧。
         */
        device_service_poll();

        lwip_periodic_handle();
        lwip_pkt_handle();
        delay_ms(2);
        t++;

        if (t == 200)
        {
            t = 0;
            LED0_TOGGLE();
        }
    }
}

/**
 * @brief TCP 客户端连接建立回调。
 *
 * 上位机连接 MCU_IP:8080 成功后，lwIP 调用本函数。
 * 本函数负责注册该连接后续的 recv/sent/error/poll 回调，
 * 并把 newpcb 交给 comm_lwip_tcp 桥接层保存。
 */
err_t lwip_tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    err_t ret_err;
    struct tcp_server_struct *es;
    LWIP_UNUSED_ARG(arg);
    LWIP_UNUSED_ARG(err);
    tcp_setprio(newpcb, TCP_PRIO_MIN);                                  /* 设置新创建的pcb优先级 */
    es = (struct tcp_server_struct *)mem_malloc(sizeof(struct tcp_server_struct)); /* 分配内存 */

    if (es != NULL)                                                     /* 内存分配成功 */
    {
        es->state = ES_TCPSERVER_ACCEPTED;                              /* 接收连接 */
        es->pcb = newpcb;
        es->p = NULL;
        
        tcp_arg(newpcb, es);
        tcp_recv(newpcb, lwip_tcp_server_recv);                         /* 初始化tcp_recv()的回调函数 */
        tcp_err(newpcb, lwip_tcp_server_error);                         /* 初始化tcp_err()回调函数 */
        tcp_poll(newpcb, lwip_tcp_server_poll, 1);                      /* 初始化tcp_poll回调函数 */
        tcp_sent(newpcb, lwip_tcp_server_sent);                         /* 初始化发送回调函数 */

        g_lwip_send_flag |= 1 << 5;                                     /* 标记有客户端连上了 */
        g_lwip_tcp_client_pcb = newpcb;                                  /* 记录当前客户端连接 */
        /* 将当前 TCP 连接交给通信桥。
         * 后续 device_service.c 发送 ADC 数据时，会通过该连接调用 tcp_write()。
         */
        comm_lwip_tcp_set_client(newpcb);                                /* 桥接层记录当前TCP连接 */
        g_lwipdev.remoteip[0] = newpcb->remote_ip.addr & 0xff;          /* IADDR4 */
        g_lwipdev.remoteip[1] = (newpcb->remote_ip.addr >> 8) & 0xff;   /* IADDR3 */
        g_lwipdev.remoteip[2] = (newpcb->remote_ip.addr >> 16) & 0xff;  /* IADDR2 */
        g_lwipdev.remoteip[3] = (newpcb->remote_ip.addr >> 24) & 0xff;  /* IADDR1 */
        ret_err = ERR_OK;
    }
    else
    {
        ret_err = ERR_MEM;
    }

    return ret_err;
}

/**
 * @brief TCP 数据接收回调。
 *
 * 调用边界：
 * - 进入本函数时，lwIP 已经完成 TCP/IP 协议解析。
 * - pbuf 中保存的是上位机发送的 TCP payload。
 * - 本函数只做 pbuf 链复制和桥接转发，不解析业务命令。
 * - 业务命令解析应放到 device_service_on_rx()。
 */
err_t lwip_tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    err_t ret_err;
    uint32_t data_len = 0;
    uint32_t copy_len;
    struct pbuf *q;
    struct tcp_server_struct *es;
    LWIP_ASSERT("arg != NULL", arg != NULL);
    es = (struct tcp_server_struct *)arg;

    if (p == NULL)                                                          /* 从客户端接收到空数据 */
    {
        es->state = ES_TCPSERVER_CLOSING;                                   /* 需要关闭TCP 连接了 */
        es->p = p;
        ret_err = ERR_OK;
    }
    else if (err != ERR_OK)                                                 /* 从客户端接收到一个非空数据,但是由于某种原因err!=ERR_OK */
    {
        if (p)pbuf_free(p);                                                 /* 释放接收pbuf */

        ret_err = err;
    }
    else if (es->state == ES_TCPSERVER_ACCEPTED)                            /* 处于连接状态 */
    {
        if (p != NULL)                                                      /* 当处于连接状态并且接收到的数据不为空时将其打印出来 */
        {
            memset(g_lwip_demo_recvbuf, 0, LWIP_DEMO_RX_BUFSIZE);           /* 数据接收缓冲区清零 */

            for (q = p; q != NULL; q = q->next)                             /* 遍历完整个pbuf链表 */
            {
                /* 判断要拷贝到LWIP_DEMO_RX_BUFSIZE中的数据是否大于LWIP_DEMO_RX_BUFSIZE的剩余空间，如果大于 */
                /* 的话就只拷贝LWIP_DEMO_RX_BUFSIZE中剩余长度的数据，否则的话就拷贝所有的数据 */
                copy_len = q->len;

                if (copy_len > (LWIP_DEMO_RX_BUFSIZE - data_len)) copy_len = LWIP_DEMO_RX_BUFSIZE - data_len;

                if (copy_len > 0)
                {
                    memcpy(g_lwip_demo_recvbuf + data_len, q->payload, copy_len);
                    data_len += copy_len;
                }

                if (data_len >= LWIP_DEMO_RX_BUFSIZE) break;                /* 超出TCP客户端接收数组,跳出 */
            }
            if (data_len > 0)
            {
                /* pbuf payload 是 TCP 应用层数据。
                 * 进入该回调前，lwIP 已经完成以太网头、IP 头、TCP 头解析。
                 */
                comm_lwip_tcp_on_receive(g_lwip_demo_recvbuf, (uint16_t)data_len);
            }

            g_lwip_send_flag |= 1 << 6;                                     /* 标记接收到数据了 */
            g_lwipdev.remoteip[0] = tpcb->remote_ip.addr & 0xff;            /* IADDR4 */
            g_lwipdev.remoteip[1] = (tpcb->remote_ip.addr >> 8) & 0xff;     /* IADDR3 */
            g_lwipdev.remoteip[2] = (tpcb->remote_ip.addr >> 16) & 0xff;    /* IADDR2 */
            g_lwipdev.remoteip[3] = (tpcb->remote_ip.addr >> 24) & 0xff;    /* IADDR1 */
            tcp_recved(tpcb, p->tot_len);                                   /* 用于获取接收数据,通知LWIP可以获取更多数据 */
            pbuf_free(p);                                                   /* 释放内存 */
            ret_err = ERR_OK;
        }
    }
    else                                                                    /* 服务器关闭了 */
    {
        tcp_recved(tpcb, p->tot_len);                                       /* 用于获取接收数据,通知LWIP可以获取更多数据 */
        es->p = NULL;
        pbuf_free(p);                                                       /* 释放内存 */
        ret_err = ERR_OK;
    }

    return ret_err;
}



/**
 * @brief       lwIP tcp_err函数的回调函数
 * @param       arg ：传入的参数
 * @param       err ：错误码
 * @retval      无
 */
void lwip_tcp_server_error(void *arg, err_t err)
{
    LWIP_UNUSED_ARG(err);
    printf("tcp error:%x\r\n", (uint32_t)arg);
    g_lwip_tcp_client_pcb = NULL;

    if (arg != NULL)mem_free(arg); /* 释放内存 */
}

/**
 * @brief       lwIP数据发送，用户应用程序调用此函数来发送数据
 * @param       tpcb ：TCP控制块
 * @retval      返回值:0，成功；其他，失败
 */
err_t lwip_tcp_server_usersent(struct tcp_pcb *tpcb)
{
    err_t ret_err;
    struct tcp_server_struct *es;
    es = tpcb->callback_arg;

    if (es != NULL)                                                                         /* 连接处于空闲可以发送数据 */
    {
        es->p = pbuf_alloc(PBUF_TRANSPORT, strlen((char *)g_lwip_demo_sendbuf), PBUF_POOL); /* 申请内存 */
        pbuf_take(es->p, (char *)g_lwip_demo_sendbuf, strlen((char *)g_lwip_demo_sendbuf)); /* 将lwip_tcp_server_sentbuf[]中的数据拷贝到es->p_tx中 */
        lwip_tcp_server_senddata(tpcb, es);                                                 /* 将lwip_tcp_server_sentbuf[]里面复制给pbuf的数据发送出去 */
        g_lwip_send_flag &= ~(1 << 7);                                                      /* 清除数据发送标志 */

        if (es->p != NULL)pbuf_free(es->p);                                                 /* 释放内存 */

        ret_err = ERR_OK;
    }
    else
    {
        tcp_abort(tpcb);                                                                    /* 终止连接,删除pcb控制块 */
        ret_err = ERR_ABRT;
    }

    return ret_err;
}

/**
 * @brief       lwIP tcp_poll的回调函数
 * @param       arg  ：传入的参数
 * @param       tpcb ：TCP控制块
 * @retval      无
 */
err_t lwip_tcp_server_poll(void *arg, struct tcp_pcb *tpcb)
{
    err_t ret_err;
    struct tcp_server_struct *es;
    es = (struct tcp_server_struct *)arg;

    if (es->state == ES_TCPSERVER_CLOSING)              /* 需要关闭连接?执行关闭操作 */
    {
        lwip_tcp_server_connection_close(tpcb, es);     /* 关闭连接 */
    }

    ret_err = ERR_OK;
    return ret_err;
}

/**
 * @brief       lwIP tcp_sent的回调函数(当从远端主机接收到ACK信号后发送数据)
 * @param       arg  ：传入的参数
 * @param       tpcb ：TCP控制块
 * @param       len  ：发送的长度
 * @retval      无
 */
err_t lwip_tcp_server_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    struct tcp_server_struct *es;
    LWIP_UNUSED_ARG(len);
    es = (struct tcp_server_struct *) arg;

    if (es->p)lwip_tcp_server_senddata(tpcb, es);   /* 发送数据 */

    return ERR_OK;
}

/**
 * @brief       此函数用来发送数据
 * @param       tpcb ：TCP控制块
 * @param       es   ：LWIP回调函数使用的结构体
 * @retval      无
 */
void lwip_tcp_server_senddata(struct tcp_pcb *tpcb, struct tcp_server_struct *es)
{
    struct pbuf *ptr;
    uint16_t plen;
    err_t wr_err = ERR_OK;

    while ((wr_err == ERR_OK) && es->p && (es->p->len <= tcp_sndbuf(tpcb)))
    {
        ptr = es->p;
        wr_err = tcp_write(tpcb, ptr->payload, ptr->len, 1);    /* 将要发送的数据加入发送缓冲队列中 */

        if (wr_err == ERR_OK)
        {
            plen = ptr->len;
            es->p = ptr->next;                                  /* 指向下一个pbuf */

            if (es->p)pbuf_ref(es->p);                          /* pbuf的ref加一 */

            pbuf_free(ptr);
            tcp_recved(tpcb, plen);
        }
        else if (wr_err == ERR_MEM) es->p = ptr;
        tcp_output(tpcb);                                       /* 将发送缓冲队列中的数据发送出去 */
    }
}

/**
 * @brief       关闭tcp连接
 * @param       tpcb ：TCP控制块
 * @param       es   ：LWIP回调函数使用的结构体
 * @retval      无
 */
void lwip_tcp_server_connection_close(struct tcp_pcb *tpcb, struct tcp_server_struct *es)
{
    tcp_close(tpcb);
    tcp_arg(tpcb, NULL);
    tcp_sent(tpcb, NULL);
    tcp_recv(tpcb, NULL);
    tcp_err(tpcb, NULL);
    tcp_poll(tpcb, NULL, 0);

    if (es)mem_free(es);
    if (g_lwip_tcp_client_pcb == tpcb)
    {
        g_lwip_tcp_client_pcb = NULL;
        comm_lwip_tcp_set_client(NULL);
    }
    g_lwip_send_flag &= ~(1 << 5);                  /* 标记连接断开了 */
}

extern void tcp_pcb_purge(struct tcp_pcb *pcb);     /* 在 tcp.c里面 */
extern struct tcp_pcb *tcp_active_pcbs;             /* 在 tcp.c里面 */
extern struct tcp_pcb *tcp_tw_pcbs;                 /* 在 tcp.c里面 */

/**
 * @brief       强制删除TCP Server主动断开时的time wait
 * @param       无
 * @retval      无
 */
void lwip_tcp_server_remove_timewait(void)
{
    struct tcp_pcb *pcb, *pcb2;
    uint8_t t = 0;

    while (tcp_active_pcbs != NULL && t < 200)
    {
        lwip_periodic_handle();                     /* 继续轮询 */
        t++;
        delay_ms(10);                               /* 等待tcp_active_pcbs为空 */
    }

    pcb = tcp_tw_pcbs;

    while (pcb != NULL)                             /* 如果有等待状态的pcbs */
    {
        tcp_pcb_purge(pcb);
        tcp_tw_pcbs = pcb->next;
        pcb2 = pcb;
        pcb = pcb->next;
        memp_free(MEMP_TCP_PCB, pcb2);
    }
}








