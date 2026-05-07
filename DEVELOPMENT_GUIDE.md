# RAW_TCP_ADC_BRIDGE 技术开发文档

## 1. 项目目标

本项目基于 STM32H743、lwIP RAW API 和以太网，实现单片机与上位机之间的局域网通信。

当前功能包括：

- UDP 广播发现设备。
- TCP Server 接收上位机连接。
- 单片机周期采集 ADC 电压并通过 TCP 发送给上位机。
- 上位机通过 TCP 命令查询、修改、恢复单片机 IP 和 TCP 服务端口。
- 修改 IP 后写入 Flash，掉电后继续生效。
- 用户忘记 IP 时，可通过 KEY0 恢复默认 IP。

整体通信流程：

```text
上位机 UDP 广播 DISCOVER_DEVICE
    -> 单片机 UDP 回复 IP/MAC/设备名/TCP端口
    -> 上位机解析真实 IP
    -> 上位机 TCP 连接 单片机IP:发现回复中的TCP端口
    -> 单片机周期发送 ADC 数据
    -> 上位机可发送 GET_NET / SET_NET / RESET_NET 等命令
```

## 2. 工程目录说明

### 2.1 业务层目录

```text
Core/App
```

负责业务逻辑。当前主要文件：

```text
Core/App/device_service.c
Core/App/device_service.h
```

该层不直接操作 lwIP 的 `tcp_pcb`，也不直接操作 ADC DMA 缓冲区，而是通过桥接接口访问底层能力。

### 2.2 桥接接口目录

```text
Core/Bridge
```

负责定义“业务层需要什么能力”，不关心底层怎么实现。

当前文件：

```text
Core/Bridge/comm_bridge.h
Core/Bridge/sensor_bridge.h
```

### 2.3 底层实现目录

```text
Core/Impl
```

负责把具体硬件或协议栈封装成桥接接口。

当前文件：

```text
Core/Impl/comm_lwip_tcp.c
Core/Impl/comm_lwip_tcp.h
Core/Impl/comm_lwip_udp_discover.c
Core/Impl/comm_lwip_udp_discover.h
Core/Impl/sensor_adc_dma.c
Core/Impl/sensor_adc_dma.h
Core/Impl/flash_storage.c
Core/Impl/flash_storage.h
```

### 2.4 协议封装目录

```text
Core/Protocol
```

负责生成上位机可解析的应用层数据格式。

当前文件：

```text
Core/Protocol/protocol_text.c
Core/Protocol/protocol_text.h
```

### 2.5 配置持久化目录

```text
Core/Config
```

负责网络参数的默认值、校验、加载和保存。

当前文件：

```text
Core/Config/net_config.c
Core/Config/net_config.h
```

### 2.6 lwIP 接入层

```text
Middlewares/lwip/arch
Middlewares/lwip/lwip_app
```

主要文件：

```text
Middlewares/lwip/arch/lwip_comm.c
Middlewares/lwip/arch/lwip_comm.h
Middlewares/lwip/arch/lwipopts.h
Middlewares/lwip/lwip_app/lwip_demo.c
Middlewares/lwip/lwip_app/lwip_demo.h
```

其中 `lwip_comm.c` 负责 lwIP 和网卡初始化，`lwip_demo.c` 负责 RAW TCP Server。

## 3. 主程序执行流程

入口文件：

```text
User/main.c
```

核心流程：

```text
main()
    -> 初始化 MPU / Cache / HAL / 时钟 / 串口 / SDRAM / LED / LCD / KEY
    -> 检测 KEY0 是否长按，用于忘记 IP 时恢复默认网络配置
    -> device_service_init()
    -> 初始化 PCF8574
    -> 初始化内存池
    -> lwip_comm_init()
    -> comm_lwip_udp_discover_init()
    -> lwip_demo()
    -> while(1)
        -> lwip_periodic_handle()
        -> lwip_pkt_handle()
        -> 按键扫描
```

关键函数：

### `int main(void)`

文件：

```text
User/main.c
```

作用：

- 完成系统启动。
- 初始化业务层桥接关系。
- 初始化 lwIP。
- 启动 UDP 发现服务。
- 启动 TCP Server。
- 持续调度 lwIP 周期任务。

### `void lwip_test_ui(uint8_t mode)`

文件：

```text
User/main.c
```

作用：

- 在 LCD 上显示标题、IP、速率等状态。
- `mode` 的 bit0 用于显示标题区域。
- `mode` 的 bit1 用于显示网络状态区域。

## 4. UDP 设备发现功能

相关文件：

