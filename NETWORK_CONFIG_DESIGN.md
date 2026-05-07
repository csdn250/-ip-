# 网络配置与上位机命令说明

本文档说明当前工程中“上位机修改单片机 IP 地址”的功能、操作命令、代码路径和设计原因。

## 1. 当前网络角色

单片机作为 TCP Server 工作，上位机作为 TCP Client 连接单片机。

默认网络参数：

```text
IP      = 192.168.1.30
MASK    = 255.255.255.0
GATEWAY = 192.168.1.1
PORT    = 8080
```

网络参数保存在 STM32H743 片内 Flash 中。修改成功后单片机会自动软件复位，下次启动时使用 Flash 中的新配置。

## 2. 上位机如何操作

### 2.1 查询当前 IP

发送：

```text
GET_NET\r\n
```

返回示例：

```text
NET,IP=192.168.1.30,MASK=255.255.255.0,GW=192.168.1.1\r\n
```

### 2.2 修改 IP

例如把单片机 IP 修改为 `192.168.1.50`，发送：

```text
SET_NET,IP=192.168.1.50,MASK=255.255.255.0,GW=192.168.1.1\r\n
```

写入成功后返回：

```text
OK,NET_SAVED,REBOOTING\r\n
```

随后单片机会自动软件复位。复位完成后，上位机需要使用新地址重新连接：

```text
192.168.1.50:8080
```

### 2.3 恢复默认 IP

如果当前还能连接设备，发送：

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

如果用户已经忘记当前 IP，无法通过 TCP 连接设备，则使用硬件恢复入口：

```text
上电时持续按住 KEY0 约 2 秒
```

单片机会擦除 Flash 中保存的网络配置，并回到默认静态 IP `192.168.1.30`。

## 3. 当前支持的 TCP 命令

| 命令 | 作用 | 示例 |
| --- | --- | --- |
| `GET_NET` | 查询当前 IP/MASK/GW | `GET_NET\r\n` |
| `SET_NET` | 修改并保存网络配置，成功后自动重启 | `SET_NET,IP=192.168.1.50,MASK=255.255.255.0,GW=192.168.1.1\r\n` |
| `RESET_NET` | 清除保存配置，恢复默认 IP，成功后自动重启 | `RESET_NET\r\n` |
| `REBOOT` | 手动软件复位 | `REBOOT\r\n` |
| `PING` | 通信测试 | `PING\r\n` |
| `ECHO` | 显式回显测试 | `ECHO,hello\r\n` |

常见返回：

```text
PONG\r\n
hello\r\n
OK,NET_SAVED,REBOOTING\r\n
OK,NET_RESET,REBOOTING\r\n
ERR,BAD_NET_FORMAT\r\n
ERR,BAD_NET_VALUE\r\n
ERR,FLASH_WRITE_FAILED\r\n
ERR,FLASH_ERASE_FAILED\r\n
ERR,UNKNOWN_CMD\r\n
```

## 4. 上位机解析建议

TCP 是字节流，一次 `recv()` 不一定等于一条完整消息。当前应用层协议使用 `\r\n` 作为一帧结束符，上位机应按结束符拆包：

```text
rx_buffer += recv_data

while rx_buffer contains "\r\n":
    frame = bytes before "\r\n"
    remove frame + "\r\n" from rx_buffer
    parse frame
```

ADC 主动上报帧格式：

```text
CH16=<raw>,V=<x.xxx>V;CH15=<raw>,V=<x.xxx>V;CH18=<raw>,V=<x.xxx>V;CH19=<raw>,V=<x.xxx>V\r\n
```

示例：

```text
CH16=116,V=0.005V;CH15=15835,V=0.797V;CH18=14695,V=0.739V;CH19=12220,V=0.615V\r\n
```

## 5. 代码实现路径

### 5.1 命令接收入口

文件：

```text
Core/App/device_service.c
```

函数：

```c
void device_service_on_rx(const uint8_t *aData, uint16_t aLen)
```

作用：

- 接收 lwIP 解封装后的 TCP payload。
- 兼容无换行普通文本回显测试。
- 对命令数据按 `\n` 聚合为应用层命令行。
- 调用 `device_service_handle_line()` 分发命令。

### 5.2 修改 IP 的业务逻辑

文件：

```text
Core/App/device_service.c
```

函数：

```c
static void device_service_handle_set_net(const char *aLine)
```

执行步骤：

```text
解析 IP/MASK/GW 字段
校验网络参数
调用 net_config_save() 写入 Flash
更新 RAM 中的 g_lwipdev
返回 OK,NET_SAVED,REBOOTING
延时 300 ms
调用 NVIC_SystemReset() 自动复位
```

### 5.3 Flash 保存逻辑

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

使用 `magic + version + crc` 判断 Flash 中的配置是否有效。配置有效时使用保存配置；配置无效时使用默认静态 IP。

### 5.4 忘记 IP 的恢复入口

文件：

```text
User/main.c
```

逻辑：

```text
上电初始化按键后检测 KEY0
如果 KEY0 持续按下约 2 秒
    调用 net_config_clear()
    LCD 显示 Net reset 和默认 IP
    后续网络初始化使用默认静态 IP
```

## 6. 为什么这样设计

修改 `g_lwipdev.ip[]` 只能改变 RAM 变量，掉电会丢失；写入 Flash 后，下次上电才能继续使用修改后的 IP。

修改 IP 后自动复位，是为了让 lwIP、网卡 netif 和 TCP Server 全部按照新地址重新初始化。这样比运行中强行切换 IP 更清晰，也更容易让上位机按“收到 OK 后等待重连”的流程实现。

保留 KEY0 恢复默认 IP，是为了处理用户忘记 IP 的现场问题。这个恢复方式不依赖网络，也不依赖上位机知道当前地址。
