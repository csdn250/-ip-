# 网络配置功能说明文档

本文档说明当前工程中“上位机修改单片机 IP 地址”的功能、使用方法、代码实现路径，以及为什么采用这种设计。

## 1. 功能目标

当前工程中，单片机作为 TCP Server 工作，上位机作为 TCP Client 连接单片机。

默认网络参数：

```text
IP      = 192.168.1.30
MASK    = 255.255.255.0
GATEWAY = 192.168.1.1
PORT    = 8080
```

新增网络配置功能后，上位机可以通过 TCP 命令完成：

- 查询当前单片机 IP 地址
- 修改单片机 IP、子网掩码、网关
- 将修改后的配置写入内部 Flash
- 重启后使用新的 IP 地址通信
- 用户忘记 IP 时，通过按键恢复默认 IP

## 2. 为什么这样设计

### 2.1 修改 IP 后必须保存到 Flash

单片机运行时的变量都在 RAM 中，掉电后会丢失。如果只修改 `g_lwipdev.ip[]` 这类 RAM 变量，设备断电重启后还是会回到默认 IP。

所以本工程把网络配置写入 STM32H743 内部 Flash。这样：

```text
上位机发送 SET_NET
  ↓
单片机解析 IP/MASK/GW
  ↓
校验参数是否合法
  ↓
擦除 Flash 配置区
  ↓
写入新的网络配置
  ↓
下次上电读取 Flash 配置
```

只要 Flash 中配置合法，单片机下次上电就会使用修改后的 IP。

### 2.2 修改后要求重启生效

TCP 连接建立后，当前连接是基于旧 IP 建立的。如果修改 IP 后立即切换网卡 IP，当前 TCP 连接可能马上断开，上位机也不一定能收到确认消息。

所以当前设计是：

```text
SET_NET 写入 Flash 成功
  ↓
单片机回复 OK,NET_SAVED,REBOOT_REQUIRED
  ↓
上位机确认收到成功回复
  ↓
上位机发送 REBOOT
  ↓
单片机重启
  ↓
单片机使用新 IP 初始化 lwIP
```

这样能保证上位机先拿到“写入成功”的明确反馈，再切换到新 IP 连接。

### 2.3 用户忘记 IP 时必须有恢复入口

如果用户把 IP 改成了一个自己忘记的地址，普通 TCP 连接就找不到设备了。

因此工程保留了一个硬件恢复方式：

```text
上电时按住 KEY0 约 2 秒
  ↓
单片机擦除 Flash 中保存的网络配置
  ↓
恢复默认 IP: 192.168.1.30
```

这个设计不依赖网络，也不依赖上位机是否知道当前 IP，适合现场救援。

## 3. 上位机如何操作

### 3.1 连接单片机

单片机默认作为 TCP Server：

```text
IP   : 192.168.1.30
PORT : 8080
```

电脑需要与单片机在同一网段，例如电脑可以设置：

```text
IP      = 192.168.1.100
MASK    = 255.255.255.0
GATEWAY = 192.168.1.1
```

然后使用网络调试助手或上位机程序连接：

```text
192.168.1.30:8080
```

### 3.2 查询当前 IP

发送：

```text
GET_NET\r\n
```

单片机返回：

```text
NET,IP=192.168.1.30,MASK=255.255.255.0,GW=192.168.1.1\r\n
```

上位机解析时，以 `\r\n` 作为一帧结束符。

### 3.3 修改 IP

例如要把单片机 IP 修改为 `192.168.1.50`，发送：

```text
SET_NET,IP=192.168.1.50,MASK=255.255.255.0,GW=192.168.1.1\r\n
```

写入成功后，单片机返回：

```text
OK,NET_SAVED,REBOOT_REQUIRED\r\n
```

这个回复表示：

- 命令格式正确
- IP/MASK/GW 校验通过
- 配置已经写入内部 Flash
- 需要重启后新 IP 才会真正生效

### 3.4 重启设备

发送：

```text
REBOOT\r\n
```

单片机返回：