```text
Core/Impl/comm_lwip_udp_discover.c
Core/Impl/comm_lwip_udp_discover.h
Middlewares/lwip/arch/lwipopts.h
User/main.c
```

### 4.1 lwIP 配置

文件：

```text
Middlewares/lwip/arch/lwipopts.h
```

相关宏：

```c
#define LWIP_UDP                1
#define IP_SOF_BROADCAST        1
#define IP_SOF_BROADCAST_RECV   1
```

作用：

- `LWIP_UDP`：启用 lwIP UDP 功能。
- `IP_SOF_BROADCAST`：允许 UDP 发送广播。
- `IP_SOF_BROADCAST_RECV`：允许 UDP 接收广播。

### 4.2 `int comm_lwip_udp_discover_init(void)`

文件：

```text
Core/Impl/comm_lwip_udp_discover.c
```

作用：

- 创建 UDP PCB。
- 允许广播收发。
- 绑定本地端口 `9999`。
- 注册 UDP 接收回调函数。

关键流程：

```text
udp_new()
ip_set_option(..., SOF_BROADCAST)
udp_bind(..., IP_ADDR_ANY, 9999)
udp_recv(..., comm_lwip_udp_discover_on_receive, 0)
```

调用位置：

```text
User/main.c
```

调用时机：

```text
lwip_comm_init() 成功之后
lwip_demo() 启动 TCP Server 之前
```

### 4.3 `static void comm_lwip_udp_discover_on_receive(...)`

文件：

```text
Core/Impl/comm_lwip_udp_discover.c
```

作用：

- 接收上位机 UDP 广播。
- 读取 UDP payload。
- 判断口令是否为 `DISCOVER_DEVICE` 或 `FIND_STM32`。
- 构造回复数据。
- 释放接收 pbuf，避免内存泄漏。

支持的搜索口令：

```text
DISCOVER_DEVICE
FIND_STM32
```

### 4.4 `static uint16_t comm_lwip_udp_discover_format_reply(...)`

文件：

```text
Core/Impl/comm_lwip_udp_discover.c
```

作用：

生成 UDP 发现回复帧。

回复格式：

```text
ACK,DEVICE=STM32H743_RAW_TCP_ADC,IP=192.168.1.30,MASK=255.255.255.0,GW=192.168.1.1,MAC=B8:AE:1D:00:04:00,TCP=8080,UDP=9999\r\n
```

字段说明：

| 字段 | 说明 |
| --- | --- |
| `ACK` | 发现应答标识 |
| `DEVICE` | 设备名称 |
| `IP` | 单片机当前 IP |
| `MASK` | 子网掩码 |
| `GW` | 网关 |
| `MAC` | 单片机 MAC 地址 |
| `TCP` | 当前 TCP Server 端口 |
| `UDP` | UDP 发现端口 |

### 4.5 `static void comm_lwip_udp_discover_send_reply(...)`

文件：

```text
Core/Impl/comm_lwip_udp_discover.c
```

作用：

- 申请发送 pbuf。
- 拷贝回复字符串到 pbuf。
- 调用 `udp_sendto()` 发出。
- 释放发送 pbuf。

当前回复策略：

```text
先向发送方单播回复
再向 255.255.255.255 广播回复一份
```

这样做是为了兼容电脑和单片机处在同一二层网络但 IP 网段不一致的情况。

## 5. TCP Server 功能

相关文件：

```text
Middlewares/lwip/lwip_app/lwip_demo.c
Middlewares/lwip/lwip_app/lwip_demo.h
Core/Impl/comm_lwip_tcp.c
Core/Impl/comm_lwip_tcp.h
Core/App/device_service.c
```

### 5.1 `void lwip_demo(void)`

文件：

```text
Middlewares/lwip/lwip_app/lwip_demo.c
```

作用：

- 创建 TCP PCB。
- 绑定当前配置的 TCP 服务端口，默认 `8080`。
- 进入监听状态。
- 注册 TCP accept 回调。
- 在循环中调度业务层 `device_service_poll()`。

核心流程：

```text
tcp_new()
tcp_bind(..., g_lwipdev.tcp_port)
tcp_listen()
tcp_accept(..., lwip_tcp_server_accept)
while(...)
    device_service_poll()
    lwip_periodic_handle()
    lwip_pkt_handle()
```

### 5.2 `err_t lwip_tcp_server_accept(...)`

文件：

```text
Middlewares/lwip/lwip_app/lwip_demo.c
```

作用：

- 上位机 TCP Client 连接成功时被 lwIP 调用。
- 分配连接状态结构体。
- 注册该连接的 recv、sent、error、poll 回调。
- 记录当前客户端 PCB。
- 调用 `comm_lwip_tcp_set_client(newpcb)`，把 TCP 连接交给通信桥。

