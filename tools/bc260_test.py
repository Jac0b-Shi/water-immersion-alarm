#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
BC260 NB-IoT 模块测试脚本
功能：
1. 测试BC260基本AT指令通信
2. 检查SIM卡和网络附着状态
3. 通过HTTP代理发送测试消息到企业微信webhook

重要提示:
---------
BC260 NB-IoT模块不支持HTTPS协议，只支持HTTP。
企业微信webhook是HTTPS的，因此需要通过HTTP代理服务器转发请求。

使用方式：
    # 基本AT指令测试（不需要代理）
    python bc260_test.py --port COM3
    
    # 自动从config.env加载配置（推荐）
    python bc260_test.py --port COM3 --auto-config
    
    # 详细调试模式
    python bc260_test.py --port COM3 --auto-config --verbose
    
    # 手动指定配置
    python bc260_test.py --port COM3 --webhook "YOUR_WEBHOOK_URL" --proxy-ip "YOUR_PROXY_IP" --proxy-port 8080

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
import json
import binascii
import re
import os

# 默认配置
DEFAULT_PORT = "COM3"
DEFAULT_BAUDRATE = 9600
DEFAULT_TIMEOUT = 5

# HTTP代理配置（用于转发HTTPS请求）
# BC260不支持HTTPS，需要通过支持CONNECT方法的HTTP代理转发
DEFAULT_PROXY_IP = "127.0.0.1"
DEFAULT_PROXY_PORT = 8080


def load_config_from_env(config_path=None):
    """
    从config.env文件加载配置
    
    Returns:
        dict: 配置字典
    """
    if config_path is None:
        # 尝试在当前目录和上级目录查找config.env
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
                # 跳过空行和注释行
                if not line or line.startswith('#'):
                    continue
                # 解析键值对
                if '=' in line:
                    key, value = line.split('=', 1)
                    key = key.strip()
                    value = value.strip()
                    config[key] = value
        print(f"[+] 配置文件加载成功")
    except Exception as e:
        print(f"[-] 加载配置文件失败: {e}")
    
    return config


