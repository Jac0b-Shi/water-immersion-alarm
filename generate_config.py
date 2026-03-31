#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
配置文件生成脚本
从 config.env 读取配置信息，生成 User/config.h 头文件
"""

import os
import sys
from pathlib import Path

def read_config(config_file):
    """读取配置文件"""
    config = {}

    if not os.path.exists(config_file):
        print(f"错误: 配置文件 {config_file} 不存在!")
        print("请复制 config.env.example 为 config.env 并填写配置信息")
        sys.exit(1)

    with open(config_file, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            # 跳过注释和空行
            if not line or line.startswith('#'):
                continue

            # 解析键值对
            if '=' in line:
                key, value = line.split('=', 1)
                config[key.strip()] = value.strip()

    return config

def validate_config(config):
    """验证配置项"""
    required_keys = ['WIFI_SSID', 'WIFI_PASSWORD', 'WEBHOOK_KEY']

    for key in required_keys:
        if key not in config or not config[key]:
            print(f"错误: 配置项 {key} 未设置!")
            return False

        # 检查是否为示例值
        if config[key] in ['你的WiFi名称', '你的WiFi密码', '你的webhook_key']:
            print(f"错误: 配置项 {key} 仍为示例值，请修改为实际值!")
            return False

    return True

def generate_header(config, output_file):
    """生成配置头文件"""

    # 解析IP地址为数组格式
    def ip_to_array(ip_str, default="0.0.0.0"):
        ip = ip_str if ip_str else default
        parts = ip.split('.')
        if len(parts) != 4:
            raise ValueError(f"Invalid IP address: {ip}")
        return ', '.join(parts)

    header_content = f'''/*
 * 自动生成的配置文件
 * 请勿手动编辑此文件！
 * 
 * 修改配置请编辑 config.env 后重新运行 generate_config.py
 * 生成时间: {__import__('datetime').datetime.now().strftime('%Y-%m-%d %H:%M:%S')}
 */

#ifndef __CONFIG_H
#define __CONFIG_H

/* WiFi配置 */
#define WIFI_SSID "{config['WIFI_SSID']}"
#define WIFI_PASSWORD "{config['WIFI_PASSWORD']}"

/* 企业微信Webhook配置 */
#define WEBHOOK_KEY "{config['WEBHOOK_KEY']}"

/* 浸水检测阈值（单位：mV） */
#ifndef WATER_THRESHOLD_MV
#define WATER_THRESHOLD_MV 1000
#endif

/* HTTPS/SSL配置 */
#ifndef USE_SSL
#define USE_SSL 1  // 1=使用SSL(443端口), 0=使用HTTP(80端口)
#endif

/* ========================================
 * 通信模块功能开关
 * 1=启用（编译相关代码）, 0=禁用（不编译相关代码）
 * ======================================== */

/* ESP8266 WiFi模块（USART3: PB10-TX, PB11-RX, PB12-RST） */
#ifndef ENABLE_ESP8266
#define ENABLE_ESP8266 {config.get('ENABLE_ESP8266', '1')}
#endif

/* BC260 NB-IoT模块（USART2: PA2-TX, PA3-RX, PA1-RST） */
#ifndef ENABLE_BC260
#define ENABLE_BC260 {config.get('ENABLE_BC260', '0')}
#endif

/* CH32V208内置10M以太网（ETH: PC6-RXD1, PC7-RXD0, PC8-TXD1, PC9-TXD0）
 * ELED1=PD3(Link LED), ELED2=PD4(Data LED) */
#ifndef ENABLE_ETHERNET
#define ENABLE_ETHERNET {config.get('ENABLE_ETHERNET', '0')}
#endif

/* ========================================
 * 以太网配置 (仅在ENABLE_ETHERNET=1时有效)
 * ======================================== */
#if ENABLE_ETHERNET

/* 本机IP地址 */
#define ETH_IP_ADDR     {{ {ip_to_array(config.get('ETH_IP_ADDR', '192.168.1.10'))} }}

/* 网关地址 */
#define ETH_GATEWAY     {{ {ip_to_array(config.get('ETH_GATEWAY', '192.168.1.1'))} }}

/* 子网掩码 */
#define ETH_NETMASK     {{ {ip_to_array(config.get('ETH_NETMASK', '255.255.255.0'))} }}

/* DNS服务器地址 */
#define ETH_DNS_SERVER  {{ {ip_to_array(config.get('ETH_DNS_SERVER', '8.8.8.8'))} }}

/* HTTP代理服务器配置（内网，用于转发HTTPS请求到企业微信） */
#define HTTP_PROXY_IP   {{ {ip_to_array(config.get('HTTP_PROXY_IP', '192.168.1.1'))} }}
#define HTTP_PROXY_PORT {config.get('HTTP_PROXY_PORT', '8080')}

#endif /* ENABLE_ETHERNET */

/* ========================================
 * BC260 NB-IoT配置 (仅在ENABLE_BC260=1时有效)
 * ======================================== */
#if ENABLE_BC260

/* BC260 HTTP代理服务器配置（公网，用于转发HTTPS请求到企业微信） */
/* 注意：BC260通过移动网络访问公网，需要部署在公网的代理服务器 */
#define BC260_PROXY_IP   {{ {ip_to_array(config.get('BC260_PROXY_IP', '0.0.0.0'))} }}
#define BC260_PROXY_PORT {config.get('BC260_PROXY_PORT', '8080')}

#endif /* ENABLE_BC260 */

#endif /* __CONFIG_H */
'''

    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(header_content)

    print(f"[OK] 配置文件已生成: {output_file}")

def main():
    # 确定路径
    script_dir = Path(__file__).parent
    config_file = script_dir / 'config.env'
    output_file = script_dir / 'User' / 'config.h'

    print("=" * 50)
    print("浸水检测报警系统 - 配置文件生成器")
    print("=" * 50)

    # 读取配置
    print(f"\n正在读取配置文件: {config_file}")
    config = read_config(config_file)

    # 验证配置
    print("\n正在验证配置...")
    if not validate_config(config):
        sys.exit(1)

    # 生成头文件
    print("\n正在生成配置头文件...")
    generate_header(config, output_file)

    print("\n" + "=" * 50)
    print("配置生成完成！")
    print("请重新编译项目以应用新配置")
    print("=" * 50)

if __name__ == '__main__':
    main()

