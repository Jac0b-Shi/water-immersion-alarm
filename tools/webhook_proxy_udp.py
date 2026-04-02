#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
企业微信Webhook UDP代理服务器
基于Kimi研究报告的UDP+二进制优化方案

功能:
1. 监听UDP端口，接收BC260发送的二进制数据包
2. 解码3字节二进制协议为传感器状态
3. 将状态信息转发到企业微信Webhook

二进制协议定义 (3字节):
  字节0: 状态字节
    - bit 0: water_status (0=无水, 1=有水)
    - bits 2-1: flags (01=低电量, 10=传感器故障)
    - bits 5-3: msg_type (000=心跳, 001=状态变化, 010=电压上报, 011=系统启动)
    - bits 7-6: 保留
  字节1: ADC值低8位
  字节2: ADC值高8位

使用方法:
    python webhook_proxy_udp.py

运行后监听 UDP 0.0.0.0:8080
TCP方案 (webhook_proxy.py) 不受影响，与UDP共用8080端口（协议不同，互不冲突）
"""

import socket
import struct
import requests
import sys
import os
import time
import argparse

# UDP监听端口 (与TCP的webhook_proxy.py共用8080，TCP和UDP协议不同互不冲突)
UDP_LISTEN_PORT = 8080

# 企业微信Webhook地址
WEIXIN_WEBHOOK_URL = "https://qyapi.weixin.qq.com/cgi-bin/webhook/send"


def load_config(config_path=None):
    """
    加载配置，优先级：环境变量 > 配置文件
    
    服务器部署推荐方式：
      export WEBHOOK_KEY=your_key_here
      python webhook_proxy_udp.py
    
    或使用 systemd service 的 Environment 配置
    """
    config = {}
    
    # 1. 首先尝试从环境变量读取（服务器部署推荐）
    env_webhook_key = os.environ.get('WEBHOOK_KEY', '').strip()
    if env_webhook_key:
        config['WEBHOOK_KEY'] = env_webhook_key
        print(f"[+] 从环境变量加载 WEBHOOK_KEY")
    
    # 2. 尝试从配置文件加载（本地开发用）
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
    
    if config_path and os.path.exists(config_path):
        try:
            with open(config_path, 'r', encoding='utf-8') as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith('#'):
                        continue
                    if '=' in line:
                        key, value = line.split('=', 1)
                        key = key.strip()
                        value = value.strip()
                        # 环境变量优先级更高，已存在则跳过
                        if key not in config:
                            config[key] = value
            print(f"[+] 加载配置文件: {config_path}")
        except Exception as e:
            print(f"[-] 加载配置文件失败: {e}")
    
    return config


def decode_binary_payload(data: bytes) -> dict | None:
    """
    解码3字节二进制载荷
    
    支持两种格式:
    1. 原始3字节二进制数据
    2. BC260 HEX模式发送的6字节HEX字符串 (如 b'089209')

    Args:
        data: 原始字节数据

    Returns:
        dict: 解码后的状态字典；数据不足时返回 None
    """
    # 尝试检测并处理HEX字符串格式 (BC260 HEX模式)
    # BC260在dataformat=1,1模式下会将二进制编码为HEX字符串发送
    if len(data) == 6:
        try:
            # 尝试将6字节ASCII HEX字符串解码为3字节原始数据
            hex_str = data.decode('ascii')
            if all(c in '0123456789abcdefABCDEF' for c in hex_str):
                data = bytes.fromhex(hex_str)
                print(f"[*] 检测到HEX字符串格式，已解码为原始数据: {data.hex()}")
        except (UnicodeDecodeError, ValueError):
            pass  # 不是有效的HEX字符串，继续使用原始数据
    
    if len(data) < 3:
        return None

    status_byte = data[0]
    adc_raw = struct.unpack('<H', data[1:3])[0]

    water_status = status_byte & 0x01
    flags = (status_byte >> 1) & 0x03
    msg_type = (status_byte >> 3) & 0x07

    msg_type_names = {
        0: "心跳",
        1: "状态变化",
        2: "电压上报",
        3: "系统启动",
    }

    flag_names = []
    if flags & 0x01:
        flag_names.append("低电量")
    if flags & 0x02:
        flag_names.append("传感器故障")

    # 假设参考电压3.3V，12位ADC
    voltage = (adc_raw / 4095.0) * 3.3 if adc_raw <= 4095 else None

    return {
        "msg_type": msg_type,
        "msg_type_name": msg_type_names.get(msg_type, f"未知类型({msg_type})"),
        "water_status": water_status,
        "water_status_text": "有水" if water_status else "无水",
        "flags": flags,
        "flag_names": flag_names,
        "adc_raw": adc_raw,
        "voltage": voltage,
        "raw_hex": data[:3].hex()
    }


def send_wechat_message(webhook_key, message):
    """发送企业微信消息"""
    if not webhook_key:
        print("[-] 未配置WEBHOOK_KEY，跳过发送")
        return False

    url = f"{WEIXIN_WEBHOOK_URL}?key={webhook_key}"
    payload = {
        "msgtype": "text",
        "text": {
            "content": message
        }
    }

    try:
        response = requests.post(
            url,
            json=payload,
            headers={'Content-Type': 'application/json'},
            timeout=10
        )
        result = response.json()
        if result.get('errcode') == 0:
            print("[+] 企业微信发送成功")
            return True
        else:
            print(f"[-] 企业微信返回错误: {result}")
            return False
    except Exception as e:
        print(f"[-] 发送企业微信消息失败: {e}")
        return False


def format_message(client_addr, decoded, packet_size):
    """格式化微信消息文本"""
    lines = [
        "【UDP设备告警/上报】",
        f"来源: {client_addr[0]}:{client_addr[1]}",
        f"类型: {decoded['msg_type_name']}",
        f"水浸状态: {decoded['water_status_text']}",
        f"ADC原始值: {decoded['adc_raw']}",
    ]
    if decoded['voltage'] is not None:
        lines.append(f"估算电压: {decoded['voltage']:.3f}V")
    if decoded['flag_names']:
        lines.append(f"设备标志: {', '.join(decoded['flag_names'])}")
    lines.append(f"原始数据: {decoded['raw_hex']}")
    lines.append(f"包大小: {packet_size}字节")
    lines.append(f"时间: {time.strftime('%Y-%m-%d %H:%M:%S')}")
    return "\n".join(lines)


def run_udp_proxy(webhook_key, port, forward_heartbeat=False):
    """运行UDP代理服务器"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    except (OSError, AttributeError):
        pass
    sock.bind(('0.0.0.0', port))

    print("=" * 60)
    print("企业微信Webhook UDP代理服务器")
    print("=" * 60)
    print(f"UDP监听地址: 0.0.0.0:{port}")
    print(f"Webhook目标: {WEIXIN_WEBHOOK_URL}")
    if webhook_key:
        # 只显示 key 的前8位和后4位，中间用***隐藏
        masked_key = webhook_key[:8] + "***" + webhook_key[-4:] if len(webhook_key) > 12 else "已配置"
        print(f"Webhook Key: {masked_key}")
    else:
        print(f"Webhook Key: 未配置 (消息只记录日志，不发送到企业微信)")
        print(f"  配置方式1: export WEBHOOK_KEY=your_key")
        print(f"  配置方式2: python webhook_proxy_udp.py --webhook-key YOUR_KEY")
    print(f"转发心跳: {'是' if forward_heartbeat else '否'}")
    print()
    print("二进制协议 (3字节):")
    print("  字节0: [状态/类型]")
    print("  字节1: [ADC低8位]")
    print("  字节2: [ADC高8位]")
    print()
    print("注意: 此UDP代理与TCP代理 (webhook_proxy.py) 共用8080端口，互不冲突")
    print("按 Ctrl+C 停止服务器")
    print("=" * 60)

    try:
        while True:
            data, addr = sock.recvfrom(1024)
            print(f"\n[UDP] 收到来自 {addr[0]}:{addr[1]} 的数据: {data.hex()} ({len(data)}字节)")

            decoded = decode_binary_payload(data)
            if decoded is None:
                print(f"[-] 数据长度不足，无法解码 (收到{len(data)}字节，需要至少3字节)")
                # 发送错误响应
                sock.sendto(b'ERR_LEN', addr)
                continue

            print(f"[+] 解码结果: {decoded}")

            # 发送确认响应 (ACK + 原始hex)
            ack = f"ACK:{decoded['raw_hex']}".encode('utf-8')
            sock.sendto(ack, addr)
            print(f"[+] 已发送ACK到 {addr[0]}:{addr[1]}")

            # 转发到企业微信 (仅对状态变化和告警转发，心跳可选)
            if not webhook_key:
                continue

            if decoded['msg_type'] == 0 and not forward_heartbeat:
                print("[*] 心跳消息，仅记录日志，不转发到微信")
                continue

            message = format_message(addr, decoded, len(data))
            send_wechat_message(webhook_key, message)

    except KeyboardInterrupt:
        print("\n[PROXY] 服务器已停止")
    finally:
        sock.close()
        sys.exit(0)