class BC260Tester:
    """BC260 NB-IoT模块测试类"""
    
    def __init__(self, port, baudrate=115200, timeout=5, webhook_url=None, proxy_ip=None, proxy_port=None, verbose=False):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.webhook_url = webhook_url
        self.proxy_ip = proxy_ip or DEFAULT_PROXY_IP
        self.proxy_port = proxy_port or DEFAULT_PROXY_PORT
        self.verbose = verbose
        self.ser = None
        self.network_attached = False
        self.socket_created = False
        
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
            # 清空缓冲区
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
            print(f"[+] 串口连接成功: {self.port}")
            if self.verbose:
                print(f"[DEBUG] 串口参数: baudrate={self.baudrate}, timeout={self.timeout}")
            return True
        except Exception as e:
            print(f"[-] 串口连接失败: {e}")
            return False
    
    def disconnect(self):
        """断开串口连接"""
        # 确保关闭socket
        if self.socket_created:
            print("[*] 关闭socket...")
            self.send_at_command("AT+QICLOSE=0", timeout=5)
            self.socket_created = False
            
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("[*] 串口已断开")
    
    def send_at_command(self, cmd, expected_response="OK", timeout=None):
        """
        发送AT指令并等待响应
        
        Args:
            cmd: AT指令
            expected_response: 期望的响应内容
            timeout: 超时时间（秒）
        
        Returns:
            (success, response) 元组
        """
        if timeout is None:
            timeout = self.timeout
            
        if not self.ser or not self.ser.is_open:
            print("[-] 串口未连接")
            return False, ""
        
        # 清空接收缓冲区
        self.ser.reset_input_buffer()
        
        # 发送指令
        full_cmd = f"{cmd}\r\n"
        print(f"[>] 发送: {cmd}")
        if self.verbose:
            hex_data = binascii.hexlify(full_cmd.encode()).decode()
            print(f"[DEBUG] 发送数据(HEX): {hex_data}")
        
        self.ser.write(full_cmd.encode('utf-8'))
        self.ser.flush()
        
        # 等待响应
        start_time = time.time()
        response_lines = []
        raw_bytes = b""
        
        while time.time() - start_time < timeout:
            if self.ser.in_waiting > 0:
                try:
                    # 读取原始字节
                    chunk = self.ser.readline()
                    raw_bytes += chunk
                    line = chunk.decode('utf-8', errors='ignore').strip()
                    
                    if line:
                        print(f"[<] 接收: {line}")
                        if self.verbose:
                            hex_chunk = binascii.hexlify(chunk).decode()
                            print(f"[DEBUG] 接收数据(HEX): {hex_chunk}")
                        response_lines.append(line)
                        
                        # 检查是否收到期望响应
                        if expected_response and expected_response in line:
                            # 继续读取剩余数据
                            time.sleep(0.3)
                            while self.ser.in_waiting > 0:
                                try:
                                    extra_chunk = self.ser.readline()
                                    raw_bytes += extra_chunk
                                    extra = extra_chunk.decode('utf-8', errors='ignore').strip()
                                    if extra:
                                        print(f"[<] 接收: {extra}")
                                        if self.verbose:
                                            hex_extra = binascii.hexlify(extra_chunk).decode()
                                            print(f"[DEBUG] 接收数据(HEX): {hex_extra}")
                                        response_lines.append(extra)
                                except:
                                    break
                            return True, "\n".join(response_lines)
                        
                        # 检查错误响应
                        if "ERROR" in line or "FAIL" in line:
                            if self.verbose:
                                print(f"[DEBUG] 检测到错误响应")
                            return False, "\n".join(response_lines)
                            
                except Exception as e:
                    print(f"[-] 接收数据错误: {e}")
                    if self.verbose:
                        import traceback
                        traceback.print_exc()
                    break
            
            time.sleep(0.1)
        
        response = "\n".join(response_lines)
        
        if self.verbose:
            print(f"[DEBUG] 完整响应({len(response_lines)}行):")
            for i, line in enumerate(response_lines):
                print(f"  [{i+1}] {line}")
        
        # 如果没有明确的期望响应，只要有响应就返回成功
        if not expected_response and response_lines:
            return True, response
            
        # 最后检查整个响应中是否包含期望字符串
        if expected_response and expected_response in response:
            return True, response
            
        print(f"[-] 等待响应超时或未收到期望响应: {expected_response}")
        if self.verbose:
            print(f"[DEBUG] 已接收内容: {response[:500] if response else '(空)'}")
        return False, response
    
    def test_basic_at(self):
        """测试基本AT通信"""
        print("\n" + "="*50)
        print("测试1: 基本AT通信")
        print("="*50)
        
        # 尝试多次发送AT
        for i in range(3):
            success, response = self.send_at_command("AT", "OK", timeout=2)
            if success:
                print("[+] AT通信测试通过!")
                return True
            time.sleep(0.5)
        
        print("[-] AT通信测试失败，请检查:")
        print("    1. BC260是否正确连接并上电")
        print("    2. 波特率是否正确（BC260Y-CN默认115200）")
        print("    3. 接线是否正确（TX接RX，RX接TX，GND接GND）")
        print("    4. BC260模块是否正常工作")
        return False
    
    def test_module_info(self):
        """测试获取模块信息"""
        print("\n" + "="*50)
        print("测试2: 获取模块信息")
        print("="*50)
        
        # 获取模块型号
        success, response = self.send_at_command("ATI", "OK", timeout=3)
        if success:
            print("[+] 模块信息获取成功")
            # 尝试解析模块型号
            for line in response.split('\n'):
                if 'Quectel' in line or 'BC260' in line:
                    print(f"[+] 模块型号: {line.strip()}")
        else:
            print("[-] 获取模块信息失败")
        
        # 获取IMEI
        success, response = self.send_at_command("AT+CGSN=1", "OK", timeout=3)
        if success:
            match = re.search(r'\+CGSN:\s*(\d+)', response)
            if match:
                print(f"[+] IMEI: {match.group(1)}")
        
        # 关闭回显
        self.send_at_command("ATE0", "OK", timeout=2)
        
        return True
    
    def test_sim_card(self):
        """测试SIM卡状态"""
        print("\n" + "="*50)
        print("测试3: SIM卡检测")
        print("="*50)
        
        success, response = self.send_at_command("AT+CIMI", "OK", timeout=5)
        if success:
            # 尝试提取IMSI
            lines = response.split('\n')
            for line in lines:
                line = line.strip()
                if line and line not in ['AT+CIMI', 'OK'] and not line.startswith('+') and line.isdigit():
                    print(f"[+] SIM卡检测成功, IMSI: {line}")
                    return True
            print("[+] SIM卡检测成功")
            return True
        else:
            print("[-] SIM卡检测失败，请检查:")
            print("    1. SIM卡是否正确插入")
            print("    2. SIM卡是否已开通NB-IoT服务（非普通4G服务）")
            print("    3. SIM卡是否欠费")
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
                
                # 信号强度解释
                if rssi == 0:
                    signal_desc = "-113 dBm或更低 (信号极弱)"
                elif rssi == 1:
                    signal_desc = "-111 dBm (信号极弱)"
                elif 2 <= rssi <= 30:
                    dbm = -113 + 2 * rssi
                    if rssi >= 20:
                        signal_desc = f"{dbm} dBm (信号强)"
                    elif rssi >= 10:
                        signal_desc = f"{dbm} dBm (信号中)"
                    else:
                        signal_desc = f"{dbm} dBm (信号弱)"
                elif rssi == 31:
                    signal_desc = "-51 dBm或更高 (信号极强)"
                else:
                    signal_desc = "未知或不可检测"
                
                print(f"[+] 信号强度: {rssi} ({signal_desc})")
                print(f"[+] 误码率: {ber} (0=最好, 7=最差)")
                return True
        
        print("[-] 信号质量检查失败")
        return False
    
    def test_network_attachment(self):
        """测试网络附着状态"""
        print("\n" + "="*50)
        print("测试5: 网络附着检查")
        print("="*50)
        
        # 开启全功能模式
        print("[*] 开启全功能模式...")
        self.send_at_command("AT+CFUN=1", "OK", timeout=5)
        
        # 检查功能模式
        success, response = self.send_at_command("AT+CFUN?", "OK", timeout=3)
        if success and "+CFUN: 1" in response:
            print("[+] 功能模式: 全功能模式")
        
        # 设置中国移动NB-IoT APN (BC260Y-CN 使用 contextID 1)
        print("[*] 设置APN...")
        self.send_at_command('AT+CGDCONT=1,"IP","cmnbiot"', "OK", timeout=5)
        
        # 检查网络注册状态
        print("[*] 检查网络注册状态...")
        max_retry = 30
        for i in range(max_retry):
            success, response = self.send_at_command("AT+CEREG?", "OK", timeout=5)
            if success:
                # 解析 +CEREG: <n>,<stat>
                import re
                match = re.search(r'\+CEREG:\s*\d+,(\d+)', response)
                if match:
                    stat = int(match.group(1))
                    if stat == 1:
                        print(f"[+] 网络已注册到归属网络 (状态: {stat})")
                        break
                    elif stat == 5:
                        print(f"[+] 网络已注册到漫游网络 (状态: {stat})")
                        break
                    elif stat == 0:
                        if (i + 1) % 5 == 0:
                            print(f"[*] 未注册网络，继续等待... ({i+1}/{max_retry})")
                    else:
                        print(f"[*] 网络状态: {stat} (0=未注册, 1=归属网络, 5=漫游)")
                time.sleep(2)
            else:
                time.sleep(1)
        
        # 尝试附着网络
        print("[*] 尝试附着网络...")
        self.send_at_command("AT+CGATT=1", "OK", timeout=10)
        
        # 检查附着状态
        print("[*] 检查网络附着状态（最多等待60秒）...")
        for i in range(max_retry):
            success, response = self.send_at_command("AT+CGATT?", "OK", timeout=5)
            if success:
                if "+CGATT: 1" in response:
                    print(f"[+] 网络已附着 (耗时{i+1}次检查)")
                    self.network_attached = True
                    
                    # 获取IP地址
                    success, response = self.send_at_command("AT+CGPADDR", "OK", timeout=3)
                    if success:
                        ip_match = re.search(r'\+CGPADDR:\s*\d+,"([^"]+)"', response)
                        if ip_match:
                            print(f"[+] IP地址: {ip_match.group(1)}")
                    return True
                elif "+CGATT: 0" in response:
                    if (i + 1) % 5 == 0:
                        print(f"[*] 未附着网络，继续等待... ({i+1}/{max_retry})")
                    time.sleep(2)
                else:
                    time.sleep(1)
            else:
                time.sleep(1)
        
        print("[-] 网络附着超时")
        print("    可能原因:")
        print("    1. SIM卡未开通NB-IoT服务（需联系运营商开通）")
        print("    2. 当前位置NB-IoT信号覆盖不佳")
        print("    3. 运营商网络问题")
        print("    4. APN设置不正确（中国移动NB-IoT使用 cmnbiot 或 cmiot）")
        return False
    
    def create_socket(self):
        """创建TCP socket - 使用BC260Y-CN标准TCP/IP指令"""
        print("[*] 创建TCP socket...")
        
        # 先关闭可能存在的socket连接，避免状态混乱
        print("[*] 清理已有socket连接...")
        self.send_at_command("AT+QICLOSE=0", "OK", timeout=5)
        # 等待并消费可能的 CLOSE OK / +QIURC URC
        time.sleep(1)
        start_time = time.time()
        while time.time() - start_time < 2:
            if self.ser.in_waiting > 0:
                try:
                    line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                    if line and self.verbose:
                        print(f"[DEBUG] 清理URC: {line}")
                except:
                    pass
            else:
                break
        
        # 配置数据格式为文本字符串
        print("[*] 配置TCP/IP参数...")
        self.send_at_command('AT+QICFG="dataformat",0,0', "OK", timeout=3)
        
        # 打开TCP连接 (contextID 必须是 0)
        # 格式: AT+QIOPEN=<Context ID>,<ConnectID>,<service_type>,<host>,<remote_port>
        print(f"[*] 打开TCP连接到代理 {self.proxy_ip}:{self.proxy_port}...")
        connect_cmd = f'AT+QIOPEN=0,0,"TCP","{self.proxy_ip}",{self.proxy_port}'
        success, response = self.send_at_command(connect_cmd, "OK", timeout=15)
        
        if success:
            # AT+QIOPEN 返回 OK 只表示开始建立连接，需要等待 +QIOPEN: 0,0 URC
            # 或轮询 QISTATE 直到 socket_state=2 (Connected)
            print("[*] 等待TCP连接建立完成...")
            connect_success = False
            start_time = time.time()
            while time.time() - start_time < 30:
                # 先检查是否有 +QIOPEN URC
                if self.ser.in_waiting > 0:
                    try:
                        lines = self.ser.read(self.ser.in_waiting).decode('utf-8', errors='ignore').split('\r\n')
                        for line in lines:
                            line = line.strip()
                            if not line:
                                continue
                            if self.verbose:
                                print(f"[DEBUG] URC: {line}")
                            if line == "+QIOPEN: 0,0":
                                connect_success = True
                                print("[+] 收到连接成功URC (+QIOPEN: 0,0)")
                                break
                            elif line.startswith("+QIOPEN: 0,"):
                                err_code = line.split(",")[1] if "," in line else "?"
                                print(f"[-] 连接失败URC: {line} (错误码 {err_code})")
                                return False
                        if connect_success:
                            break
                    except Exception as e:
                        if self.verbose:
                            print(f"[DEBUG] 读取URC错误: {e}")
                
                # 每2秒轮询一次 QISTATE
                if int(time.time() - start_time) % 2 == 0 and (time.time() - start_time) > 0:
                    success, response = self.send_at_command("AT+QISTATE?", "OK", timeout=3)
                    if success:
                        # 解析 socket_state: +QISTATE: 0,"TCP",...,2 表示 Connected
                        state_match = re.search(r'\+QISTATE:\s*0,[^,]+,[^,]+,[^,]+,[^,]+,(\d+)', response)
                        if state_match:
                            state = int(state_match.group(1))
                            if state == 2:
                                connect_success = True
                                print(f"[+] QISTATE 确认连接已建立 (state={state})")
                                break
                            elif state == 1:
                                if self.verbose:
                                    print(f"[DEBUG] 连接建立中 (state={state})...")
                            else:
                                print(f"[-] 连接状态异常 (state={state})")
                                return False
                    # 避免同一秒内多次查询
                    time.sleep(1)
                
                time.sleep(0.2)
            
            if connect_success:
                self.socket_created = True
                return True
            else:
                print("[-] Socket连接建立超时 (30秒)")
                return False
        else:
            print("[-] Socket创建失败")
            print("    可能原因:")
            print("    1. 网络未附着")
            print("    2. 代理服务器不可达")
            print("    3. Socket资源已满(最多支持8个连接)")
        return False
    
    def close_socket(self):
        """关闭socket - 使用BC260Y-CN标准TCP/IP指令"""
        print("[*] 关闭socket (AT+QICLOSE=0)...")
        success, response = self.send_at_command("AT+QICLOSE=0", "OK", timeout=5)
        if success:
            print("[+] Socket已关闭")
        else:
            print("[-] 关闭socket失败或socket已关闭")
        self.socket_created = False
        # 等待并消费可能的 CLOSE OK / +QIURC URC，确保资源完全释放
        time.sleep(2)
        start_time = time.time()
        while time.time() - start_time < 2:
            if self.ser.in_waiting > 0:
                try:
                    line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                    if line and self.verbose:
                        print(f"[DEBUG] 关闭URC: {line}")
                except:
                    pass
            else:
                break
    
    def send_http_via_proxy(self, host, port, path, headers, body):
        """
        通过应用层HTTP代理发送请求
        
        这种代理模式适用于 webhook_proxy.py 类型的代理服务器：
        - BC260 直接发送 HTTP POST 请求到代理服务器
        - 代理服务器负责将请求转发到 HTTPS 目标
        - 代理服务器返回响应给 BC260
        
        注意: 这是应用层代理，不是CONNECT隧道代理
              请求直接发送到代理，代理转发到目标HTTPS
        """
        print(f"\n[*] 通过应用层代理发送请求")
        print(f"[*] 代理服务器: {self.proxy_ip}:{self.proxy_port}")
        print(f"[*] 最终目标: {host}:{port}{path}")
        print(f"[*] 代理类型: 应用层代理 (直接转发)")
        
        # 创建socket并连接到代理服务器
        if not self.create_socket():
            print("[-] 创建socket失败，无法继续")
            return False
        
        # 构建HTTP请求（应用层代理模式）
        # 路径必须包含完整的请求路径（包括查询参数）
        request_lines = [f"POST {path} HTTP/1.1"]
        request_lines.append(f"Host: {host}")  # Host头是最终目标服务器
        
        for key, value in headers.items():
            request_lines.append(f"{key}: {value}")
        
        body_bytes = body.encode('utf-8') if isinstance(body, str) else body
        request_lines.append(f"Content-Length: {len(body_bytes)}")
        request_lines.append("")
        request_lines.append("")
        
        # 构建完整的HTTP请求字节流
        http_request_str = "\r\n".join(request_lines)
        http_request_bytes = http_request_str.encode('utf-8') + body_bytes
        http_request = http_request_bytes.decode('utf-8', errors='ignore')  # 用于显示
        data_to_send = http_request_bytes  # 实际发送的字节
        
        print(f"\n[*] HTTP请求内容:")
        print("-" * 50)
        print(http_request[:800])
        print("-" * 50)
        if len(http_request) > 800:
            print(f"... (还有 {len(http_request) - 800} 字节)")
        
        # 使用AT+QISEND发送数据 (交互式模式)
        # BC260Y-CN 不支持 QISENDEX，必须使用 QISEND
        max_send_len = 1024
        actual_data_len = len(data_to_send)
        if actual_data_len > max_send_len:
            print(f"[-] 请求数据过长 ({actual_data_len} > {max_send_len})，需要截断")
            data_to_send = data_to_send[:max_send_len]
            actual_data_len = len(data_to_send)
        
        print(f"\n[*] 发送HTTP请求...")
        print(f"    实际数据长度: {actual_data_len} 字节")
        
        # 步骤1: 发送 AT+QISEND 命令，等待 ">" 提示
        # 增加重试机制：如果第一次没收到 >，可能是模块刚从深睡眠唤醒，重新创建 socket 再试
        prompt_received = False
        for attempt in range(2):
            self.ser.reset_input_buffer()
            self.ser.write(f'AT+QISEND=0,{actual_data_len}\r\n'.encode('utf-8'))
            self.ser.flush()
            if self.verbose:
                print(f"[*] AT指令: AT+QISEND=0,{actual_data_len} (尝试 {attempt+1}/2)")
            
            start_time = time.time()
            while time.time() - start_time < 8:
                if self.ser.in_waiting > 0:
                    data = self.ser.read(self.ser.in_waiting).decode('utf-8', errors='ignore')
                    if self.verbose and data:
                        print(f"[DEBUG] QISEND 响应片段: {repr(data)}")
                    if '>' in data:
                        prompt_received = True
                        if self.verbose:
                            print(f"[DEBUG] 收到发送提示符 '>'")
                        break
                time.sleep(0.1)
            
            if prompt_received:
                break
            else:
                print(f"[-] 未收到发送提示符 '>' (尝试 {attempt+1}/2)")
                if attempt == 0:
                    print("[*] 尝试重新创建 socket 连接...")
                    self.close_socket()
                    time.sleep(1)
                    if not self.create_socket():
                        print("[-] 重新创建 socket 失败")
                        return False
                    # 重新创建连接后，给模块一点时间恢复
                    time.sleep(2)
                    # 发送一个空 AT 确认模块清醒
                    self.send_at_command("AT", "OK", timeout=3)
        
        if not prompt_received:
            print("[-] 两次尝试均未收到发送提示符 '>'，发送失败")
            self.close_socket()
            return False
        
        # 步骤2: 发送实际数据（字节流，不带换行）
        if self.verbose:
            print(f"[DEBUG] 发送HTTP数据 ({actual_data_len} 字节)")
        self.ser.write(data_to_send)
        self.ser.flush()
        
        # 步骤3: 等待 SEND OK 或 SEND FAIL
        print("[*] 等待发送完成...")
        start_time = time.time()
        send_ok = False
        while time.time() - start_time < 15:
            if self.ser.in_waiting > 0:
                try:
                    line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        if self.verbose:
                            print(f"[<] 响应: {line}")
                        if "SEND OK" in line:
                            send_ok = True
                            break
                        if "SEND FAIL" in line or "ERROR" in line:
                            print(f"[-] 发送失败: {line}")
                            self.close_socket()
                            return False
                except:
                    pass
            time.sleep(0.1)
        
        if not send_ok:
            print("[-] HTTP请求发送超时（未收到SEND OK）")
            self.close_socket()
            return False
        
        print("[+] HTTP请求发送成功")
        
        # 等待服务器响应
        time.sleep(3)
        
        # 在直吐模式下，数据会通过+QIURC主动上报
        # 等待接收数据上报
        print(f"\n[*] 等待接收响应数据...")
        print("    说明: 在直吐模式下，模块会主动上报+QIURC\"recv\" URC")
        
        # 监听一段时间接收数据
        start_time = time.time()
        http_response_parts = []  # 存储分段数据
        response_received = False
        pending_line = None  # 用于保存需要外层循环处理的行
        
        while time.time() - start_time < 15:
            if pending_line:
                line = pending_line
                pending_line = None
            elif self.ser.in_waiting > 0:
                try:
                    line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                except Exception as e:
                    if self.verbose:
                        print(f"[DEBUG] 接收数据错误: {e}")
                    continue
            else:
                time.sleep(0.1)
                continue
            
            if not line:
                time.sleep(0.1)
                continue
            
            if self.verbose:
                print(f"[<] 接收: {line}")
            
            # 检查是否是数据上报
            if re.search(r'\+QIURC:\s*"recv"', line):
                match = re.search(r'\+QIURC:\s*"recv",(\d+),(\d+)', line)
                if match:
                    recv_len = int(match.group(2))
                    
                    # 提取当前行中已有的数据（从第一个数据引号后开始）
                    data_match = re.search(r'\+QIURC:\s*"recv",\d+,\d+,"(.*)', line)
                    if data_match:
                        data_buffer = data_match.group(1)
                        # 如果末尾有结束引号，去掉它
                        if data_buffer.endswith('"'):
                            data_buffer = data_buffer[:-1]
                        # 补充被readline截断的换行符
                        data_buffer += "\r\n"
                    else:
                        data_buffer = ""
                    
                    # 继续读取后续数据行，直到遇到结束标志
                    data_wait_start = time.time()
                    while time.time() - data_wait_start < 5:
                        if self.ser.in_waiting > 0:
                            try:
                                data_line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                                if data_line:
                                    if self.verbose:
                                        print(f"[<] 接收: {data_line}")
                                    
                                    # 结束引号，表示数据结束
                                    if data_line == '"':
                                        break
                                    
                                    # 下一个URC，保存给外层循环处理
                                    if re.search(r'\+QIURC:', data_line):
                                        pending_line = data_line
                                        break
                                    
                                    # 数据末尾和下一个URC混在一起的情况
                                    # 例如: v",0,27,"{\"errcode\":0,\"errmsg\":\"ok\"}"
                                    if '",0,' in data_line or '",1,' in data_line or '",2,' in data_line:
                                        parts = data_line.split('",')
                                        if parts[0]:
                                            data_buffer += parts[0]
                                        break
                                    
                                    data_buffer += data_line + "\r\n"
                            except Exception as e:
                                if self.verbose:
                                    print(f"[DEBUG] 读取数据行错误: {e}")
                        time.sleep(0.1)
                    
                    http_response_parts.append(data_buffer)
                    response_received = True
                    print(f"[+] 收到响应片段: {len(data_buffer)} 字节")
                    continue
            
            # 检查连接是否被关闭
            if re.search(r'\+QIURC:\s*"closed"', line):
                if self.verbose:
                    print("[DEBUG] 连接被关闭")
                break
        
        # 合并所有响应片段
        http_response = "".join(http_response_parts)
        
        if not response_received:
            print("[-] 未收到响应数据")
        else:
            print(f"[+] 共收到响应数据: {len(http_response)} 字节")
            
        success = response_received
        
        print(f"\n[*] 关闭socket...")
        self.close_socket()
        
        # 解析响应
        if response_received and http_response:
            print(f"\n[*] HTTP响应:")
            print("-" * 50)
            print(http_response[:1000] if http_response else "(无数据)")
            print("-" * 50)
            if len(http_response) > 1000:
                print(f"... (还有 {len(http_response) - 1000} 字节)")
            
            # 检查HTTP状态码
            if "HTTP/1.0 200" in http_response or "HTTP/1.1 200" in http_response:
                print("\n[+] HTTP 200 OK - 请求成功")
            
            # 尝试解析JSON响应体 (企业微信返回格式: {"errcode":0,"errmsg":"ok"})
            json_match = re.search(r'\{[^}]*\}', http_response)
            if json_match:
                try:
                    json_data = json.loads(json_match.group())
                    if json_data.get('errcode') == 0:
                        print("[+] 企业微信返回成功!")
                        return True
                    else:
                        print(f"[-] 企业微信返回错误: {json_data}")
                        return False
                except json.JSONDecodeError:
                    if self.verbose:
                        print(f"[DEBUG] JSON解析失败: {json_match.group()}")
            
            # 如果没有明确的错误，也算成功（可能数据被截断）
            if "errcode" in http_response and "0" in http_response:
                print("[+] 检测到成功响应")
                return True
                
            print("[-] 无法解析HTTP响应")
            return False
        else:
            print("[-] 未收到响应或响应超时")
            print("    可能原因:")
            print("    1. 代理服务器未响应")
            print("    2. 网络超时")
            print("    3. 请求被拒绝")
            return False
    
    def send_wechat_webhook(self, message):
        """发送企业微信webhook消息"""
        if not self.webhook_url:
            print("[-] 未提供webhook地址")
            return False
        
        print("\n" + "="*50)
        print("测试6: 发送企业微信Webhook消息")
        print("="*50)
        
        if not self.network_attached:
            print("[-] 网络未附着，无法发送消息")
            return False
        
        # 解析webhook URL
        if self.webhook_url.startswith("https://"):
            url = self.webhook_url[8:]  # 去掉 https://
            is_https = True
        elif self.webhook_url.startswith("http://"):
            url = self.webhook_url[7:]  # 去掉 http://
            is_https = False
        else:
            print("[-] 不支持的URL格式")
            return False
        
        # 分离host和path
        if "/" in url:
            host, path = url.split("/", 1)
            path = "/" + path
        else:
            host = url
            path = "/"
        
        # 分离host和port
        if ":" in host:
            host, port_str = host.split(":")
            port = int(port_str)
        else:
            port = 443 if is_https else 80
        
        print(f"[*] Webhook目标: {host}:{port}{path}")
        print(f"[*] 代理配置: {self.proxy_ip}:{self.proxy_port}")
        print("[*] 提示: 使用应用层代理模式，代理将负责HTTPS转发")
        print("\n" + "-"*50)
        
        # 构建消息体
        payload = {
            "msgtype": "text",
            "text": {
                "content": message
            }
        }
        body = json.dumps(payload, ensure_ascii=False)
        
        headers = {
            "Content-Type": "application/json"
        }
        
        # 使用应用层代理发送请求
        return self.send_http_via_proxy(host, port, path, headers, body)
    
    def run_all_tests(self):
        """运行所有测试"""
        print("\n" + "="*60)
        print("BC260 NB-IoT 模块全面测试")
        print("="*60)
        print(f"串口: {self.port}")
        print(f"波特率: {self.baudrate}")
        if self.webhook_url:
            print(f"Webhook: {self.webhook_url[:50]}...")
        print(f"详细模式: {'是' if self.verbose else '否'}")
        print("="*60)
        
        # 连接串口
        if not self.connect():
            return False
        
        results = {}
        
        try:
            # 测试1: 基本AT通信
            results['basic_at'] = self.test_basic_at()
            if not results['basic_at']:
                print("\n[-] 基本AT通信失败，停止后续测试")
                return False
            
            # 测试2: 模块信息
            results['module_info'] = self.test_module_info()
            
            # 测试3: SIM卡检测
            results['sim_card'] = self.test_sim_card()
            if not results['sim_card']:
                print("\n[-] SIM卡检测失败，停止后续测试")
                return False
            
            # 测试4: 信号质量
            results['signal_quality'] = self.test_signal_quality()
            
            # 测试5: 网络附着
            results['network'] = self.test_network_attachment()
            
            # 测试6: 发送Webhook消息（如果提供了webhook地址）
            if self.webhook_url:
                test_message = (
                    f"【BC260测试消息】\n"
                    f"设备: 水浸报警器测试\n"
                    f"代理服务器: {self.proxy_ip}:{self.proxy_port}\n"
                    f"时间: {time.strftime('%Y-%m-%d %H:%M:%S')}\n"
                    f"状态: 测试通过，通信正常"
                )
                results['webhook'] = self.send_wechat_webhook(test_message)
        finally:
            # 确保断开连接
            self.disconnect()
        
        # 打印测试摘要
        print("\n" + "="*60)
        print("测试摘要")
        print("="*60)
        test_names = {
            'basic_at': '基本AT通信',
            'module_info': '模块信息',
            'sim_card': 'SIM卡检测',
            'signal_quality': '信号质量',
            'network': '网络附着',
            'webhook': '发送消息'
        }
        for key, name in test_names.items():
            if key in results:
                status = "[OK] 通过" if results[key] else "[FAIL] 失败"
                print(f"  {name}: {status}")
        print("="*60)
        
        return all(results.values())


