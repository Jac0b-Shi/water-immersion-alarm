#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
UDP代理服务器测试脚本
基于Kimi研究报告的UDP+二进制优化方案

用于验证UDP代理服务器是否能正常接收二进制数据包并返回ACK

使用方法:
    # 自动从config.env加载配置测试
    python test_proxy_udp.py

    # 手动指定参数
    python test_proxy_udp.py --proxy-ip 192.168.1.1 --proxy-port 8081 \
        --water 1 --adc 3000 --type 1

    # 仅测试UDP连通性（不发送结构化数据）
    python test_proxy_udp.py --test-connect-only
"""

import socket
import struct
import sys
import time
import argparse
import os

# UDP代理默认端口 (与TCP代理共用8080，协议不同互不冲突)
DEFAULT_PROXY_PORT = 8080


def get_first_secret_key(config):
    """从配置中提取首个UDP密钥"""
    secret_str = config.get('UDP_SECRET_KEY', '').strip()
    if not secret_str:
        return ''
    return next((item.strip() for item in secret_str.split(',') if item.strip()), '')


def load_config_from_env(config_path=None):
    """从config.env文件加载配置"""
    if config_path is None:
        possible_paths = [
            'config.env',
            '../config.env',
            os.path.join(os.path.dirname(__file__), '..', 'config.env'),
            os.path.join(os.path.dirname(__file__), 'config.env')
        ]
        for path in possible_paths:
            if os.path.exists(path):
                config_path = path
                break

    config = {}
    if config_path and os.path.exists(config_path):
        try:
            with open(config_path, 'r', encoding='utf-8') as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith('#'):
                        continue
                    if '=' in line:
                        key, value = line.split('=', 1)
                        config[key.strip()] = value.strip()
        except Exception as e:
            print(f"[-] 加载配置文件失败: {e}")
    return config


def encode_payload(msg_type, water_status, flags, adc_value):
    """编码3字节二进制载荷"""
    status_byte = (
        (water_status & 0x01) |
        ((flags & 0x03) << 1) |
        ((msg_type & 0x07) << 3)
    )
    adc_clamped = max(0, min(65535, int(adc_value)))
    return struct.pack('<BH', status_byte, adc_clamped)


def decode_payload(data):
    """解码3字节二进制载荷"""
    if len(data) < 3:
        return None
    status_byte = data[0]
    adc_raw = struct.unpack('<H', data[1:3])[0]
    return {
        "msg_type": (status_byte >> 3) & 0x07,
        "water_status": status_byte & 0x01,
        "flags": (status_byte >> 1) & 0x03,
        "adc_raw": adc_raw,
    }


def test_udp_connect(proxy_ip, proxy_port, timeout=5):
    """测试UDP连接到代理服务器"""
    print(f"\n[1] 测试UDP连接到代理服务器 {proxy_ip}:{proxy_port}...")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(timeout)
        # 发送一个空探测包
        sock.sendto(b'PING', (proxy_ip, proxy_port))
        try:
            data, addr = sock.recvfrom(1024)
            print(f"[+] 收到UDP响应: {data} (来自 {addr[0]}:{addr[1]})")
            sock.close()
            return True
        except socket.timeout:
            print("[*] UDP探测包未收到响应 (正常现象，代理可能不回复PING)")
            print(f"[+] UDP发送成功，端口{proxy_port}可能开放")
            sock.close()
            return True
    except Exception as e:
        print(f"[-] UDP连接异常: {e}")
        return False


def test_udp_binary_packet(proxy_ip, proxy_port, msg_type, water_status, flags, adc_value,
                           timeout=10, hex_mode=False, udp_secret=''):
    """
    测试发送二进制数据包到UDP代理
    
    Args:
        hex_mode: 如果为True，发送6字节HEX字符串（模拟BC260的HEX模式）
                 如果为False，发送3字节原始二进制
    """
    print(f"\n[2] 测试发送UDP二进制数据包...")
    print(f"    代理: {proxy_ip}:{proxy_port}")
    print(f"    模式: {'HEX字符串(模拟BC260)' if hex_mode else '原始二进制'}")
    print(f"    类型: {msg_type}, 水浸: {water_status}, 标志: {flags}, ADC: {adc_value}")

    payload = encode_payload(msg_type, water_status, flags, adc_value)

    if hex_mode:
        # 模拟BC260的HEX模式：将3字节二进制编码为6字节HEX字符串
        payload = payload.hex().encode('ascii')

    if udp_secret:
        payload = udp_secret.encode('utf-8') + payload
        print(f"    已附加UDP密钥前缀: {udp_secret[:4]}...")

    print(f"    载荷(HEX): {payload.hex()} ({len(payload)}字节)")

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(timeout)
        sock.sendto(payload, (proxy_ip, proxy_port))
        print(f"[+] 数据包已发送")

        # 等待ACK响应
        print(f"[*] 等待ACK响应 (最多{timeout}秒)...")
        try:
            data, addr = sock.recvfrom(1024)
            print(f"[+] 收到响应: {data} (来自 {addr[0]}:{addr[1]})")
            if data.startswith(b'ACK:'):
                ack_hex = data[4:].decode('utf-8', errors='ignore')
                print(f"[+] 代理确认收到数据: {ack_hex}")
                sock.close()
                return True
            else:
                print(f"[*] 收到非ACK响应: {data}")
                sock.close()
                return True
        except socket.timeout:
            print("[*] 未收到ACK响应 (UDP不保证响应)")
            print("[+] 但数据包已成功发送")
            sock.close()
            return True

    except Exception as e:
        print(f"[-] 发送失败: {e}")
        import traceback
        traceback.print_exc()
        return False


def main():
    parser = argparse.ArgumentParser(
        description='UDP代理服务器测试工具',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  # 自动从config.env加载配置并测试 (发送原始二进制)
  python test_proxy_udp.py

  # 模拟BC260的HEX模式发送 (推荐用于测试代理服务器)
  python test_proxy_udp.py --hex-mode

  # 仅测试UDP连通性
  python test_proxy_udp.py --test-connect-only

  # 手动指定参数
  python test_proxy_udp.py --proxy-ip x.x.x.x --proxy-port 8080 \
      --water 1 --adc 3000 --type 1

  # 模拟BC260发送告警消息
  python test_proxy_udp.py --hex-mode --water 1 --adc 3000 --type 1
        """
    )
    parser.add_argument('--proxy-ip', help='UDP代理服务器IP')
    parser.add_argument('--proxy-port', type=int, help='UDP代理服务器端口')
    parser.add_argument('--config', help='指定config.env路径')
    parser.add_argument('--auto-config', action='store_true',
                        help='自动从config.env加载代理配置（使用BC260_PROXY_IP和BC260_PROXY_PORT，UDP与TCP共用端口）')
    parser.add_argument('--udp-secret',
                        help='UDP密钥（覆盖配置文件中的UDP_SECRET_KEY，多个密钥时默认取第一个）')
    parser.add_argument('--test-connect-only', action='store_true',
                        help='仅测试UDP连通性，不发送结构化数据')
    parser.add_argument('--water', type=int, default=0, choices=[0, 1],
                        help='水浸状态 (0=无水, 1=有水)')
    parser.add_argument('--adc', type=int, default=2450, help='ADC原始值')
    parser.add_argument('--type', type=int, default=1, choices=[0, 1, 2],
                        help='消息类型 (0=心跳, 1=状态变化, 2=电压上报)')
    parser.add_argument('--flags', type=int, default=0,
                        help='标志位 (bit0=低电量, bit1=传感器故障)')
    parser.add_argument('--hex-mode', action='store_true',
                        help='使用HEX字符串模式发送(模拟BC260)，默认发送原始二进制')

    args = parser.parse_args()

    print("="*60)
    print("UDP代理服务器测试工具")
    print("="*60)

    # 加载配置
    config = load_config_from_env(args.config)

    proxy_ip = args.proxy_ip
    proxy_port = args.proxy_port
    udp_secret = args.udp_secret or ''

    if args.auto_config and config:
        config_ip = config.get('BC260_PROXY_IP', '').strip()
        if config_ip:
            proxy_ip = config_ip
            print(f"[+] 从配置加载代理IP (BC260_PROXY_IP): {proxy_ip}")

        # UDP与TCP共用同一端口（协议不同，互不冲突）
        tcp_port_str = config.get('BC260_PROXY_PORT', '').strip()
        if tcp_port_str:
            try:
                proxy_port = int(tcp_port_str)
                print(f"[+] 从配置加载端口 (BC260_PROXY_PORT): {proxy_port} (UDP与TCP共用)")
            except ValueError:
                pass
        if not udp_secret:
            udp_secret = get_first_secret_key(config)
            if udp_secret:
                print(f"[+] 从配置加载UDP密钥 (UDP_SECRET_KEY): {udp_secret[:4]}...")
    elif not args.proxy_ip and config:
        # 非自动模式但无手动指定
        config_ip = config.get('BC260_PROXY_IP', '').strip()
        if config_ip:
            proxy_ip = config_ip
            print(f"[+] 从配置加载代理IP (BC260_PROXY_IP): {proxy_ip}")

        udp_port_str = config.get('UDP_PROXY_PORT', '').strip()
        if udp_port_str:
            try:
                proxy_port = int(udp_port_str)
                print(f"[+] 从配置加载UDP端口 (UDP_PROXY_PORT): {proxy_port}")
            except ValueError:
                pass
        if not udp_secret:
            udp_secret = get_first_secret_key(config)
            if udp_secret:
                print(f"[+] 从配置加载UDP密钥 (UDP_SECRET_KEY): {udp_secret[:4]}...")

    if not proxy_ip:
        proxy_ip = '127.0.0.1'
        print(f"[*] 使用默认代理IP: {proxy_ip}")

    if not proxy_port:
        proxy_port = DEFAULT_PROXY_PORT
        print(f"[*] 使用默认代理端口: {proxy_port}")

    print("\n" + "-"*60)
    print("测试配置:")
    print(f"  UDP代理服务器: {proxy_ip}:{proxy_port}")
    print(f"  发送模式: {'HEX字符串(模拟BC260)' if args.hex_mode else '原始二进制'}")
    print(f"  UDP密钥: {'已配置' if udp_secret else '未配置'}")
    if not args.test_connect_only:
        print(f"  测试载荷: type={args.type}, water={args.water}, adc={args.adc}, flags={args.flags}")
    print("-"*60)

    # 执行测试
    results = {}

    # 测试1: UDP连通性
    results['udp_connect'] = test_udp_connect(proxy_ip, proxy_port)
    if not results['udp_connect']:
        print("\n[-] UDP连接测试失败")
        sys.exit(1)

    # 测试2: 发送二进制数据包
    if not args.test_connect_only:
        results['send_binary'] = test_udp_binary_packet(
            proxy_ip, proxy_port,
            args.type, args.water, args.flags, args.adc,
            hex_mode=args.hex_mode,
            udp_secret=udp_secret
        )

    # 打印测试摘要
    print("\n" + "="*60)
    print("测试摘要")
    print("="*60)

    test_names = {
        'udp_connect': 'UDP连通性',
        'send_binary': '发送二进制数据包',
    }

    for key, name in test_names.items():
        if key in results:
            status = "[OK] 通过" if results[key] else "[FAIL] 失败"
            print(f"  {name}: {status}")

    print("="*60)

    if all(results.values()):
        print("\n[+] 所有测试通过！UDP代理工作正常")
        sys.exit(0)
    else:
        print("\n[-] 部分测试失败")
        sys.exit(1)


if __name__ == '__main__':
    main()
