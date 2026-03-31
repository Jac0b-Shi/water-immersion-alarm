#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
BC260 NB-IoT 网络诊断脚本
参考M5310模组实现，用于诊断网络附着失败的原因

使用方法:
    python bc260_diagnose.py --port COM3
"""

import serial
import serial.tools.list_ports
import time
import sys
import argparse
import re


class BC260Diagnoser:
    # 常见NB-IoT频段和频点参考
    NB_BANDS = {
        5: {"name": "B5 (850MHz)", "freq_range": "824-849MHz/869-894MHz", "earfcn_start": 2400, "earfcn_end": 2649},
        8: {"name": "B8 (900MHz)", "freq_range": "880-915MHz/925-960MHz", "earfcn_start": 3450, "earfcn_end": 3799},
        3: {"name": "B3 (1800MHz)", "freq_range": "1710-1785MHz/1805-1880MHz", "earfcn_start": 1200, "earfcn_end": 1949},
        1: {"name": "B1 (2100MHz)", "freq_range": "1920-1980MHz/2110-2170MHz", "earfcn_start": 0, "earfcn_end": 599},
    }
    
    def __init__(self, port, timeout=5, lock_earfcn=None):
        self.port = port
        self.baudrate = 115200  # 固定使用115200
        self.timeout = timeout
        self.ser = None
        self.auto_report_enabled = False
        self.lock_earfcn = lock_earfcn  # 锁频频点，None表示不锁频
        self.locked_band = None  # 记录实际锁定的频段
    
    def connect(self):
        try:
            print(f"[*] 连接串口 {self.port} (波特率: 115200)...")
            
            self.ser = serial.Serial(
                port=self.port,
                baudrate=115200,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=self.timeout
            )
            time.sleep(0.5)
            self.ser.reset_input_buffer()
            print(f"[+] 串口连接成功 (115200bps)")
            
            # 立即发送AT验证模块就绪
            if not self.wait_for_at_ready(max_attempts=5):
                print("[-] 模块未响应AT命令，请检查:")
                print("    1. 模块是否正确上电")
                print("    2. 串口连接是否正确")
                print("    3. 波特率是否为115200")
                return False
            
            return True
        except Exception as e:
            print(f"[-] 串口连接失败: {e}")
            return False
    
    def disconnect(self):
        # 解除锁频
        if self.ser and self.ser.is_open:
            self.unlock_earfcn()
        
        # 关闭自动上报功能
        if self.ser and self.ser.is_open and self.auto_report_enabled:
            print("\n[*] 恢复自动上报设置...")
            try:
                self.send_at("AT+CSCON=0", timeout=2)  # 关闭信号自动上报
                self.send_at("AT+CEREG=0", timeout=2)  # 关闭注册自动上报
            except:
                pass
        
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("[*] 串口已断开")
    
    def send_at(self, cmd, timeout=None, wait_extra=0.5):
        if timeout is None:
            timeout = self.timeout
            
        if not self.ser or not self.ser.is_open:
            return False, ""
        
        self.ser.reset_input_buffer()
        full_cmd = f"{cmd}\r\n"
        print(f"\n[>] {cmd}")
        self.ser.write(full_cmd.encode())
        self.ser.flush()
        
        start_time = time.time()
        lines = []
        
        while time.time() - start_time < timeout:
            if self.ser.in_waiting > 0:
                try:
                    line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        print(f"[<] {line}")
                        lines.append(line)
                        if "OK" in line or "ERROR" in line:
                            time.sleep(wait_extra)
                            while self.ser.in_waiting > 0:
                                extra = self.ser.readline().decode('utf-8', errors='ignore').strip()
                                if extra:
                                    print(f"[<] {extra}")
                                    lines.append(extra)
                            break
                except:
                    break
            time.sleep(0.1)
        
        return "OK" in lines, "\n".join(lines)
    
    def wait_for_at_ready(self, max_attempts=10):
        """循环发送AT直到返回OK，证明模块初始化正常 (参考M5310)"""
        print("[*] 验证模块就绪...")
        for i in range(max_attempts):
            success, _ = self.send_at("AT", timeout=2)
            if success:
                print(f"[+] 模块就绪 (尝试 {i+1} 次)")
                return True
            time.sleep(0.3)
        return False
    
    def enable_auto_report(self):
        """开启信号和注册自动上报 (参考M5310)"""
        print("\n[*] 开启自动上报功能...")
        self.send_at("AT+CSCON=1", timeout=3)  # 打开信号提示自动上报
        self.send_at("AT+CEREG=1", timeout=3)  # 打开注册信息自动上报
        self.auto_report_enabled = True
        print("[+] 自动上报已开启")
        print("    +CSCON:1 表示信号连接状态")
        print("    +CEREG 表示网络注册状态")
    
    def get_band_from_earfcn(self, earfcn):
        """根据EARFCN判断所属频段"""
        for band, info in self.NB_BANDS.items():
            if info["earfcn_start"] <= earfcn <= info["earfcn_end"]:
                return band
        return None
    
    def try_lock_earfcn(self):
        """尝试锁定频段 (BC260使用AT+QBAND限制频段)
        锁频可以有效减小搜网时间，但设置错误会导致搜网失败
        """
        if self.lock_earfcn is None:
            return
        
        print("\n" + "-"*40)
        print("锁频设置")
        print("-"*40)
        print(f"[!] 警告: 锁频可以减小搜网时间，但设置错误会导致搜网失败!")
        print(f"[*] 目标频点: {self.lock_earfcn}")
        
        # 判断频点所属频段
        target_band = self.get_band_from_earfcn(self.lock_earfcn)
        if target_band is None:
            print(f"[-] 频点 {self.lock_earfcn} 不在已知频段范围内，放弃锁频")
            return
        
        print(f"[*] 频点 {self.lock_earfcn} 属于 B{target_band} 频段")
        
        # 显示频段参考信息
        print("\n[*] NB-IoT频段参考:")
        for band, info in self.NB_BANDS.items():
            marker = " <-- 目标频段" if band == target_band else ""
            print(f"    B{band}: {info['name']} EARFCN={info['earfcn_start']}-{info['earfcn_end']}{marker}")
        
        # 先查询当前频段设置
        print("\n[*] 查询当前频段设置...")
        success, current_band_resp = self.send_at("AT+QBAND?")
        
        # BC260锁频：使用AT+QBAND限制只搜索特定频段
        # 先注销网络
        print("\n[*] 注销当前网络，准备切换频段...")
        self.send_at("AT+COPS=2", timeout=5)
        time.sleep(1)
        
        # 设置只搜索目标频段
        cmd = f'AT+QBAND=1,{target_band}'
        print(f"[*] 设置只搜索 B{target_band} 频段...")
        success, resp = self.send_at(cmd, timeout=5)
        
        if success:
            print(f"[+] 锁频成功: 只搜索 B{target_band} 频段")
            print("[*] 恢复自动搜网...")
            self.send_at("AT+COPS=0", timeout=5)
            time.sleep(2)
            self.locked_band = target_band  # 记录锁定的频段，用于后续恢复
        else:
            print(f"[-] 锁频失败，恢复自动频段选择...")
            self.send_at("AT+COPS=0", timeout=5)
            time.sleep(2)
    
    def unlock_earfcn(self):
        """解除锁频，恢复自动搜网"""
        if self.lock_earfcn is None:
            return
        
        print("\n[*] 解除锁频，恢复自动搜网...")
        # 恢复支持多个频段 (B5和B8)
        self.send_at("AT+COPS=2", timeout=3)  # 先注销
        time.sleep(0.5)
        self.send_at("AT+QBAND=2,5,8", timeout=5)  # 恢复B5和B8
        time.sleep(0.5)
        self.send_at("AT+COPS=0", timeout=5)  # 恢复自动选择
        time.sleep(1)
        print("[+] 已恢复自动搜网")
    
    def wait_for_network_registration(self, max_wait_time=120, check_interval=5):
        """等待网络注册，带重试机制"""
        print(f"\n[*] 正在等待网络注册 (最长等待 {max_wait_time} 秒)...")
        
        status_map = {
            0: "未注册，未搜索",
            1: "已注册，归属网络 ✅",
            2: "未注册，正在搜索",
            3: "注册被拒绝 ❌",
            4: "未知",
            5: "已注册，漫游 ✅"
        }
        
        start_time = time.time()
        attempt = 0
        
        while time.time() - start_time < max_wait_time:
            attempt += 1
            elapsed = int(time.time() - start_time)
            remaining = max_wait_time - elapsed
            
            print(f"\n[检查 {attempt}] 已用时间: {elapsed}s, 剩余: {remaining}s")
            
            # 检查网络注册状态
            success, resp = self.send_at("AT+CEREG?", timeout=3)
            
            if success:
                match = re.search(r'\+CEREG:\s*\d+,(\d+)', resp)
                if match:
                    status = int(match.group(1))
                    status_text = status_map.get(status, '未知')
                    print(f"    状态: {status} ({status_text})")
                    
                    # 检查信号
                    _, csq_resp = self.send_at("AT+CSQ", timeout=2)
                    csq_match = re.search(r'\+CSQ:\s*(\d+)', csq_resp)
                    if csq_match:
                        signal = int(csq_match.group(1))
                        signal_text = "无信号" if signal == 99 else f"{signal}/31"
                        print(f"    信号: {signal_text}")
                    
                    if status == 1 or status == 5:
                        print(f"\n[+] 网络注册成功！用时 {elapsed} 秒")
                        return True, status
                    elif status == 3:
                        print("\n[-] 网络注册被拒绝，可能需要联系运营商")
                        return False, status
                else:
                    print(f"    响应: {resp[:50] if resp else '无'}")
            
            if elapsed < max_wait_time:
                print(f"    等待 {check_interval} 秒后重试...")
                time.sleep(check_interval)
        
        print(f"\n[-] 等待超时 ({max_wait_time} 秒)，网络未注册成功")
        return False, 2  # 返回正在搜索状态
    
    def diagnose(self):
        print("\n" + "="*60)
        print("BC260 NB-IoT 网络诊断")
        print("="*60)
        
        if not self.connect():
            return
        
        try:
            # 连接时已验证AT就绪，这里继续后续诊断
            
            # 2. 检查模块信息
            print("\n" + "-"*40)
            print("1. 检查模块信息")
            print("-"*40)
            self.send_at("ATI")
            self.send_at("AT+CGMM")  # 模块型号
            self.send_at("AT+CGMR")  # 软件版本
            
            # 3. 检查SIM卡
            print("\n" + "-"*40)
            print("2. 检查SIM卡状态")
            print("-"*40)
            self.send_at("AT+CIMI")  # IMSI
            self.send_at("AT+CCID")  # ICCID
            
            # 4. 检查功能模式
            print("\n" + "-"*40)
            print("3. 检查功能模式")
            print("-"*40)
            self.send_at("AT+CFUN?")
            
            # 确保功能模式为全功能
            success, resp = self.send_at("AT+CFUN?")
            if success and "+CFUN: 1" not in resp:
                print("[*] 设置全功能模式...")
                self.send_at("AT+CFUN=1", timeout=10)
                time.sleep(2)
            
            # 5. 尝试锁频 (如果指定了频点)
            if self.lock_earfcn:
                self.try_lock_earfcn()
            
            # 6. 开启自动上报 (参考M5310)
            print("\n" + "-"*40)
            print("4. 开启自动上报")
            print("-"*40)
            self.enable_auto_report()
            
            # 等待一下让自动上报生效
            time.sleep(1)
            
            # 6. 检查当前注册状态
            print("\n" + "-"*40)
            print("5. 检查网络注册状态")
            print("-"*40)
            success, resp = self.send_at("AT+CEREG?")
            current_status = None
            if success:
                match = re.search(r'\+CEREG:\s*\d+,(\d+)', resp)
                if match:
                    current_status = int(match.group(1))
                    status_map = {
                        0: "未注册，未搜索",
                        1: "已注册，归属网络",
                        2: "未注册，正在搜索",
                        3: "注册被拒绝",
                        4: "未知",
                        5: "已注册，漫游"
                    }
                    print(f"[*] 网络注册状态: {current_status} ({status_map.get(current_status, '未知')})")
                    
                    # 如果正在搜索网络或已注册，等待注册
                    if current_status == 2:
                        registered, final_status = self.wait_for_network_registration(
                            max_wait_time=90,  # 最多等待90秒
                            check_interval=5   # 每5秒检查一次
                        )
                        current_status = final_status
            
            # 7. 检查运营商
            print("\n" + "-"*40)
            print("6. 检查运营商选择")
            print("-"*40)
            self.send_at("AT+COPS?")
            
            # 如果网络未注册，尝试搜索可用网络
            if current_status == 0 or current_status == 2:
                print("\n[*] 网络未注册，尝试搜索可用运营商...")
                print("    (这可能需要 30-60 秒，请耐心等待)")
                success, ops_resp = self.send_at("AT+COPS=?", timeout=60)
                
                # 保存运营商响应，供后续分析
                self.last_cops_response = ops_resp
                
                if success and "+COPS:" in ops_resp:
                    # 解析可用运营商 (支持多种格式)
                    # 格式1: (stat,"long","short","numeric"[,AcT])
                    ops_list = re.findall(r'\((\d+),"([^"]*)","([^"]*)","(\d+)"(?:,\d+)?\)', ops_resp)
                    if not ops_list:
                        # 格式2: 没有引号的简化格式
                        ops_list = re.findall(r'\((\d+),([^,]+),([^,]+),(\d+)(?:,\d+)?\)', ops_resp)
                    
                    # 也尝试从之前缓存的响应中解析
                    if not ops_list and hasattr(self, 'delayed_cops'):
                        ops_list = re.findall(r'\((\d+),"([^"]*)","([^"]*)","(\d+)"(?:,\d+)?\)', self.delayed_cops)
                    
                    if ops_list:
                        print("\n[+] 发现可用运营商:")
                        nb_iot_ops = []
                        for op in ops_list:
                            status, long_name, short_name, plmn = op
                            status_text = "可用" if status == "2" else "当前" if status == "1" else "禁止"
                            print(f"    - {long_name} ({short_name}): PLMN={plmn}, 状态={status_text}")
                            
                            # 收集 NB-IoT 运营商
                            if plmn in ["46004", "46005", "46006", "46007", "46008", "46009", "46011", "46012"]:
                                nb_iot_ops.append((plmn, f"{long_name} NB-IoT"))
                        
                        # 尝试自动选择或手动选择
                        print("\n[*] 尝试手动选择运营商...")
                        self.try_manual_operator_selection(nb_iot_ops)
                    else:
                        print("[-] 未找到可用运营商，可能是:")
                        print("    1. 当地无 NB-IoT 覆盖")
                        print("    2. SIM 卡未开通 NB-IoT 服务")
                        print("    3. 天线位置不佳")
            
            # 8. 检查APN设置
            print("\n" + "-"*40)
            print("7. 检查APN设置")
            print("-"*40)
            self.send_at("AT+CGDCONT?")
            
            # 9. 检查信号质量
            print("\n" + "-"*40)
            print("8. 检查信号质量")
            print("-"*40)
            self.send_at("AT+CSQ")
            
            # 10. 尝试手动设置APN并附着
            print("\n" + "-"*40)
            print("9. 尝试设置移动NB-IoT APN")
            print("-"*40)
            print("[*] 中国移动NB-IoT APN通常为: cmnbiot 或 cmiot")
            self.send_at('AT+CGDCONT=1,"IP","cmnbiot"')
            
            # 11. 尝试附着网络
            print("\n" + "-"*40)
            print("10. 尝试附着网络")
            print("-"*40)
            self.send_at("AT+CGATT=1", timeout=15)
            
            # 等待附着完成
            print("\n[*] 等待网络附着完成...")
            time.sleep(5)
            
            # 轮询检查附着状态
            for i in range(6):  # 最多再检查6次，每次5秒
                success, resp = self.send_at("AT+CGATT?", timeout=3)
                if success and "+CGATT: 1" in resp:
                    print("[+] 网络附着成功！")
                    break
                elif i < 5:
                    print(f"    未附着，等待 5 秒... ({i+1}/6)")
                    time.sleep(5)
                else:
                    print("[-] 网络附着超时")
            
            # 12. 检查附着状态
            print("\n" + "-"*40)
            print("11. 检查附着状态")
            print("-"*40)
            self.send_at("AT+CGATT?")
            
            # 13. 如果附着成功，检查IP地址
            print("\n" + "-"*40)
            print("12. 检查IP地址")
            print("-"*40)
            self.send_at("AT+CGPADDR")
            
            # 14. 其他可能有用的信息
            print("\n" + "-"*40)
            print("13. 检查频段设置")
            print("-"*40)
            self.send_at("AT+QBAND?")
            
            print("\n" + "-"*40)
            print("14. 检查网络类型")
            print("-"*40)
            self.send_at("AT+QNWINFO")
            
            # 15. 最终状态汇总
            print("\n" + "="*60)
            print("诊断结果汇总")
            print("="*60)
            time.sleep(0.5)
            self.send_at("AT+CEREG?", timeout=3)
            time.sleep(0.3)
            self.send_at("AT+CGATT?", timeout=3)
            time.sleep(0.3)
            self.send_at("AT+CSQ", timeout=3)
            time.sleep(0.3)
            
        finally:
            self.disconnect()
    
    def try_manual_operator_selection(self, available_nb_iot_ops=None):
        """尝试手动选择运营商
        参考M5310: AT+COPS=1,2,"46000" 设置手动注册移动运营MNC
        Args:
            available_nb_iot_ops: 从COPS=?获取到的可用NB-IoT运营商列表 [(plmn, name), ...]
        """
        # 首先尝试 46000 (中国移动GSM) 验证基本功能 (参考M5310)
        print("\n[*] 首先尝试注册中国移动 GSM (46000) 验证模块基础功能...")
        self.send_at("AT+COPS=0", timeout=5)
        time.sleep(1)
        
        success, resp = self.send_at('AT+COPS=1,2,"46000"', timeout=15)
        if success:
            print("    等待注册结果...")
            for check in range(6):  # 等待 30 秒
                time.sleep(5)
                check_success, check_resp = self.send_at("AT+CREG?", timeout=3)
                if check_success:
                    match = re.search(r'\+CREG:\s*\d+,(\d+)', check_resp)
                    if match:
                        status = int(match.group(1))
                        status_text = {0: "未注册", 1: "已注册归属网络", 2: "搜索中", 3: "被拒绝", 5: "已注册漫游"}.get(status, "未知")
                        print(f"    GSM 注册状态: {status} ({status_text})")
                        
                        if status == 1 or status == 5:
                            print(f"    [+] GSM 网络注册成功！")
                            print(f"    [*] 这说明模块和 SIM 卡基本功能正常")
                            print(f"    [*] 问题可能是: SIM 卡未开通 NB-IoT / 当地无 NB-IoT 覆盖")
                            
                            # 检查GSM网络下的附着状态
                            self.send_at("AT+CGATT?", timeout=2)
                            
                            # 注册成功后切换回自动模式，继续尝试NB-IoT
                            print("\n[*] 切换回自动模式，继续尝试 NB-IoT...")
                            self.send_at("AT+COPS=0", timeout=5)
                            time.sleep(2)
                            break
                        elif status == 3:
                            print(f"    [-] GSM 注册被拒绝，SIM卡可能有问题")
                            break
        
        # NB-IoT 运营商 PLMN 代码
        operators = [
            ("46004", "中国移动 NB-IoT"),
            ("46007", "中国移动 NB-IoT"),
            ("46008", "中国移动 NB-IoT"),
            ("46006", "中国联通 NB-IoT"),
            ("46009", "中国联通 NB-IoT"),
            ("46005", "中国电信 NB-IoT"),
            ("46011", "中国电信 NB-IoT"),
            ("46012", "中国电信 NB-IoT"),
        ]
        
        target_plmns = []
        
        # 如果提供了可用的 NB-IoT 运营商，优先使用
        if available_nb_iot_ops:
            print(f"\n[*] 优先尝试搜索到的 NB-IoT 运营商: {[op[0] for op in available_nb_iot_ops]}")
            target_plmns = available_nb_iot_ops
        
        # 如果没有可用的，根据 IMSI 判断 SIM 卡运营商
        if not target_plmns:
            imsi_success, imsi_resp = self.send_at("AT+CIMI", timeout=3)
            
            if imsi_success and len(imsi_resp) >= 15:
                # 从 IMSI 提取 MCC+MNC
                mcc = imsi_resp[:3] if imsi_resp[:3].isdigit() else None
                mnc_digits = imsi_resp[3:5] if imsi_resp[3:5].isdigit() else None
                
                if mcc == "460":
                    if mnc_digits in ["00", "02", "04", "07", "08"]:
                        print("    检测到中国移动 SIM 卡")
                        target_plmns = [("46004", "中国移动 NB-IoT"), 
                                       ("46007", "中国移动 NB-IoT"),
                                       ("46008", "中国移动 NB-IoT")]
                    elif mnc_digits in ["01", "06", "09"]:
                        print("    检测到中国联通 SIM 卡")
                        target_plmns = [("46006", "中国联通 NB-IoT"),
                                       ("46009", "中国联通 NB-IoT")]
                    elif mnc_digits in ["03", "05", "11", "12"]:
                        print("    检测到中国电信 SIM 卡")
                        target_plmns = [("46005", "中国电信 NB-IoT"),
                                       ("46011", "中国电信 NB-IoT"),
                                       ("46012", "中国电信 NB-IoT")]
        
        # 如果没有检测到，尝试所有 NB-IoT 运营商
        if not target_plmns:
            print("    尝试所有 NB-IoT 运营商...")
            target_plmns = operators
        
        # 尝试注册每个运营商
        registered = False
        for plmn, name in target_plmns:
            print(f"\n    尝试注册 {name} (PLMN: {plmn})...")
            
            # 先设置为自动模式清除之前的设置
            self.send_at("AT+COPS=0", timeout=5)
            time.sleep(1)
            
            # 手动选择运营商
            success, resp = self.send_at(f'AT+COPS=1,2,"{plmn}"', timeout=15)
            
            if success:
                # 等待注册 (NB-IoT 初次注册可能需要 30-120 秒)
                print(f"    等待注册完成 (最长60秒)...")
                for check in range(12):  # 等待 60 秒
                    time.sleep(5)
                    check_success, check_resp = self.send_at("AT+CEREG?", timeout=3)
                    if check_success:
                        match = re.search(r'\+CEREG:\s*\d+,(\d+)', check_resp)
                        if match:
                            status = int(match.group(1))
                            if status == 1 or status == 5:
                                print(f"    [+] 成功注册到 {name}!")
                                registered = True
                                break
                            elif status == 3:
                                print(f"    [-] 被拒绝，尝试下一个...")
                                break
                            elif check < 3:  # 前几次打印状态
                                print(f"    等待中... 状态={status}")
                
                if registered:
                    break
            else:
                print(f"    [-] 选择失败: {resp[:50] if resp else '无响应'}")
        
        if not registered:
            print("\n[-] 手动选择运营商失败")
            
            # 如果锁频了，建议解除锁频再试
            if self.lock_earfcn and hasattr(self, 'locked_band'):
                print(f"\n[!] 当前已锁频到 B{self.locked_band} 频段，可能是锁频导致无法注册")
                print(f"    目标频点 {self.lock_earfcn} 可能无NB-IoT覆盖")
                print("[*] 建议:")
                print("    1. 解除锁频后重试")
                print("    2. 或尝试该频段的其他频点")
            
            # 恢复自动模式
            print("\n[*] 恢复自动选择模式...")
            self.send_at("AT+COPS=0", timeout=5)
            time.sleep(2)
        
        print("\n" + "="*60)
        print("诊断完成")
        print("="*60)
        print("\n常见问题:")
        print("1. 如果CEREG返回0或2: SIM卡可能未开通NB-IoT服务")
        print("2. 如果CEREG返回3: 网络拒绝注册，可能需要联系运营商")
        print("3. 如果APN为空或不正确: 需要手动设置cmnbiot或cmiot")
        print("4. 如果信号正常但无法附着: 可能是当地NB-IoT覆盖问题")
        print("\n提示:")
        print("- NB-IoT 初次注册可能需要 30-120 秒")
        print("- 建议将天线放置在靠近窗户的位置")
        print("- 确认 SIM 卡已开通 NB-IoT 数据业务")


def main():
    parser = argparse.ArgumentParser(description='BC260 NB-IoT 网络诊断工具')
    parser.add_argument('--port', default='COM3', help='串口号 (默认: COM3)')
    parser.add_argument('--lock-earfcn', type=int, metavar='EARFCN',
                        help='锁定特定频点 (例如: 3555)，可以减小搜网时间但设置错误会导致失败')
    
    args = parser.parse_args()
    
    if args.lock_earfcn:
        print("="*60)
        print("[!] 锁频模式已启用")
        print(f"    目标频点: {args.lock_earfcn}")
        print("    警告: 锁频错误会导致搜网失败!")
        print("="*60)
    
    try:
        import serial
    except ImportError:
        print("[-] 请先安装 pyserial: pip install pyserial")
        sys.exit(1)
    
    diagnoser = BC260Diagnoser(args.port, lock_earfcn=args.lock_earfcn)
    diagnoser.diagnose()


if __name__ == '__main__':
    main()
