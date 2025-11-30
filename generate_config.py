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

#endif /* __CONFIG_H */
'''

    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(header_content)

    print(f"✓ 配置文件已生成: {output_file}")

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

