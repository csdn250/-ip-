# 网络配置、UDP 发现与 TCP 通信说明

本文档说明当前工程中设备发现、TCP 建连、ADC 数据上报、上位机修改 IP 的完整流程。

## 1. 总体通信流程

当前工程采用“UDP 发现 + TCP 业务通信”的组合：

```text
上位机 UDP 广播搜索
    -> 单片机 UDP 回复自身信息
    -> 上位机解析出单片机真实 IP
    -> 上位机发起 TCP 连接
    -> TCP 连接建立后进行 ADC 数据接收和网络配置命令交互
```

默认网络参数：

```text
IP       = 192.168.1.30
MASK     = 255.255.255.0
GATEWAY  = 192.168.1.1
TCP PORT = 8080
UDP PORT = 9999
```

## 2. UDP 设备发现

### 2.1 上位机搜索

上位机创建 UDP socket，打开广播发送权限，然后向 `255.255.255.255:9999` 发送：

```text
DISCOVER_DEVICE
```

当前固件也兼容旧搜索口令：

```text
FIND_STM32
```

### 2.2 单片机回复

单片机后台的 UDP Server 一直监听 9999 端口。收到正确口令后，回复：

```text
ACK,DEVICE=STM32H743_RAW_TCP_ADC,IP=192.168.1.30,MASK=255.255.255.0,GW=192.168.1.1,MAC=B8:AE:1D:00:04:00,TCP=8080,UDP=9999\r\n
```

回复方式：

- 先向发送方 IP 和端口单播回复。
- 再向 `255.255.255.255` 广播回复一份。

这样做是为了兼容两类场景：

- 电脑和单片机在同一 IP 网段，单播回复即可正常到达。
- 电脑和单片机处于同一二层网络但 IP 网段不同，广播回复更容易被上位机收到。

### 2.3 重要网络限制

UDP 广播不能穿过普通路由器的三层隔离。也就是说，电脑和单片机必须处在同一个二层局域网中，例如接在同一个交换机或同一个家用路由器 LAN 口下。

如果电脑 IP 是 `10.0.0.5`，单片机 IP 是 `192.168.1.30`，UDP 广播可能可以发现设备，但 TCP 连接仍然可能失败。上位机发现设备后，应提示用户把电脑网卡切换到 `192.168.1.x`，或给电脑网卡添加一个辅助 IP，再发起 TCP 连接。

## 3. TCP 业务通信

单片机作为 TCP Server，监听：

```text
<单片机真实 IP>:8080
```

上位机通过 UDP 发现得到真实 IP 后，再连接 TCP 端口 8080。

TCP 连接建立后，单片机会每 2 秒主动发送一帧 ADC 数据：

```text
CH16=<raw>,V=<x.xxx>V;CH15=<raw>,V=<x.xxx>V;CH18=<raw>,V=<x.xxx>V;CH19=<raw>,V=<x.xxx>V\r\n
```

示例：

```text
CH16=116,V=0.005V;CH15=15835,V=0.797V;CH18=14695,V=0.739V;CH19=12220,V=0.615V\r\n
```

TCP 是字节流，上位机必须按 `\r\n` 做粘包和拆包处理：

```text
rx_buffer += recv_data

while rx_buffer contains "\r\n":
    frame = bytes before "\r\n"
    remove frame + "\r\n" from rx_buffer
    parse frame
```

## 4. TCP 网络配置命令

### 4.1 查询当前网络配置

发送：

```text
GET_NET\r\n
```

返回：

```text
NET,IP=192.168.1.30,MASK=255.255.255.0,GW=192.168.1.1\r\n
```

### 4.2 修改 IP

发送：

```text
SET_NET,IP=192.168.1.50,MASK=255.255.255.0,GW=192.168.1.1\r\n
```

写入成功后返回：

```text
OK,NET_SAVED,REBOOTING\r\n
```

