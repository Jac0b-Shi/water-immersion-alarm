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
    required_keys = ['WEBHOOK_KEY']

    def parse_positive_int(key, minimum=1, maximum=None):
        raw_value = config.get(key, '').strip()
        if not raw_value:
            return None
        try:
            value = int(raw_value)
        except ValueError:
            print(f"错误: 配置项 {key} 必须是整数!")
            return False
        if value < minimum:
            print(f"错误: 配置项 {key} 必须大于等于 {minimum}!")
            return False
        if maximum is not None and value > maximum:
            print(f"错误: 配置项 {key} 必须小于等于 {maximum}!")
            return False
        return True

    for key in required_keys:
        if key not in config or not config[key]:
            print(f"错误: 配置项 {key} 未设置!")
            return False

        # 检查是否为示例值
        if config[key] in ['你的webhook_key']:
            print(f"错误: 配置项 {key} 仍为示例值，请修改为实际值!")
            return False

    if config.get('ENABLE_ESP8266', '0') == '1':
        print("错误: ESP8266 支持已弃用，不再维护，请改用 BC260 或以太网。")
        return False

    if config.get('ENABLE_IMMERSION_SENSOR', '1') == '1':
        for key, minimum, maximum in [
            ('IMMERSION_WET_THRESHOLD_MV', 0, 65535),
            ('IMMERSION_DRY_THRESHOLD_MV', 0, 65535),
            ('IMMERSION_WET_CONFIRM_COUNT', 1, 255),
            ('IMMERSION_DRY_CONFIRM_COUNT', 1, 255),
        ]:
            result = parse_positive_int(key, minimum, maximum)
            if result is False:
                return False

    if config.get('ENABLE_ULTRASONIC_SENSOR', '0') == '1':
        for key, minimum, maximum in [
            ('ULTRASONIC_UART_BAUDRATE', 1200, None),
            ('ULTRASONIC_MAX_DISTANCE_MM', 1, 65535),
            ('ULTRASONIC_SAMPLING_IDLE_SECONDS', 0, 86400),
            ('ULTRASONIC_SAMPLING_BURST_DURATION_SECONDS', 0, 3600),
            ('ULTRASONIC_SAMPLING_BURST_INTERVAL_MS', 0, 60000),
            ('ULTRASONIC_PERIODIC_REPORT_INTERVAL_MIN', 1, 1440),
            ('ULTRASONIC_HIGH_LEVEL_DISTANCE_THRESHOLD_MM', 0, 65535),
            ('ULTRASONIC_HIGH_LEVEL_REPORT_INTERVAL_MIN', 1, 1440),
            ('ULTRASONIC_FILTER_WINDOW_SECONDS', 1, 86400),
            ('ULTRASONIC_FILTER_SAMPLE_COUNT', 1, 512),
            ('ULTRASONIC_FILTER_TRIM_PERCENT', 0, 49),
        ]:
            result = parse_positive_int(key, minimum, maximum)
            if result is False:
                return False

        if 'ULTRASONIC_HIGH_LEVEL_DISTANCE_THRESHOLD_MM' not in config and 'ULTRASONIC_LOW_LEVEL_THRESHOLD_MM' in config:
            config['ULTRASONIC_HIGH_LEVEL_DISTANCE_THRESHOLD_MM'] = config['ULTRASONIC_LOW_LEVEL_THRESHOLD_MM']
        if 'ULTRASONIC_HIGH_LEVEL_REPORT_INTERVAL_MIN' not in config and 'ULTRASONIC_LOW_LEVEL_REPORT_INTERVAL_MIN' in config:
            config['ULTRASONIC_HIGH_LEVEL_REPORT_INTERVAL_MIN'] = config['ULTRASONIC_LOW_LEVEL_REPORT_INTERVAL_MIN']
        if 'ULTRASONIC_HIGH_LEVEL_REPORT_ENABLED' not in config and 'ULTRASONIC_LOW_LEVEL_REPORT_ENABLED' in config:
            config['ULTRASONIC_HIGH_LEVEL_REPORT_ENABLED'] = config['ULTRASONIC_LOW_LEVEL_REPORT_ENABLED']

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

/* ESP8266 支持已弃用，不再维护 */
#define WIFI_SSID "{config.get('WIFI_SSID', '')}"
#define WIFI_PASSWORD "{config.get('WIFI_PASSWORD', '')}"

/* 企业微信Webhook配置 */
#define WEBHOOK_KEY "{config['WEBHOOK_KEY']}"

/* HTTPS/SSL配置 */
#ifndef USE_SSL
#define USE_SSL 1  // 1=使用SSL(443端口), 0=使用HTTP(80端口)
#endif

/* ========================================
 * 通信模块功能开关
 * 1=启用（编译相关代码）, 0=禁用（不编译相关代码）
 * ======================================== */

/* 浸水传感器模块（ADC: PA0） */
#ifndef ENABLE_IMMERSION_SENSOR
#define ENABLE_IMMERSION_SENSOR {config.get('ENABLE_IMMERSION_SENSOR', '1')}
#endif

/* ESP8266 WiFi模块（已弃用，不再维护） */
#ifndef ENABLE_ESP8266
#define ENABLE_ESP8266 0
#endif

/* BC260 NB-IoT模块（USART2: PA2-TX, PA3-RX, PA1-RST） */
#ifndef ENABLE_BC260
#define ENABLE_BC260 {config.get('ENABLE_BC260', '0')}
#endif