```text
OK,REBOOTING\r\n
```

随后单片机软件复位。复位后，需要用新 IP 连接：

```text
192.168.1.50:8080
```

### 3.5 恢复默认 IP

如果当前还能连接设备，可以发送：

```text
RESET_NET\r\n
```

单片机返回：

```text
OK,NET_RESET,REBOOT_REQUIRED\r\n
```

然后发送：

```text
REBOOT\r\n
```

重启后恢复默认 IP：

```text
192.168.1.30
```

如果已经忘记 IP，无法通过 TCP 连接设备，则使用硬件恢复方式：

```text
上电时按住 KEY0 约 2 秒
```

单片机会擦除 Flash 中保存的网络配置，并恢复默认 IP。

## 4. 当前支持的 TCP 命令

| 命令 | 作用 | 示例 |
|---|---|---|
| `GET_NET` | 查询当前 IP/MASK/GW | `GET_NET\r\n` |
| `SET_NET` | 修改并保存网络配置 | `SET_NET,IP=192.168.1.50,MASK=255.255.255.0,GW=192.168.1.1\r\n` |
| `RESET_NET` | 清除保存配置，恢复默认 IP | `RESET_NET\r\n` |
| `REBOOT` | 软件复位 | `REBOOT\r\n` |
| `PING` | 通信测试 | `PING\r\n` |
| `ECHO` | 显式回显测试 | `ECHO,hello\r\n` |

返回示例：

```text
PONG\r\n
hello\r\n
OK,NET_SAVED,REBOOT_REQUIRED\r\n
ERR,BAD_NET_FORMAT\r\n
ERR,BAD_NET_VALUE\r\n
ERR,FLASH_WRITE_FAILED\r\n
ERR,UNKNOWN_CMD\r\n
```

## 5. 上位机解析建议

TCP 是字节流，一次 `recv()` 不一定等于一条完整消息。

上位机建议这样处理：

```text
rx_buffer += recv_data

while rx_buffer contains "\r\n":
    frame = bytes before "\r\n"
    remove frame + "\r\n" from rx_buffer
    parse frame
```

例如收到：

```text
OK,NET_SAVED,REBOOT_REQUIRED\r\n
```

上位机可以判断：

```text
frame starts with "OK,NET_SAVED"
  -> 提示用户保存成功
  -> 提示需要重启
```

## 6. 代码实现路径

### 6.1 TCP 接收入口

文件：

```text
Core/App/device_service.c
```

函数：

```c
void device_service_on_rx(const uint8_t *data, uint16_t len)
```

作用：

- 接收 lwIP 传上来的 TCP payload
- 按 `\n` 聚合成完整命令行
- 调用 `device_service_handle_line()` 分发命令

关键逻辑：

```c
if (data[i] == '\n')
{
    s_rx_line[s_rx_line_len] = '\0';
    device_service_handle_line(s_rx_line);
    s_rx_line_len = 0;
}
```

### 6.2 命令分发

文件：

```text
Core/App/device_service.c
```

函数：

```c
static void device_service_handle_line(char *line)
```

作用：

```text
GET_NET      -> device_service_handle_get_net()
SET_NET,...  -> device_service_handle_set_net()
RESET_NET    -> device_service_handle_reset_net()
REBOOT       -> NVIC_SystemReset()
PING         -> 返回 PONG
ECHO,...     -> 返回指定内容
```

### 6.3 修改 IP 的业务逻辑

文件：

```text
Core/App/device_service.c
```

函数：

```c
static void device_service_handle_set_net(const char *line)
```

执行步骤：

```text
解析 IP 字段
解析 MASK 字段
解析 GW 字段
校验网络参数
调用 net_config_save()
更新 RAM 中的 g_lwipdev
回复 OK,NET_SAVED,REBOOT_REQUIRED
```

关键代码：

```c
if (net_config_save(&config) != 0)
{
    device_service_send_text("ERR,FLASH_WRITE_FAILED\r\n");
    return;
}

device_service_apply_config_to_ram(&config);
device_service_send_text("OK,NET_SAVED,REBOOT_REQUIRED\r\n");
```

