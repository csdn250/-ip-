# MCU TCP Protocol for PC Application

This document describes the application data carried by TCP. The PC application does not need to parse Ethernet/IP/TCP headers. The operating system TCP stack removes those headers and returns only the TCP payload to `recv()`.

## Transport

- MCU role: TCP Server
- Default port: 8080
- Encoding: ASCII text
- Frame delimiter: `\r\n`
- Delimiter bytes: `0x0D 0x0A`

## ADC Upload Frame

The MCU periodically sends one text frame:

```text
CH16=<raw>,V=<voltage>V;CH15=<raw>,V=<voltage>V;CH18=<raw>,V=<voltage>V;CH19=<raw>,V=<voltage>V\r\n
```

Example:

```text
CH16=12345,V=0.621V;CH15=23456,V=1.180V;CH18=34567,V=1.739V;CH19=45678,V=2.298V\r\n
```

## Field Meaning

| Field | Meaning |
|---|---|
| `CH16` | ADC channel 16, PA0 |
| `CH15` | ADC channel 15, PA3 |
| `CH18` | ADC channel 18, PA4 |
| `CH19` | ADC channel 19, PA5 |
| `<raw>` | Averaged ADC raw value, decimal integer |
| `<voltage>` | Converted voltage, decimal text with 3 fractional digits |
| `;` | Channel separator |
| `,` | Raw value and voltage separator |
| `\r\n` | End of one application frame |

## PC Parsing Rule

TCP is a byte stream. One `recv()` call is not guaranteed to equal one application frame.

The PC program should:

1. Append every `recv()` result to a receive buffer.
2. Search for `\r\n`.
3. If found, take bytes before `\r\n` as one complete frame.
4. Remove this frame and delimiter from the receive buffer.
5. Split the frame by `;` to get channel fields.
6. Parse every channel field as `CHxx=<raw>,V=<voltage>V`.
7. Continue parsing if more complete frames remain in the buffer.

## Pseudocode

```text
rx_buffer += recv_data

while rx_buffer contains "\r\n":
    frame = bytes before "\r\n"
    remove frame + "\r\n" from rx_buffer

    channel_items = split(frame, ";")
    for item in channel_items:
        raw_part, voltage_part = split(item, ",")
        channel, raw = split(raw_part, "=")
        _, voltage_text = split(voltage_part, "=")
        voltage = remove trailing "V" from voltage_text
```

## Current Receive Behavior

At this stage, commands from the PC are echoed back by the MCU. This keeps communication verification simple. Later commands can be added on the same `\r\n` framed text protocol, for example:

```text
GET_NET\r\n
SET_NET,IP=192.168.1.50,MASK=255.255.255.0,GW=192.168.1.1,DHCP=0\r\n
SAVE_NET\r\n
REBOOT\r\n
```

## Related Firmware Files

- `Core/Protocol/protocol_text.c`: formats ADC text frames.
- `Core/App/device_service.c`: business logic and receive entry.
- `Core/Impl/comm_lwip_tcp.c`: TCP bridge implementation.
- `Middlewares/lwip/lwip_app/lwip_demo.c`: lwIP RAW TCP server callback adapter.