/* 超声波液位模块（UART4 默认映射: PC10-TX, PC11-RX, RX DMA固定4字节帧） */
#ifndef ENABLE_ULTRASONIC_SENSOR
#define ENABLE_ULTRASONIC_SENSOR {config.get('ENABLE_ULTRASONIC_SENSOR', '0')}
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

/* UDP密钥验证配置（防止伪造消息） */
/* 设置一个密钥字符串，发送的数据包会以此密钥开头 */
/* 必须与代理服务器 config.env 中的 UDP_SECRET_KEY 一致 */
#define UDP_SECRET_KEY "{config.get('UDP_SECRET_KEY', '')}"

#endif /* ENABLE_BC260 */

/* ========================================
 * 超声波液位配置 (仅在ENABLE_ULTRASONIC_SENSOR=1时有效)
 * ======================================== */
#if ENABLE_ULTRASONIC_SENSOR

/* L07A UART参数（UART4 默认映射: PC10-TX, PC11-RX） */
/* 协议帧: [0xFF, Data_H, Data_L, SUM], 特殊值: 0xFFFE=干扰, 0xFFFD=未检测到物体 */
#define ULTRASONIC_UART_BAUDRATE {config.get('ULTRASONIC_UART_BAUDRATE', '115200')}

/* 传感器测量有效范围 */
#define ULTRASONIC_MIN_DISTANCE_MM {config.get('ULTRASONIC_MIN_DISTANCE_MM', '0')}
#define ULTRASONIC_MAX_DISTANCE_MM {config.get('ULTRASONIC_MAX_DISTANCE_MM', '3000')}

/* 采样调度策略
 * BURST_DURATION_SECONDS=0 时回退到旧的均匀采样间隔计算；
 * 否则每次空闲一段时间后，进入一次短时间高频重试窗口，窗口内捕获到首个有效值即结束本轮。
 */
#define ULTRASONIC_SAMPLING_IDLE_SECONDS {config.get('ULTRASONIC_SAMPLING_IDLE_SECONDS', '0')}
#define ULTRASONIC_SAMPLING_BURST_DURATION_SECONDS {config.get('ULTRASONIC_SAMPLING_BURST_DURATION_SECONDS', '0')}
#define ULTRASONIC_SAMPLING_BURST_INTERVAL_MS {config.get('ULTRASONIC_SAMPLING_BURST_INTERVAL_MS', '0')}

/* 定时上报策略 */
#define ULTRASONIC_PERIODIC_REPORT_ENABLED {config.get('ULTRASONIC_PERIODIC_REPORT_ENABLED', '1')}
#define ULTRASONIC_PERIODIC_REPORT_INTERVAL_MIN {config.get('ULTRASONIC_PERIODIC_REPORT_INTERVAL_MIN', '30')}

/* 高液位加密上报策略（测距值小于阈值代表水位升高） */
#define ULTRASONIC_HIGH_LEVEL_REPORT_ENABLED {config.get('ULTRASONIC_HIGH_LEVEL_REPORT_ENABLED', config.get('ULTRASONIC_LOW_LEVEL_REPORT_ENABLED', '1'))}
#define ULTRASONIC_HIGH_LEVEL_DISTANCE_THRESHOLD_MM {config.get('ULTRASONIC_HIGH_LEVEL_DISTANCE_THRESHOLD_MM', config.get('ULTRASONIC_LOW_LEVEL_THRESHOLD_MM', '200'))}
#define ULTRASONIC_HIGH_LEVEL_REPORT_INTERVAL_MIN {config.get('ULTRASONIC_HIGH_LEVEL_REPORT_INTERVAL_MIN', config.get('ULTRASONIC_LOW_LEVEL_REPORT_INTERVAL_MIN', '5'))}

/* 滤波窗口策略 */
#define ULTRASONIC_FILTER_WINDOW_SECONDS {config.get('ULTRASONIC_FILTER_WINDOW_SECONDS', '300')}
#define ULTRASONIC_FILTER_SAMPLE_COUNT {config.get('ULTRASONIC_FILTER_SAMPLE_COUNT', '50')}
#define ULTRASONIC_FILTER_TRIM_PERCENT {config.get('ULTRASONIC_FILTER_TRIM_PERCENT', '20')}

#endif /* ENABLE_ULTRASONIC_SENSOR */

/* ========================================
 * 浸水传感器配置 (仅在ENABLE_IMMERSION_SENSOR=1时有效)
 * ======================================== */
#if ENABLE_IMMERSION_SENSOR

#define IMMERSION_WET_THRESHOLD_MV {config.get('IMMERSION_WET_THRESHOLD_MV', '1000')}
#define IMMERSION_DRY_THRESHOLD_MV {config.get('IMMERSION_DRY_THRESHOLD_MV', '500')}
#define IMMERSION_WET_CONFIRM_COUNT {config.get('IMMERSION_WET_CONFIRM_COUNT', '2')}
#define IMMERSION_DRY_CONFIRM_COUNT {config.get('IMMERSION_DRY_CONFIRM_COUNT', '5')}

#endif /* ENABLE_IMMERSION_SENSOR */

#endif /* __CONFIG_H */
'''

    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(header_content)

    print(f"[OK] 配置文件已生成: {output_file}")

def main():
    # 确定路径
    script_dir = Path(__file__).parent
    config_file = Path(sys.argv[1]) if len(sys.argv) >= 2 else script_dir / 'config.env'
    output_file = Path(sys.argv[2]) if len(sys.argv) >= 3 else script_dir / 'User' / 'config.h'

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