def main():
    parser = argparse.ArgumentParser(
        description='企业微信Webhook UDP代理服务器',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
服务器部署示例 (推荐):
  # 使用环境变量配置 (systemd service 推荐方式)
  export WEBHOOK_KEY=your_webhook_key_here
  python webhook_proxy_udp.py

本地开发示例:
  # 自动加载config.env
  python webhook_proxy_udp.py

  # 指定端口
  python webhook_proxy_udp.py --port 9090

  # 指定webhook key (覆盖环境变量和配置文件)
  python webhook_proxy_udp.py --webhook-key YOUR_KEY

  # 同时转发心跳消息到微信
  python webhook_proxy_udp.py --forward-heartbeat

systemd service 配置示例:
  [Service]
  Environment="WEBHOOK_KEY=your_key_here"
  ExecStart=/usr/bin/python3 /path/to/webhook_proxy_udp.py
        """
    )
    parser.add_argument('--port', type=int, default=UDP_LISTEN_PORT,
                        help=f'UDP监听端口 (默认: {UDP_LISTEN_PORT})')
    parser.add_argument('--webhook-key', help='企业微信Webhook Key')
    parser.add_argument('--config', help='指定config.env路径')
    parser.add_argument('--forward-heartbeat', action='store_true',
                        help='将心跳消息也转发到企业微信 (默认不转发)')

    args = parser.parse_args()

    # 加载配置（环境变量 > 命令行 > 配置文件）
    config = load_config(args.config)

    # 优先级：命令行参数 > 环境变量/配置文件
    webhook_key = args.webhook_key
    if not webhook_key:
        webhook_key = config.get('WEBHOOK_KEY', '').strip()

    run_udp_proxy(webhook_key, args.port, forward_heartbeat=args.forward_heartbeat)


if __name__ == '__main__':
    main()