随后单片机会自动软件复位。复位完成后，上位机需要重新 UDP 搜索，或直接使用新地址连接：

```text
192.168.1.50:8080
```

### 4.3 恢复默认 IP

如果当前还能通过 TCP 连接设备，发送：

```text
RESET_NET\r\n
```

返回：

```text
OK,NET_RESET,REBOOTING\r\n
```

随后单片机会自动软件复位，复位后恢复默认 IP：

```text
192.168.1.30
```

如果用户已经忘记当前 IP，无法 TCP 连接设备，则使用硬件恢复入口：

```text
上电时持续按住 KEY0 约 2 秒
```

单片机会擦除 Flash 中保存的网络配置，并回到默认静态 IP。

## 5. 当前支持的命令

| 类型 | 命令 | 端口 | 作用 |
| --- | --- | --- | --- |
| UDP | `DISCOVER_DEVICE` | 9999 | 广播搜索设备 |
| UDP | `FIND_STM32` | 9999 | 兼容旧搜索口令 |
| TCP | `GET_NET` | 8080 | 查询当前 IP/MASK/GW |
| TCP | `SET_NET` | 8080 | 修改并保存网络配置，成功后自动重启 |
| TCP | `RESET_NET` | 8080 | 清除保存配置，恢复默认 IP，成功后自动重启 |
| TCP | `REBOOT` | 8080 | 手动软件复位 |
| TCP | `PING` | 8080 | 通信测试 |
| TCP | `ECHO` | 8080 | 显式回显测试 |

## 6. 代码实现路径

### 6.1 UDP 发现模块

文件：

```text
Core/Impl/comm_lwip_udp_discover.c
Core/Impl/comm_lwip_udp_discover.h
```

入口函数：

```c
int comm_lwip_udp_discover_init(void)
```

执行流程：

```text
udp_new()
设置 SOF_BROADCAST
udp_bind(IP_ADDR_ANY, 9999)
udp_recv(..., comm_lwip_udp_discover_on_receive, ...)
收到 DISCOVER_DEVICE 或 FIND_STM32
构造 ACK,DEVICE=...,IP=...,MAC=...,TCP=8080,UDP=9999
udp_sendto() 单播回复
udp_sendto() 广播回复
pbuf_free() 释放接收缓冲区
```

### 6.2 UDP 发现启动位置

文件：

```text
User/main.c
```

位置：

```text
lwip_comm_init() 成功之后
lwip_demo() 创建 TCP Server 之前
```

这样可以保证 netif、IP、MAC 已经准备好，UDP 回复中的地址信息是有效的。

### 6.3 TCP 命令处理

文件：

```text
Core/App/device_service.c
```

入口函数：

```c
void device_service_on_rx(const uint8_t *aData, uint16_t aLen)
```

作用：

- 接收 lwIP 解封装后的 TCP payload。
- 兼容无换行普通文本回显测试。
- 对命令数据按 `\n` 聚合为应用层命令行。
- 调用 `device_service_handle_line()` 分发命令。

### 6.4 Flash 保存逻辑

文件：

```text
Core/Config/net_config.c
Core/Impl/flash_storage.c
```

保存记录包含：

```text
magic
version
ip
netmask
gateway
flags
crc
reserved
```

使用 `magic + version + crc` 判断 Flash 中的配置是否有效。配置有效时使用保存配置，配置无效时使用默认静态 IP。

## 7. 为什么这样设计

UDP 广播适合发现设备，但不适合做可靠业务数据传输；TCP 连接稳定后更适合持续 ADC 上报和配置命令交互。因此本项目将“发现”和“业务通信”拆成两个独立模块。

修改 IP 只改 RAM 变量会掉电丢失，所以必须写入 Flash。修改成功后自动复位，是为了让 lwIP、netif 和 TCP Server 全部按新地址重新初始化。

KEY0 恢复默认 IP 是最后的现场救援手段，不依赖网络，也不依赖上位机知道当前地址。
