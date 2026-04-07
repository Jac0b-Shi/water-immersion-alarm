#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
超声波液位 UDP 代理测试脚本

发送 1xx 类型的 3 字节二进制消息，用于验证 webhook_proxy.py
对超声波液位数据包的解析与 ACK 返回。

示例:
    python test_proxy_ultrasonic_udp.py --auto-config
    python test_proxy_ultrasonic_udp.py --auto-config --distance 180 --type high
    python test_proxy_ultrasonic_udp.py --proxy-ip 127.0.0.1 --proxy-port 8080 --distance 620 --type periodic
"""

import argparse
import sys

from test_proxy_udp import (
    DEFAULT_PROXY_PORT,
    get_first_secret_key,
    load_config_from_env,
    test_udp_binary_packet,
    test_udp_connect,
)


ULTRASONIC_TYPE_MAP = {
    "heartbeat": 4,
    "high": 5,
    "periodic": 6,
    "startup": 7,
}


def main():
    parser = argparse.ArgumentParser(description='超声波液位 UDP 代理测试脚本')
    parser.add_argument('--proxy-ip', help='UDP代理服务器IP')
    parser.add_argument('--proxy-port', type=int, help='UDP代理服务器端口')
    parser.add_argument('--config', help='指定config.env路径')
    parser.add_argument('--auto-config', action='store_true',
                        help='自动从config.env加载 BC260 代理配置和 UDP 密钥')
    parser.add_argument('--udp-secret',
                        help='UDP密钥（覆盖配置文件中的 UDP_SECRET_KEY）')
    parser.add_argument('--type', choices=ULTRASONIC_TYPE_MAP.keys(), default='periodic',
                        help='超声消息类型: heartbeat/high/periodic/startup')
    parser.add_argument('--distance', type=int, default=200,
                        help='传感器测距值（mm），将编码到 16 位测量值字段')
    parser.add_argument('--high-level', type=int, choices=[0, 1], default=None,
                        help='显式指定 bit0 状态（0=正常, 1=高液位）；默认按 --type 自动推断')
    parser.add_argument('--flags', type=int, default=0,
                        help='标志位 (bit0=低电量, bit1=传感器故障)')
    parser.add_argument('--hex-mode', action='store_true',
                        help='使用HEX字符串模式发送(模拟BC260)')

    args = parser.parse_args()

    config = load_config_from_env(args.config)
    proxy_ip = args.proxy_ip
    proxy_port = args.proxy_port
    udp_secret = args.udp_secret or ''

    if args.auto_config and config:
        if not proxy_ip:
            proxy_ip = config.get('BC260_PROXY_IP', '').strip()
        if not proxy_port:
            port_str = config.get('BC260_PROXY_PORT', '').strip()
            if port_str:
                proxy_port = int(port_str)
        if not udp_secret:
            udp_secret = get_first_secret_key(config)

    if not proxy_ip:
        proxy_ip = '127.0.0.1'
    if not proxy_port:
        proxy_port = DEFAULT_PROXY_PORT

    msg_type = ULTRASONIC_TYPE_MAP[args.type]
    water_status = args.high_level
    if water_status is None:
        water_status = 1 if args.type == 'high' else 0

    print("=" * 60)
    print("超声波液位 UDP 代理测试")
    print("=" * 60)
    print(f"代理: {proxy_ip}:{proxy_port}")
    print(f"消息类型: {args.type} ({msg_type})")
    print(f"测距值: {args.distance}mm")
    print(f"高液位状态: {water_status}")
    print(f"发送模式: {'HEX字符串(模拟BC260)' if args.hex_mode else '原始二进制'}")
    print(f"UDP密钥: {'已配置' if udp_secret else '未配置'}")

    if not test_udp_connect(proxy_ip, proxy_port):
        print("\n[-] UDP 连通性测试失败")
        sys.exit(1)

    success = test_udp_binary_packet(
        proxy_ip,
        proxy_port,
        msg_type=msg_type,
        water_status=water_status,
        flags=args.flags,
        adc_value=args.distance,
        hex_mode=args.hex_mode,
        udp_secret=udp_secret,
    )

    if success:
        print("\n[+] 超声波液位消息发送完成")
        sys.exit(0)

    print("\n[-] 超声波液位消息发送失败")
    sys.exit(1)


if __name__ == '__main__':
    main()