def get_webhook_url_from_config(config):
    """从配置构建webhook URL"""
    webhook_key = config.get('WEBHOOK_KEY', '').strip()
    if webhook_key:
        return f"https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key={webhook_key}"
    return None


def get_bc260_proxy_from_config(config):
    """从配置获取BC260代理设置"""
    proxy_ip = config.get('BC260_PROXY_IP', '').strip()
    proxy_port_str = config.get('BC260_PROXY_PORT', '').strip()
    
    proxy_port = DEFAULT_PROXY_PORT
    if proxy_port_str:
        try:
            proxy_port = int(proxy_port_str)
        except ValueError:
            pass
    
    return proxy_ip or DEFAULT_PROXY_IP, proxy_port


def list_serial_ports():
    """列出可用串口"""
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("未找到可用串口")
        return
    
    print("\n可用串口:")
    for port in ports:
        print(f"  {port.device} - {port.description}")


def print_proxy_help():
    """打印代理配置帮助"""
    help_text = """
关于HTTP代理的说明:
-------------------
BC260 NB-IoT模块不支持HTTPS协议，只支持HTTP。
企业微信webhook是HTTPS的，因此需要HTTP代理服务器转发。

代理服务器选项:
1. 自建代理: 使用squid、nginx或Python脚本搭建支持CONNECT的HTTP代理
   
   简单的Python代理示例:
   ---------------------
   # 安装依赖: pip install pysocks requests
   
   # 使用下面的脚本启动代理:
   
   import socket
   import threading
   import select
   
   def handle_client(client_socket):
       request = client_socket.recv(4096)
       if not request.startswith(b'CONNECT'):
           client_socket.close()
           return
       
       # 解析目标
       host_port = request.split(b' ')[1].decode()
       host, port = host_port.split(':')
       port = int(port)
       
       # 连接目标服务器
       try:
           server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
           server.connect((host, port))
           client_socket.sendall(b'HTTP/1.1 200 Connection established\\r\\n\\r\\n')
       except:
           client_socket.close()
           return
       
       # 双向转发
       def forward(source, destination):
           while True:
               data = source.recv(4096)
               if not data:
                   break
               destination.sendall(data)
       
       t1 = threading.Thread(target=forward, args=(client_socket, server))
       t2 = threading.Thread(target=forward, args=(server, client_socket))
       t1.start()
       t2.start()
       t1.join()
       t2.join()
   
   server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
   server.bind(('0.0.0.0', 8080))
   server.listen(5)
   print("代理服务器运行在 0.0.0.0:8080")
   
   while True:
       client, addr = server.accept()
       threading.Thread(target=handle_client, args=(client,)).start()

2. 使用现有的HTTP代理服务（需确保支持CONNECT方法）

3. 替代方案: 使用支持HTTP的webhook服务或自建HTTP服务器转发
    """
    print(help_text)


