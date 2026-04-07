#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
BC260 NB-IoT 模块UDP通信测试脚本
基于Kimi研究报告的UDP+二进制优化方案

功能：
1. 测试BC260基本AT指令通信
2. 检查SIM卡和网络附着状态
3. 通过UDP发送二进制数据包到代理服务器

二进制协议定义 (3字节):
  字节0: 状态字节
    - bit 0: water_status (0=无水, 1=有水)
    - bits 2-1: flags (01=低电量, 10=传感器故障)
    - bits 5-3: msg_type (000=心跳, 001=状态变化, 010=电压上报, 011=系统启动)
    - bits 7-6: 保留
  字节1: ADC值低8位
  字节2: ADC值高8位

使用方式：
    # 自动从config.env加载配置（推荐）
    python bc260_test_udp.py --port COM3 --auto-config

    # 手动指定参数
    python bc260_test_udp.py --port COM3 --proxy-ip "YOUR_PROXY_IP" --proxy-port 8081

    # 自定义传感器状态
    python bc260_test_udp.py --port COM3 --auto-config --water 1 --adc 2450 --type 1

重要提示:
---------
此脚本为UDP可行性验证版本，不修改已有的TCP方案 (bc260_test.py)。
UDP无连接特性意味着不保证到达，但可显著降低NB-IoT流量消耗。

硬件连接：
    BC260 TX -> 电脑USB转串口 RX
    BC260 RX -> 电脑USB转串口 TX
    BC260 GND -> 电脑GND
    确保BC260已上电（3.3V或5V根据模块要求）