### 5.3 `err_t lwip_tcp_server_recv(...)`

文件：

```text
Middlewares/lwip/lwip_app/lwip_demo.c
```

作用：

- 接收 lwIP 解封装后的 TCP payload。
- 将收到的数据复制到接收缓冲区。
- 调用 `comm_lwip_tcp_on_receive()` 把 payload 交给业务层。
- 调用 `tcp_recved()` 告诉 lwIP 已经处理了多少数据。
- 释放 pbuf。

注意：

到这个函数时，Ethernet 头、IP 头、TCP 头都已经由 lwIP 处理完成。这里拿到的是应用层数据。

## 6. TCP 通信桥

相关文件：

```text
Core/Bridge/comm_bridge.h
Core/Impl/comm_lwip_tcp.c
Core/Impl/comm_lwip_tcp.h
```

### 6.1 `comm_bridge_t`

文件：

```text
Core/Bridge/comm_bridge.h
```

作用：

定义业务层需要的通信能力：

```c
int (*send)(const uint8_t *aData, uint16_t aLen);
int (*is_connected)(void);
```

这样业务层不需要知道底层是 TCP、UDP、串口还是其他通信方式。

### 6.2 `const comm_bridge_t *comm_lwip_tcp_get_bridge(void)`

文件：

```text
Core/Impl/comm_lwip_tcp.c
```

作用：

返回 lwIP TCP 通信桥实例。

调用位置：

```text
User/main.c
```

```c
device_service_init(comm_lwip_tcp_get_bridge(), sensor_adc_dma_get_bridge());
```

### 6.3 `void comm_lwip_tcp_set_client(struct tcp_pcb *aPcb)`

文件：

```text
Core/Impl/comm_lwip_tcp.c
```

作用：

保存当前已连接的 TCP 客户端 PCB。

调用场景：

- TCP accept 成功时传入 `newpcb`。
- TCP 连接关闭时传入 `0`。

### 6.4 `static int comm_lwip_tcp_send_impl(...)`

文件：

```text
Core/Impl/comm_lwip_tcp.c
```

作用：

- 检查当前 TCP 是否已连接。
- 检查 lwIP 发送缓存是否足够。
- 调用 `tcp_write()` 提交 payload。
- 调用 `tcp_output()` 立即推动发送。

重要说明：

`tcp_write()` 只接收应用层 payload。TCP 头、IP 头、以太网头由 lwIP 和网卡驱动继续向下封装。

### 6.5 `void comm_lwip_tcp_on_receive(...)`

文件：

```text
Core/Impl/comm_lwip_tcp.c
```

作用：

把 TCP 接收到的 payload 转交给业务层：

```c
device_service_on_rx(aData, aLen);
```

## 7. 业务服务层

相关文件：

```text
Core/App/device_service.c
Core/App/device_service.h
```

### 7.1 `void device_service_init(...)`

作用：

- 保存通信桥接口。
- 保存采集桥接口。
- 初始化 ADC 采集模块。

调用位置：

```text
User/main.c
```

### 7.2 `void device_service_poll(void)`

作用：

- 判断 TCP 是否已连接。
- 判断是否到达 ADC 上报周期。
- 读取 ADC 采样值。
- 调用 `protocol_text_format_adc()` 生成文本帧。
- 通过通信桥发送给上位机。

当前 ADC 上报周期：

```c
#define DEVICE_SERVICE_ADC_SEND_INTERVAL  2000U
```

即每 2 秒发送一次。

### 7.3 `void device_service_on_rx(...)`

作用：

- 接收上位机发来的 TCP payload。
- 如果是普通无换行文本，例如 `test`，则立即原样回显。
- 如果是命令，则按 `\n` 聚合成完整命令行。
- 调用 `device_service_handle_line()` 分发处理。

为什么要聚合命令：

TCP 是字节流，不保证上位机一次 `send()` 对应单片机一次完整 `recv()`。所以应用层必须自己处理粘包和拆包。

### 7.4 `static void device_service_handle_line(char *aLine)`

作用：

分发 TCP 命令。

当前支持：

```text
GET_NET
GET_NET?
SET_NET,IP=...,MASK=...,GW=...,PORT=...
RESET_NET
FACTORY_NET
SAVE_NET
REBOOT
PING
ECHO,...
```

### 7.5 `static void device_service_handle_get_net(void)`

作用：

返回当前网络配置。

返回格式：

```text
NET,IP=192.168.1.30,MASK=255.255.255.0,GW=192.168.1.1\r\n
```

### 7.6 `static void device_service_handle_set_net(const char *aLine)`