def main():
    parser = argparse.ArgumentParser(
        description='BC260 NB-IoT 模块测试脚本',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  # 列出所有可用串口
  python bc260_test.py --list
  
  # 基本AT指令测试
  python bc260_test.py --port COM3
  
  # 详细调试模式（显示HEX数据）
  python bc260_test.py --port COM3 --verbose
  
  # 自动从config.env加载配置（推荐）
  python bc260_test.py --port COM3 --auto-config
  
  # 指定配置文件路径
  python bc260_test.py --port COM3 --config ../config.env
  
  # 手动指定参数
  python bc260_test.py --port COM3 --webhook "https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=YOUR_KEY" --proxy-ip 192.168.1.100 --proxy-port 8080
  
  # 查看代理配置帮助
  python bc260_test.py --proxy-help
        """
    )
    
    parser.add_argument('--port', default=DEFAULT_PORT,
                        help=f'串口号 (默认: {DEFAULT_PORT})')
    parser.add_argument('--baudrate', type=int, default=DEFAULT_BAUDRATE,
                        help=f'波特率 (默认: {DEFAULT_BAUDRATE})')
    parser.add_argument('--webhook', 
                        help='企业微信Webhook地址（用于测试消息发送）')
    parser.add_argument('--proxy-ip', default=DEFAULT_PROXY_IP,
                        help=f'HTTP代理IP地址 (默认: {DEFAULT_PROXY_IP})')
    parser.add_argument('--proxy-port', type=int, default=DEFAULT_PROXY_PORT,
                        help=f'HTTP代理端口 (默认: {DEFAULT_PROXY_PORT})')
    parser.add_argument('--config',
                        help='指定config.env配置文件路径')
    parser.add_argument('--auto-config', action='store_true',
                        help='自动从config.env加载配置（将覆盖--webhook、--proxy-ip、--proxy-port参数）')
    parser.add_argument('--verbose', action='store_true',
                        help='详细调试模式（显示HEX数据等详细信息）')
    parser.add_argument('--list', action='store_true',
                        help='列出可用串口')
    parser.add_argument('--proxy-help', action='store_true',
                        help='显示HTTP代理配置帮助')
    
    args = parser.parse_args()
    
    if args.proxy_help:
        print_proxy_help()
        return
    
    if args.list:
        list_serial_ports()
        return
    
    # 检查pyserial是否安装
    try:
        import serial
    except ImportError:
        print("[-] 请先安装 pyserial: pip install pyserial")
        sys.exit(1)
    
    # 处理配置
    webhook_url = args.webhook
    proxy_ip = args.proxy_ip
    proxy_port = args.proxy_port
    
    if args.auto_config or args.config:
        config = load_config_from_env(args.config)
        if config:
            # 从配置获取webhook
            config_webhook = get_webhook_url_from_config(config)
            if config_webhook:
                webhook_url = config_webhook
                print(f"[+] 从配置文件加载webhook: {webhook_url[:50]}...")
            
            # 从配置获取代理
            config_proxy_ip, config_proxy_port = get_bc260_proxy_from_config(config)
            if config_proxy_ip != DEFAULT_PROXY_IP:
                proxy_ip = config_proxy_ip
                proxy_port = config_proxy_port
                print(f"[+] 从配置文件加载代理: {proxy_ip}:{proxy_port}")
            
            # 检查BC260是否启用
            bc260_enabled = config.get('ENABLE_BC260', '0').strip()
            print(f"[*] 配置文件BC260启用状态: {bc260_enabled}")
    
    # 打印配置信息
    print("\n" + "-"*50)
    print("测试配置:")
    print(f"  串口: {args.port}")
    print(f"  波特率: {args.baudrate}")
    if webhook_url:
        print(f"  Webhook: {webhook_url[:50]}...")
        print(f"  代理: {proxy_ip}:{proxy_port}")
    else:
        print("  Webhook: 未配置（跳过消息发送测试）")
    print("-"*50 + "\n")
    
    # 运行测试
    tester = BC260Tester(
        port=args.port,
        baudrate=args.baudrate,
        webhook_url=webhook_url,
        proxy_ip=proxy_ip,
        proxy_port=proxy_port,
        verbose=args.verbose
    )
    
    try:
        success = tester.run_all_tests()
        
        if success:
            print("\n[+] 所有测试通过!")
            sys.exit(0)
        else:
            print("\n[-] 部分测试失败，请检查输出信息")
            sys.exit(1)
    except KeyboardInterrupt:
        print("\n[!] 用户中断测试")
        tester.disconnect()
        sys.exit(1)


if __name__ == '__main__':
    main()