## 7. Flash 保存格式

文件：

```text
Core/Config/net_config.c
```

保存结构：

```c
typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t flags;
    uint32_t crc;
    uint32_t reserved;
} net_config_record_t;
```

字段说明：

| 字段 | 作用 |
|---|---|
| `magic` | 判断 Flash 中是否是本工程网络配置 |
| `version` | 配置结构版本，方便后续升级 |
| `ip` | 保存 IP 地址 |
| `netmask` | 保存子网掩码 |
| `gateway` | 保存默认网关 |
| `flags` | 预留字段 |
| `crc` | 校验配置是否完整可靠 |
| `reserved` | 预留字段 |

读取时会校验：

```text
magic 是否正确
version 是否正确
crc 是否正确
IP/MASK/GW 是否合法
```

任意一项失败，就认为 Flash 配置无效，自动使用默认 IP。

## 8. Flash 擦写实现

文件：

```text
Core/Impl/flash_storage.c
```

当前配置保存地址：

```c
#define FLASH_STORAGE_ADDRESS 0x081E0000UL
#define FLASH_STORAGE_BANK    FLASH_BANK_2
#define FLASH_STORAGE_SECTOR  FLASH_SECTOR_7
```

写入流程：

```text
准备 32 字节 Flash Word 数据
  ↓
HAL_FLASH_Unlock()
  ↓
擦除配置扇区 HAL_FLASHEx_Erase()
  ↓
HAL_FLASH_Program()
  ↓
HAL_FLASH_Lock()
```

关键代码：

```c
status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD,
                           FLASH_STORAGE_ADDRESS,
                           (uint32_t)flash_word);
```

## 9. 上电加载配置流程

文件：

```text
Middlewares/lwip/arch/lwip_comm.c
```

函数：

```c
void lwip_comm_default_ip_set(__lwip_dev *lwipx)
```

流程：

```text
先写入默认 IP: 192.168.1.30
  ↓
调用 net_config_load()
  ↓
如果 Flash 中有合法配置
  ↓
用 Flash 配置覆盖默认 IP/MASK/GW
```

因此下次上电会自动使用之前保存过的 IP。

## 10. 忘记 IP 的恢复流程

文件：

```text
User/main.c
```

启动阶段会检测 KEY0：

```c
if (KEY0 == 0)
{
    delay_ms(2000);
    if (KEY0 == 0)
    {
        net_config_clear();
    }
}
```

也就是说：

```text
上电按住 KEY0 约 2 秒
  ↓
擦除 Flash 网络配置
  ↓
恢复默认 IP
```

这个功能用于现场找回设备。

## 11. 推荐测试步骤

1. 烧录程序，上电。
2. 电脑连接默认地址：

```text
192.168.1.30:8080
```

3. 查询当前 IP：

```text
GET_NET\r\n
```

4. 修改 IP：

```text
SET_NET,IP=192.168.1.50,MASK=255.255.255.0,GW=192.168.1.1\r\n
```

5. 看到：

```text
OK,NET_SAVED,REBOOT_REQUIRED\r\n
```

6. 重启：

```text
REBOOT\r\n
```

7. 上位机重新连接：

```text
192.168.1.50:8080
```

8. 再次查询：

```text
GET_NET\r\n
```

9. 如果想恢复默认：

```text
RESET_NET\r\n
REBOOT\r\n
```

或者上电按住 KEY0 约 2 秒。

## 12. 注意事项

- 所有命令建议以 `\r\n` 结尾。
- `SET_NET` 成功后不会立即切换当前 TCP 连接的 IP，需要重启后生效。
- 如果电脑改连新 IP 失败，先确认电脑和单片机是否在同一网段。
- 如果忘记 IP，使用 KEY0 上电恢复默认配置。
- 当前已关闭 DHCP，工程固定使用静态 IP。
- ADC 数据仍会周期主动发送，目前周期为 2 秒一次。