作用：

处理上位机修改 IP 命令。

流程：

```text
解析 IP
解析 MASK
解析 GW
校验参数
写入 Flash
更新 RAM 中的 g_lwipdev
回复 OK,NET_SAVED,REBOOTING
延时 300 ms
调用 NVIC_SystemReset()
```

命令示例：

```text
SET_NET,IP=192.168.1.50,MASK=255.255.255.0,GW=192.168.1.1,PORT=8081\r\n
```

### 7.7 `static void device_service_handle_reset_net(void)`

作用：

- 擦除 Flash 中保存的网络配置。
- 恢复默认 IP。
- 回复 `OK,NET_RESET,REBOOTING`。
- 自动软件复位。

### 7.8 `static void device_service_reboot_after_reply(void)`

作用：

先延时 300 ms，让 `tcp_write()` / `tcp_output()` 有时间把 OK 回复发出去，然后调用：

```c
NVIC_SystemReset();
```

## 8. ADC 采集桥

相关文件：

```text
Core/Bridge/sensor_bridge.h
Core/Impl/sensor_adc_dma.c
Core/Impl/sensor_adc_dma.h
Drivers/BSP/ADC/adc.c
Drivers/BSP/ADC/adc.h
```

### 8.1 `sensor_bridge_t`

文件：

```text
Core/Bridge/sensor_bridge.h
```

作用：

定义业务层需要的采集能力：

```c
void (*init)(void);
int (*read)(adc_sample_t *aSample);
```

### 8.2 `const sensor_bridge_t *sensor_adc_dma_get_bridge(void)`

文件：

```text
Core/Impl/sensor_adc_dma.c
```

作用：

返回 ADC DMA 采集桥实例。

### 8.3 `static void sensor_adc_dma_init_impl(void)`

作用：

- 调用 BSP ADC DMA 初始化。
- 启动 DMA 多通道采集。

核心调用：

```c
adc_nch_dma_init((uint32_t)&ADC1->DR, (uint32_t)s_adc_dma_buf);
adc_dma_enable(SENSOR_ADC_DMA_BUF_SIZE);
```

### 8.4 `static int sensor_adc_dma_read_impl(adc_sample_t *aSample)`

作用：

- 判断 DMA 是否完成一批采集。
- 失效 D-Cache，保证 CPU 读到 DMA 最新数据。
- 对每个通道求平均。
- 把原始 ADC 值换算成毫伏。
- 清除 DMA 完成标志。
- 启动下一轮 DMA。

当前通道对应关系：

| 下标 | 协议通道 | ADC 通道 | 引脚 |
| --- | --- | --- | --- |
| `raw[0]` / `mv[0]` | CH16 | ADC_CHANNEL_16 | PA0 |
| `raw[1]` / `mv[1]` | CH15 | ADC_CHANNEL_15 | PA3 |
| `raw[2]` / `mv[2]` | CH18 | ADC_CHANNEL_18 | PA4 |
| `raw[3]` / `mv[3]` | CH19 | ADC_CHANNEL_19 | PA5 |

## 9. ADC 文本协议

相关文件：

```text
Core/Protocol/protocol_text.c
Core/Protocol/protocol_text.h
```

### `int protocol_text_format_adc(...)`

作用：

把 ADC 采样结果转换成上位机可解析的文本帧。

输出格式：

```text
CH16=<raw>,V=<x.xxx>V;CH15=<raw>,V=<x.xxx>V;CH18=<raw>,V=<x.xxx>V;CH19=<raw>,V=<x.xxx>V\r\n
```

示例：

```text
CH16=116,V=0.005V;CH15=15835,V=0.797V;CH18=14695,V=0.739V;CH19=12220,V=0.615V\r\n
```

注意：

这一层只生成 TCP payload，不包含 TCP 头、IP 头、以太网头。

## 10. 网络配置持久化

相关文件：

```text
Core/Config/net_config.c
Core/Config/net_config.h
Core/Impl/flash_storage.c
Core/Impl/flash_storage.h
Middlewares/lwip/arch/lwip_comm.c
```

### 10.1 `void net_config_get_default(net_config_t *aConfig)`

作用：

提供默认静态网络参数：

```text
IP      = 192.168.1.30
MASK    = 255.255.255.0
GATEWAY = 192.168.1.1
TCP     = 8080
```

### 10.2 `int net_config_validate(const net_config_t *aConfig)`

作用：

校验 IP、掩码、网关是否合法。

### 10.3 `int net_config_load(net_config_t *aConfig)`

作用：

从 Flash 读取网络配置，并通过 `magic + version + crc` 判断是否有效。

