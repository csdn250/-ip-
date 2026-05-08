#!/usr/bin/env python3
"""
STM32H743 RAW TCP ADC receiver and verifier.

This script is intentionally dependency-free. It discovers the board by UDP,
connects to the TCP server, parses fixed 1460-byte ADC frames, and verifies
sequence continuity plus checksum correctness.
"""

import argparse
import socket
import struct
import time


DISCOVER_CMD = b"DISCOVER_DEVICE"
ADC_MAGIC = b"ADC1"
ADC_HEADER_SIZE = 20
ADC_SAMPLE_COUNT = 720
ADC_FRAME_SIZE = ADC_HEADER_SIZE + ADC_SAMPLE_COUNT * 2


def parse_args():
    parser = argparse.ArgumentParser(description="Verify STM32 TCP ADC stream.")
    parser.add_argument("--ip", help="Device IP. If omitted, UDP discovery is used.")
    parser.add_argument("--tcp-port", type=int, default=8080, help="TCP server port.")
    parser.add_argument("--udp-port", type=int, default=9999, help="UDP discovery port.")
    parser.add_argument("--local-port", type=int, default=60000, help="Local UDP port.")
    parser.add_argument("--broadcast", default="255.255.255.255", help="Broadcast address.")
    parser.add_argument("--seconds", type=float, default=10.0, help="Capture duration.")
    parser.add_argument("--timeout", type=float, default=3.0, help="Socket timeout.")
    parser.add_argument("--save-bin", help="Optional path to save raw TCP payload bytes.")
    return parser.parse_args()


def discover_device(args):
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    udp.settimeout(args.timeout)
    udp.bind(("", args.local_port))

    try:
        print(f"DISCOVER: send {DISCOVER_CMD.decode()} to {args.broadcast}:{args.udp_port}")
        udp.sendto(DISCOVER_CMD, (args.broadcast, args.udp_port))
        data, remote = udp.recvfrom(512)
    finally:
        udp.close()

    text = data.decode("ascii", errors="replace")
    device_ip = remote[0]

    for item in text.split(","):
        if item.startswith("IP="):
            device_ip = item[3:]

    print(f"DISCOVER: recv from {remote[0]}:{remote[1]}")
    print(f"DISCOVER: {text}")
    return device_ip


def recv_exact(sock, size):
    chunks = []
    remain = size

    while remain > 0:
        data = sock.recv(remain)
        if not data:
            raise ConnectionError("TCP connection closed")

        chunks.append(data)
        remain -= len(data)

    return b"".join(chunks)


def read_frame(sock, buffer):
    while True:
        index = buffer.find(ADC_MAGIC)

        if index < 0:
            buffer[:] = buffer[-3:]
            buffer.extend(sock.recv(4096))
            continue

        if index > 0:
            del buffer[:index]

        while len(buffer) < ADC_FRAME_SIZE:
            data = sock.recv(ADC_FRAME_SIZE - len(buffer))
            if not data:
                raise ConnectionError("TCP connection closed")

            buffer.extend(data)

        frame = bytes(buffer[:ADC_FRAME_SIZE])
        del buffer[:ADC_FRAME_SIZE]
        return frame


def parse_frame(frame):
    magic = frame[0:4]
    seq, sample_count, channel, sample_rate, checksum = struct.unpack_from("<IHHII", frame, 4)
    samples = struct.unpack_from("<720H", frame, ADC_HEADER_SIZE)
    calc_checksum = sum(samples) & 0xFFFFFFFF
    return magic, seq, sample_count, channel, sample_rate, checksum, calc_checksum, samples


def main():
    args = parse_args()

    try:
        device_ip = args.ip or discover_device(args)
    except socket.timeout:
        print("")
        print("DISCOVER_ERR: UDP discovery timed out.")
        print("Try one of these commands:")
        print("  python .\\Tools\\pc_adc_verify.py --ip 192.168.1.50 --tcp-port 8081 --seconds 10")
        print("  python .\\Tools\\pc_adc_verify.py --broadcast 192.168.1.255 --seconds 10")
        print("")
        print("Also check that the board firmware has UDP discovery enabled and the PC is on the Ethernet adapter.")
        return

    end_time = time.time() + args.seconds
    frame_count = 0
    sample_count_total = 0
    checksum_error_count = 0
    seq_lost_count = 0
    prev_seq = None
    raw_file = None

    if args.save_bin:
        raw_file = open(args.save_bin, "wb")

    try:
        with socket.create_connection((device_ip, args.tcp_port), timeout=args.timeout) as tcp:
            tcp.settimeout(args.timeout)
            buffer = bytearray()
            start = time.time()

            print(f"TCP: connected to {device_ip}:{args.tcp_port}")
            print("TCP: receiving ADC frames...")

            while time.time() < end_time:
                frame = read_frame(tcp, buffer)

                if raw_file:
                    raw_file.write(frame)

                magic, seq, sample_count, channel, sample_rate, checksum, calc_checksum, samples = parse_frame(frame)

                if magic != ADC_MAGIC or sample_count != ADC_SAMPLE_COUNT:
                    print(f"FRAME_ERR: magic={magic!r}, sample_count={sample_count}")
                    continue

                if checksum != calc_checksum:
                    checksum_error_count += 1

                if prev_seq is not None:
                    expected = (prev_seq + 1) & 0xFFFFFFFF
                    if seq != expected:
                        if seq > expected:
                            seq_lost_count += seq - expected
                        else:
                            seq_lost_count += 1

                prev_seq = seq
                frame_count += 1
                sample_count_total += sample_count

                if frame_count == 1 or frame_count % 200 == 0:
                    print(
                        f"seq={seq} ch={channel} fs={sample_rate}Hz "
                        f"raw0={samples[0]} checksum={'OK' if checksum == calc_checksum else 'BAD'}"
                    )

            elapsed = time.time() - start
    finally:
        if raw_file:
            raw_file.close()

    payload_mbps = (frame_count * ADC_FRAME_SIZE * 8.0) / elapsed / 1_000_000.0
    adc_ksps = sample_count_total / elapsed / 1000.0

    print("")
    print("RESULT")
    print(f"frames          : {frame_count}")
    print(f"samples         : {sample_count_total}")
    print(f"elapsed         : {elapsed:.3f} s")
    print(f"adc rate actual : {adc_ksps:.1f} KS/s")
    print(f"tcp payload     : {payload_mbps:.2f} Mbps")
    print(f"seq lost        : {seq_lost_count}")
    print(f"checksum errors : {checksum_error_count}")


if __name__ == "__main__":
    main()