"""

import serial
import serial.tools.list_ports
import time
import sys
import argparse
import binascii
import re
import os
import struct

# 默认配置
DEFAULT_PORT = "COM3"
DEFAULT_BAUDRATE = 9600
DEFAULT_TIMEOUT = 5

# UDP代理配置（与TCP代理共用8080端口，TCP和UDP互不冲突）
DEFAULT_PROXY_IP = "127.0.0.1"
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

    if not config_path or not os.path.exists(config_path):
        return {}

    config = {}
    print(f"[*] 正在加载配置文件: {config_path}")
    try:
        with open(config_path, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                if '=' in line:
                    key, value = line.split('=', 1)
                    config[key.strip()] = value.strip()
        print(f"[+] 配置文件加载成功")
    except Exception as e:
        print(f"[-] 加载配置文件失败: {e}")
    return config


def encode_payload(msg_type, water_status, flags, adc_value):
    """
    编码3字节二进制载荷

    Args:
        msg_type: 消息类型 (0=心跳, 1=状态变化, 2=电压上报, 3=系统启动)
        water_status: 水浸状态 (0或1)
        flags: 标志位 (bit0=低电量, bit1=传感器故障)
        adc_value: ADC原始值 (0-65535)

    Returns:
        bytes: 3字节二进制数据
    """
    status_byte = (
        (water_status & 0x01) |
        ((flags & 0x03) << 1) |
        ((msg_type & 0x07) << 3)
    )
    adc_clamped = max(0, min(65535, int(adc_value)))
    return struct.pack('<BH', status_byte, adc_clamped)


class BC260UDPTester:
    """BC260 NB-IoT模块UDP测试类"""

    def __init__(self, port, baudrate=115200, timeout=5, proxy_ip=None, proxy_port=None,
                 udp_secret='', verbose=False):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.proxy_ip = proxy_ip or DEFAULT_PROXY_IP
        self.proxy_port = proxy_port or DEFAULT_PROXY_PORT
        self.udp_secret = udp_secret or ''
        self.verbose = verbose
        self.ser = None
        self.network_attached = False
        self.socket_created = False
        self._urc_buffer = []  # 存储URC消息，供跨方法使用
        self.imei = None

    def connect(self):
        """连接串口"""
        try:
            print(f"[*] 正在连接 {self.port} @ {self.baudrate}bps...")
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=self.timeout
            )
            time.sleep(0.5)
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
            print(f"[+] 串口连接成功: {self.port}")
            return True
        except Exception as e:
            print(f"[-] 串口连接失败: {e}")
            return False

    def disconnect(self):
        """断开串口连接"""
        if self.socket_created:
            print("[*] 关闭UDP socket...")
            self.send_at_command("AT+QICLOSE=0", timeout=5)
            self.socket_created = False
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("[*] 串口已断开")

    def send_at_command(self, cmd, expected_response="OK", timeout=None):
        """发送AT指令并等待响应"""
        if timeout is None:
            timeout = self.timeout
        if not self.ser or not self.ser.is_open:
            print("[-] 串口未连接")
            return False, ""

        self.ser.reset_input_buffer()
        full_cmd = f"{cmd}\r\n"
        print(f"[>] 发送: {cmd}")
        if self.verbose:
            print(f"[DEBUG] HEX: {binascii.hexlify(full_cmd.encode()).decode()}")

        self.ser.write(full_cmd.encode('utf-8'))
        self.ser.flush()

        start_time = time.time()
        response_lines = []

        while time.time() - start_time < timeout:
            if self.ser.in_waiting > 0:
                try:
                    chunk = self.ser.readline()
                    line = chunk.decode('utf-8', errors='ignore').strip()
                    if line:
                        print(f"[<] 接收: {line}")
                        response_lines.append(line)
                        if expected_response and expected_response in line:
                            time.sleep(0.3)
                            while self.ser.in_waiting > 0:
                                extra = self.ser.readline().decode('utf-8', errors='ignore').strip()
                                if extra:
                                    print(f"[<] 接收: {extra}")
                                    response_lines.append(extra)
                            return True, "\n".join(response_lines)
                        if "ERROR" in line or "FAIL" in line:
                            return False, "\n".join(response_lines)
                except Exception as e:
                    print(f"[-] 接收数据错误: {e}")
                    break
            time.sleep(0.1)

        response = "\n".join(response_lines)
        if expected_response and expected_response in response:
            return True, response
        print(f"[-] 等待响应超时或未收到期望响应: {expected_response}")
        return False, response

    def test_basic_at(self):
        """测试基本AT通信"""
        print("\n" + "="*50)
        print("测试1: 基本AT通信")
        print("="*50)
        for i in range(3):
            success, _ = self.send_at_command("AT", "OK", timeout=2)
            if success:
                print("[+] AT通信测试通过!")
                return True
            time.sleep(0.5)
        print("[-] AT通信测试失败")
        return False

    def test_module_info(self):
        """测试获取模块信息"""
        print("\n" + "="*50)
        print("测试2: 获取模块信息")
        print("="*50)
        success, response = self.send_at_command("ATI", "OK", timeout=3)
        if success:
            for line in response.split('\n'):
                if 'Quectel' in line or 'BC260' in line:
                    print(f"[+] 模块型号: {line.strip()}")
        success, response = self.send_at_command("AT+CGSN=1", "OK", timeout=3)
        if success:
            match = re.search(r'\+CGSN:\s*(\d+)', response)
            if match:
                self.imei = match.group(1)
                print(f"[+] IMEI: {self.imei}")
        self.send_at_command("ATE0", "OK", timeout=2)
        return True

    def test_sim_card(self):
        """测试SIM卡状态"""
        print("\n" + "="*50)
        print("测试3: SIM卡检测")
        print("="*50)
        success, response = self.send_at_command("AT+CIMI", "OK", timeout=5)
        if success:
            for line in response.split('\n'):
                line = line.strip()
                if line and line not in ['AT+CIMI', 'OK'] and not line.startswith('+') and line.isdigit():
                    print(f"[+] SIM卡检测成功, IMSI: {line}")
                    return True
            print("[+] SIM卡检测成功")
            return True
        print("[-] SIM卡检测失败")
        return False

    def test_signal_quality(self):
        """测试信号质量"""
        print("\n" + "="*50)
        print("测试4: 信号质量检查")
        print("="*50)
        success, response = self.send_at_command("AT+CSQ", "OK", timeout=3)
        if success:
            match = re.search(r'\+CSQ:\s*(\d+),(\d+)', response)
            if match:
                rssi = int(match.group(1))
                ber = int(match.group(2))
                print(f"[+] 信号强度: {rssi}, 误码率: {ber}")
                return True
        print("[-] 信号质量检查失败")
        return False

    def test_network_attachment(self):
        """测试网络附着状态"""
        print("\n" + "="*50)
        print("测试5: 网络附着检查")
        print("="*50)
        self.send_at_command("AT+CFUN=1", "OK", timeout=5)
        self.send_at_command('AT+CGDCONT=1,"IP","cmnbiot"', "OK", timeout=5)

        max_retry = 30
        for i in range(max_retry):
            success, response = self.send_at_command("AT+CEREG?", "OK", timeout=5)
            if success:
                match = re.search(r'\+CEREG:\s*\d+,(\d+)', response)
                if match:
                    stat = int(match.group(1))
                    if stat in (1, 5):
                        print(f"[+] 网络已注册 (状态: {stat})")
                        break
            time.sleep(2)

        self.send_at_command("AT+CGATT=1", "OK", timeout=10)
        for i in range(max_retry):
            success, response = self.send_at_command("AT+CGATT?", "OK", timeout=5)
            if success and "+CGATT: 1" in response:
                print(f"[+] 网络已附着")
                self.network_attached = True
                success, response = self.send_at_command("AT+CGPADDR", "OK", timeout=3)
                if success:
                    ip_match = re.search(r'\+CGPADDR:\s*\d+,"([^"]+)"', response)
                    if ip_match:
                        print(f"[+] IP地址: {ip_match.group(1)}")
                return True
            time.sleep(2)
        print("[-] 网络附着超时")
        return False

    def create_udp_socket(self):
        """创建UDP socket"""
        print("\n" + "="*50)
        print("测试6: 创建UDP Socket")
        print("="*50)

        # 清理已有socket
        print("[*] 清理已有socket连接...")
        self.send_at_command("AT+QICLOSE=0", timeout=5)
        time.sleep(1)
        start_time = time.time()
        while time.time() - start_time < 2:
            if self.ser.in_waiting > 0:
                line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                if line and self.verbose:
                    print(f"[DEBUG] 清理URC: {line}")
            else:
                break

        # 配置数据格式: 0=文本模式, 1=HEX模式
        # 注意: 某些BC260固件版本HEX模式有bug，这里使用文本模式更可靠
        print("[*] 配置UDP/IP参数 (文本发送模式)...")
        self.send_at_command('AT+QICFG="dataformat",0,0', "OK", timeout=3)

        # 打开UDP连接
        print(f"[*] 打开UDP连接到代理 {self.proxy_ip}:{self.proxy_port}...")
        connect_cmd = f'AT+QIOPEN=0,0,"UDP","{self.proxy_ip}",{self.proxy_port}'
        success, response = self.send_at_command(connect_cmd, "OK", timeout=15)

        if success:
            print("[*] 等待UDP socket就绪...")
            ready = False
            start_time = time.time()
            while time.time() - start_time < 20:
                if self.ser.in_waiting > 0:
                    data = self.ser.read(self.ser.in_waiting).decode('utf-8', errors='ignore')
                    for line in data.split('\r\n'):
                        line = line.strip()
                        if line == "+QIOPEN: 0,0":
                            ready = True
                            print("[+] UDP socket创建成功 (+QIOPEN: 0,0)")
                            break
                        elif line.startswith("+QIOPEN: 0,"):
                            print(f"[-] UDP创建失败: {line}")
                            return False
                    if ready:
                        break
                # 轮询QISTATE
                if int(time.time() - start_time) % 2 == 0 and (time.time() - start_time) > 0:
                    success, response = self.send_at_command("AT+QISTATE?", "OK", timeout=3)
                    if success:
                        state_match = re.search(r'\+QISTATE:\s*0,[^,]+,[^,]+,[^,]+,[^,]+,(\d+)', response)
                        if state_match:
                            state = int(state_match.group(1))
                            if state == 2:
                                ready = True
                                print(f"[+] UDP socket已就绪 (state={state})")
                                break
                    time.sleep(1)
                time.sleep(0.2)

            if ready:
                self.socket_created = True
                return True
            else:
                print("[-] UDP socket创建超时")
                return False
        else:
            print("[-] UDP socket创建失败")
            return False

    def close_socket(self):
        """关闭socket"""
        print("[*] 关闭socket (AT+QICLOSE=0)...")
        self.send_at_command("AT+QICLOSE=0", timeout=5)
        self.socket_created = False
        time.sleep(1)

    def build_udp_send_data(self, payload_bytes):
        """构造与 webhook_proxy.py 兼容的UDP发送内容"""
        payload_hex = payload_bytes.hex()
        parts = []

        if self.imei:
            parts.append(f"{self.imei}:")

        if self.udp_secret:
            parts.append(self.udp_secret)

        parts.append(payload_hex)
        return ''.join(parts)

    def send_udp_binary(self, payload_bytes):
        """
        通过UDP发送二进制数据

        注意: 当前已配置为HEX发送模式 (dataformat=1,1)，
        因此需要将字节数据编码为HEX字符串发送。
        """
        if not self.socket_created:
            print("[-] UDP socket未创建")
            return False

        print(f"\n[*] 准备发送UDP数据: {payload_bytes.hex()} ({len(payload_bytes)}字节)")

        # 文本模式下直接发送原始字节（但UDP套接字可能不支持二进制0x00）
        # 将数据转为可打印字符，或使用HEX字符串
        # 这里使用HEX字符串便于代理解析
        send_data = self.build_udp_send_data(payload_bytes)
        data_len = len(send_data)
        if self.udp_secret:
            print(f"[*] 已附加UDP密钥前缀: {self.udp_secret[:4]}...")
        if self.imei:
            print(f"[*] 已附加设备IMEI前缀: {self.imei}")

        # 步骤1: 发送 AT+QISEND，等待 ">" 提示
        prompt_received = False
        for attempt in range(2):
            self.ser.reset_input_buffer()
            send_cmd = f'AT+QISEND=0,{data_len}'
            print(f"[>] 发送: {send_cmd}")
            self.ser.write(f'{send_cmd}\r\n'.encode('utf-8'))
            self.ser.flush()

            start_time = time.time()
            while time.time() - start_time < 8:
                if self.ser.in_waiting > 0:
                    data = self.ser.read(self.ser.in_waiting).decode('utf-8', errors='ignore')
                    if '>' in data:
                        prompt_received = True
                        print("[+] 收到发送提示符 '>'")
                        break
                time.sleep(0.1)

            if prompt_received:
                break
            else:
                print(f"[-] 未收到提示符 (尝试 {attempt+1}/2)")
                if attempt == 0:
                    self.close_socket()
                    time.sleep(1)
                    if not self.create_udp_socket():
                        return False
                    time.sleep(2)
                    self.send_at_command("AT", "OK", timeout=3)

        if not prompt_received:
            print("[-] 发送失败: 未收到 '>' 提示符")
            self.close_socket()
            return False

        # 步骤2: 发送数据（不带换行，需要Ctrl-Z结束符）
        # BC260文档: 数据发送后需要Ctrl-Z(0x1A)作为结束
        print(f"[*] 发送数据: {send_data}")
        self.ser.write(send_data.encode('utf-8'))
        # 发送 Ctrl-Z (0x1A) 作为数据结束符
        print("[*] 发送 Ctrl-Z 结束符...")
        self.ser.write(b'\x1a')  
        self.ser.flush()

        # 步骤3: 等待 SEND OK (UDP模式下响应可能有延迟或格式不同)
        # 注意：UDP响应(ACK)可能在这个时间段到达，需要保存URC供后续处理
        print("[*] 等待发送完成...")
        start_time = time.time()
        send_ok = False
        response_buffer = ""
        
        while time.time() - start_time < 15:
            if self.ser.in_waiting > 0:
                # 读取所有可用数据，而不是逐行读取
                try:
                    data = self.ser.read(self.ser.in_waiting).decode('utf-8', errors='ignore')
                    response_buffer += data
                    # 按行分割并处理
                    lines = response_buffer.split('\r\n')
                    response_buffer = lines.pop() if lines else ""  # 保留未完成的最后一行
                    
                    for line in lines:
                        line = line.strip()
                        if not line:
                            continue
                        # 保存URC消息供后续处理
                        if line.startswith('+QIURC:'):
                            self._urc_buffer.append(line)
                            print(f"[<] URC(已缓存): {line}")
                            continue
                        print(f"[<] 响应: {line}")
                        if "SEND OK" in line:
                            send_ok = True
                            break
                        if "SEND FAIL" in line or "ERROR" in line:
                            print(f"[-] 发送失败: {line}")
                            self.close_socket()
                            return False
                    if send_ok:
                        break
                except Exception as e:
                    print(f"[-] 读取响应错误: {e}")
            time.sleep(0.1)

        # UDP不保证一定有SEND OK响应，如果收到了ACK也认为成功
        if send_ok:
            print("[+] UDP数据发送成功 (收到 SEND OK)")
        else:
            # 对于UDP，数据可能已经发送成功只是没收到SEND OK确认
            print("[*] 未收到SEND OK，等待2秒后检查状态...")
            time.sleep(2)
            # 检查 socket 状态
            self.send_at_command("AT+QISTATE?", timeout=3)
            # 尝试读取任何剩余的URC
            for _ in range(10):
                if self.ser.in_waiting > 0:
                    line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        print(f"[<] 额外响应: {line}")
                        if line.startswith('+QIURC:'):
                            self._urc_buffer.append(line)
                time.sleep(0.1)
        
        return True

    def _process_recv_urc(self, line, response_parts):
        """处理接收数据URC，返回True如果成功处理"""
        match = re.search(r'\+QIURC:\s*"recv",(\d+),(\d+)', line)
        if not match:
            return False
        
        recv_len = int(match.group(2))
        print(f"[*] 处理URC: recv_len={recv_len}")
        
        # 读取HEX格式的数据
        # BC260 HEX模式下，数据格式: +QIURC: "recv",0,12,"41434b3a303839323039"
        data_match = re.search(r'\+QIURC:\s*"recv",\d+,\d+,"([^"]*)"?', line)
        data_buffer = ""
        if data_match:
            data_buffer = data_match.group(1)
            print(f"[*] 从URC提取数据: {data_buffer}")
        else:
            # 数据可能在下一行
            print(f"[*] URC中无数据，尝试读取后续行...")

        # 继续读取后续数据行（最多500ms）
        data_wait_start = time.time()
        while time.time() - data_wait_start < 0.5:
            if self.ser and self.ser.in_waiting > 0:
                data_line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                print(f"[*] 读取数据行: {data_line}")
                if data_line == '"' or data_line.startswith('+QIURC:'):
                    break
                if data_line.endswith('"'):
                    data_buffer += data_line[:-1]
                    break
                data_buffer += data_line
            time.sleep(0.05)

        if not data_buffer:
            return False

        # HEX模式接收的数据需要解码
        try:
            decoded_bytes = bytes.fromhex(data_buffer)
            response_parts.append(decoded_bytes)
            print(f"[+] 收到UDP响应数据 (HEX解码): {decoded_bytes}")
            return True
        except ValueError as e:
            print(f"[-] HEX解码失败: {e}, 原始数据: {data_buffer}")
            response_parts.append(data_buffer.encode())
            print(f"[+] 收到UDP响应数据 (文本): {data_buffer}")
            return True

    def wait_for_udp_response(self, timeout_sec=15):
        """等待UDP响应 (ACK或其他数据)"""
        print(f"\n[*] 等待UDP响应 (最多{timeout_sec}秒)...")
        print("    说明: UDP不保证响应，代理可选发送ACK")
        print("    注意: 移动网络NAT可能导致响应延迟或丢失")
        start_time = time.time()
        response_parts = []
        
        # 首先处理缓存的URC消息（可能在send_udp_binary期间到达的ACK）
        if self._urc_buffer:
            print(f"[*] 处理缓存的 {len(self._urc_buffer)} 条URC消息...")
            for i, urc in enumerate(self._urc_buffer):
                print(f"    缓存[{i}]: {urc[:80]}...")
                if re.search(r'\+QIURC:\s*"recv"', urc):
                    if self._process_recv_urc(urc, response_parts):
                        print(f"[+] 从缓存中找到UDP响应")
            self._urc_buffer = []  # 清空已处理的缓存
            if response_parts:
                return True, response_parts

        while time.time() - start_time < timeout_sec:
            if self.ser.in_waiting > 0:
                line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                if not line:
                    continue
                print(f"[<] 接收: {line}")

                # 检查数据上报
                if re.search(r'\+QIURC:\s*"recv"', line):
                    self._process_recv_urc(line, response_parts)

                # 检查关闭URC
                if re.search(r'\+QIURC:\s*"closed"', line):
                    break
            time.sleep(0.1)

        if response_parts:
            print(f"[+] 共收到 {len(response_parts)} 个响应片段")
            return True, response_parts
        else:
            print("[*] 未收到UDP响应 (正常现象，UDP无连接不保证响应)")
            print("    提示: 移动网络NAT可能阻止了入站UDP，数据可能已送达")
            return False, []

    def run_udp_test(self, msg_type=1, water_status=0, flags=0, adc_value=2450):
        """运行UDP发送测试"""
        print("\n" + "="*60)
        print("BC260 NB-IoT UDP二进制通信测试")
        print("="*60)

        if not self.connect():
            return False

        results = {}
        try:
            results['basic_at'] = self.test_basic_at()
            if not results['basic_at']:
                return False

            results['module_info'] = self.test_module_info()
            results['sim_card'] = self.test_sim_card()
            if not results['sim_card']:
                return False

            results['signal_quality'] = self.test_signal_quality()
            results['network'] = self.test_network_attachment()

            if not results['network']:
                print("\n[-] 网络未附着，跳过UDP测试")
                return False

            # 创建UDP socket并发送数据
            results['udp_socket'] = self.create_udp_socket()
            if results['udp_socket']:
                payload = encode_payload(msg_type, water_status, flags, adc_value)
                print(f"\n[*] 测试载荷参数: type={msg_type}, water={water_status}, flags={flags}, adc={adc_value}")
                print(f"[*] 编码结果: {payload.hex()}")

                results['udp_send'] = self.send_udp_binary(payload)
                if results['udp_send']:
                    # 延长等待时间，移动网络NAT可能导致ACK延迟
                    self.wait_for_udp_response(timeout_sec=12)
                    
                    # 如果还没收到响应，尝试主动查询接收缓冲区
                    if not self._urc_buffer:
                        print("\n[*] 尝试主动查询接收缓冲区...")
                        self.send_at_command("AT+QIRD=0,0", timeout=3)
                self.close_socket()
        finally:
            self.disconnect()

        # 打印摘要
        print("\n" + "="*60)
        print("UDP测试摘要")
        print("="*60)
        test_names = {
            'basic_at': '基本AT通信',
            'module_info': '模块信息',
            'sim_card': 'SIM卡检测',
            'signal_quality': '信号质量',
            'network': '网络附着',
            'udp_socket': 'UDP Socket创建',
            'udp_send': 'UDP数据发送',
        }
        for key, name in test_names.items():
            if key in results:
                status = "[OK] 通过" if results[key] else "[FAIL] 失败"
                print(f"  {name}: {status}")
        print("="*60)

        return all(results.values())


def list_serial_ports():
    """列出可用串口"""
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("未找到可用串口")
        return
    print("\n可用串口:")
    for port in ports:
        print(f"  {port.device} - {port.description}")


def main():
    parser = argparse.ArgumentParser(
        description='BC260 NB-IoT UDP通信测试脚本',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  # 列出串口
  python bc260_test_udp.py --list

  # 自动加载配置并测试
  python bc260_test_udp.py --port COM3 --auto-config

  # 模拟有水状态
  python bc260_test_udp.py --port COM3 --auto-config --water 1 --adc 3000 --type 1

  # 发送心跳包
  python bc260_test_udp.py --port COM3 --auto-config --type 0 --adc 2450
        """
    )
    parser.add_argument('--port', default=DEFAULT_PORT, help=f'串口号 (默认: {DEFAULT_PORT})')
    parser.add_argument('--baudrate', type=int, default=DEFAULT_BAUDRATE, help=f'波特率 (默认: {DEFAULT_BAUDRATE})')
    parser.add_argument('--proxy-ip', default=DEFAULT_PROXY_IP, help=f'UDP代理IP (默认: {DEFAULT_PROXY_IP})')
    parser.add_argument('--proxy-port', type=int, default=DEFAULT_PROXY_PORT, help=f'UDP代理端口 (默认: {DEFAULT_PROXY_PORT}，与TCP代理共用)')
    parser.add_argument('--config', help='指定config.env路径')
    parser.add_argument('--auto-config', action='store_true', help='自动从config.env加载配置')
    parser.add_argument('--udp-secret',
                        help='UDP密钥（覆盖配置文件中的UDP_SECRET_KEY，多个密钥时默认取第一个）')
    parser.add_argument('--verbose', action='store_true', help='详细调试模式')
    parser.add_argument('--list', action='store_true', help='列出可用串口')
    parser.add_argument('--water', type=int, default=0, choices=[0, 1], help='水浸状态 (0=无水, 1=有水)')
    parser.add_argument('--adc', type=int, default=2450, help='ADC原始值 (默认: 2450)')
    parser.add_argument('--type', type=int, default=1, choices=[0, 1, 2, 3, 4, 5, 6, 7], help='消息类型 (0-3=浸水, 4-7=超声波)')
    parser.add_argument('--flags', type=int, default=0, help='标志位 (bit0=低电量, bit1=传感器故障)')

    args = parser.parse_args()

    if args.list:
        list_serial_ports()
        return

    try:
        import serial
    except ImportError:
        print("[-] 请先安装 pyserial: pip install pyserial")
        sys.exit(1)

    proxy_ip = args.proxy_ip
    proxy_port = args.proxy_port
    udp_secret = args.udp_secret or ''

    if args.auto_config or args.config:
        config = load_config_from_env(args.config)
        if config:
            # UDP代理配置优先使用 BC260_PROXY_IP 但端口改为默认UDP端口
            # 如果配置中有 UDP_PROXY_PORT 则使用它
            config_ip = config.get('BC260_PROXY_IP', '').strip()
            if config_ip:
                proxy_ip = config_ip
                print(f"[+] 从配置加载代理IP (BC260_PROXY_IP): {proxy_ip}")

            # UDP与TCP共用同一端口（协议不同，互不冲突）
            tcp_port_str = config.get('BC260_PROXY_PORT', '').strip()
            if tcp_port_str:
                try:
                    proxy_port = int(tcp_port_str)
                    print(f"[+] 从配置加载代理端口 (BC260_PROXY_PORT): {proxy_port} (UDP与TCP共用)")
                except ValueError:
                    pass
            if not udp_secret:
                udp_secret = get_first_secret_key(config)
                if udp_secret:
                    print(f"[+] 从配置加载UDP密钥 (UDP_SECRET_KEY): {udp_secret[:4]}...")

    print("\n" + "-"*50)
    print("UDP测试配置:")
    print(f"  串口: {args.port}")
    print(f"  波特率: {args.baudrate}")
    print(f"  UDP代理: {proxy_ip}:{proxy_port}")
    print(f"  UDP密钥: {'已配置' if udp_secret else '未配置'}")
    print(f"  消息类型: {args.type}, 水浸: {args.water}, ADC: {args.adc}, flags: {args.flags}")
    print("-"*50 + "\n")

    tester = BC260UDPTester(
        port=args.port,
        baudrate=args.baudrate,
        proxy_ip=proxy_ip,
        proxy_port=proxy_port,
        udp_secret=udp_secret,
        verbose=args.verbose
    )

    try:
        success = tester.run_udp_test(
            msg_type=args.type,
            water_status=args.water,
            flags=args.flags,
            adc_value=args.adc
        )
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        print("\n[!] 用户中断测试")
        tester.disconnect()
        sys.exit(1)


if __name__ == '__main__':
    main()