### 10.4 `int net_config_save(const net_config_t *aConfig)`

作用：

把网络配置打包成记录，并写入 Flash。

记录字段：

```text
magic
version
ip
netmask
gateway
tcp_port
crc
reserved
```

### 10.5 `int net_config_clear(void)`

作用：

擦除 Flash 中保存的网络配置。下次启动时会回到默认 IP。

### 10.6 `void flash_storage_read(...)`

文件：

```text
Core/Impl/flash_storage.c
```

作用：

从固定 Flash 地址读取数据。

当前地址：

```c
#define FLASH_STORAGE_ADDRESS  0x081E0000UL
```

### 10.7 `int flash_storage_write(...)`

作用：

- 擦除固定 Flash 扇区。
- 按 Flash word 写入网络配置记录。

### 10.8 `int flash_storage_erase(void)`

作用：

擦除保存网络配置的 Flash 扇区。

## 11. lwIP 初始化与默认 IP 加载

相关文件：

```text
Middlewares/lwip/arch/lwip_comm.c
Middlewares/lwip/arch/lwip_comm.h
```

### `uint8_t lwip_comm_init(void)`

作用：

- 设置默认 IP/MASK/GW/MAC。
- 尝试从 Flash 加载已保存的网络配置。
- 初始化 lwIP 协议栈。
- 添加并启用网卡 netif。

### `void lwip_comm_default_ip_set(__lwip_dev *lwipx)`

作用：

- 设置默认 MAC 地址。
- 设置默认 IP。
- 如果 Flash 中存在合法网络配置，则覆盖默认 IP/MASK/GW。

## 12. 忘记 IP 的恢复逻辑

文件：

```text
User/main.c
```

逻辑：

```text
上电初始化 KEY 后
    如果 KEY0 被按下
        delay 2000 ms
        如果 KEY0 仍然被按下
            net_config_clear()
            LCD 显示 Net reset
            LCD 显示默认 IP
```

作用：

当用户忘记当前 IP，无法通过 UDP/TCP 正常管理设备时，可以通过硬件按键恢复默认 IP。

## 13. 上位机对接建议

### 13.1 设备发现

上位机发送 UDP 广播：

```text
目标 IP   : 255.255.255.255
目标端口 : 9999
payload  : DISCOVER_DEVICE
```

收到回复后解析：

```text
ACK,DEVICE=...,IP=...,MASK=...,GW=...,MAC=...,TCP=<当前TCP端口>,UDP=9999\r\n
```

解析出 `IP` 和 `TCP` 后再建立 TCP 连接。

### 13.2 TCP 收包

所有 TCP 文本帧都以 `\r\n` 结束。上位机必须维护接收缓存，按 `\r\n` 切分完整帧。

### 13.3 TCP 发命令

命令必须建议带 `\r\n`：

```text
GET_NET\r\n
PING\r\n
ECHO,hello\r\n
SET_NET,IP=192.168.1.50,MASK=255.255.255.0,GW=192.168.1.1,PORT=8081\r\n
RESET_NET\r\n
```

## 14. 模块设计原则

本项目当前采用分层和桥接思路：

```text
User/main.c
    -> 负责启动和调度

Core/App/device_service.c
    -> 负责业务逻辑

Core/Bridge/*.h
    -> 定义业务层依赖的抽象接口

Core/Impl/*.c
    -> 负责 TCP、UDP、ADC、Flash 的具体实现

Core/Protocol/*.c
    -> 负责应用层协议格式化

Middlewares/lwip/*
    -> 负责协议栈和 RAW API 接入
```

这样做的好处：

- TCP 业务和 UDP 发现分离。
- ADC 采集和业务发送分离。
- 网络配置保存和命令解析分离。
- 后续如果替换通信方式或协议格式，影响范围更小。

## 15. 当前关键端口和协议汇总

| 功能 | 协议 | 端口 | 数据 |
| --- | --- | --- | --- |
| 设备发现 | UDP | 9999 | `DISCOVER_DEVICE` |
| 设备发现兼容口令 | UDP | 9999 | `FIND_STM32` |
| 业务连接 | TCP | 当前TCP端口 | ADC 数据、网络配置命令 |
| ADC 上报 | TCP | 当前TCP端口 | `CH16=...;CH15=...;CH18=...;CH19=...\r\n` |
| 查询网络 | TCP | 当前TCP端口 | `GET_NET\r\n` |
| 修改网络 | TCP | 当前TCP端口 | `SET_NET,IP=...,MASK=...,GW=...,PORT=...\r\n` |
| 恢复网络 | TCP | 当前TCP端口 | `RESET_NET\r\n` |
