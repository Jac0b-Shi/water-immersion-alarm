#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
超声波液位传感器测试脚本 (带Modbus配置)
串口配置: 115200/8/N/1
数据格式: 4字节帧 [帧头0xFF, Data_H, Data_L, SUM校验和]

Modbus寄存器配置:
  - 电源降噪等级(0x021A): 默认1, 范围1~5
  - 算法模式(0x0228): 默认0, 范围0~5 (0:实时值, 1-3:液面晃动过滤, 4:小台阶过滤, 5:高灵敏度)

使用方法:
    python ultrasonic_sensor_reader.py -p COM3
    python ultrasonic_sensor_reader.py --port COM4
"""

import serial
import time
import struct
import argparse


# 默认配置参数
DEFAULT_PORT = 'COM3'     # 默认串口号
BAUDRATE = 115200         # 波特率
TIMEOUT = 1               # 读取超时时间(秒)
READ_INTERVAL = 5         # 读取间隔(秒)
DEVICE_ADDRESS = 0x01     # Modbus设备地址(默认)

# Modbus寄存器地址
REG_POWER_NOISE_REDUCTION = 0x021A  # 电源降噪等级寄存器
REG_ALGORITHM_MODE = 0x0228          # 算法模式寄存器

# 配置值
POWER_NOISE_LEVEL = 3     # 电源降噪等级设为3
ALGORITHM_MODE = 2        # 算法模式设为2 (液面晃动过滤等级2)


def calculate_crc16(data: bytes) -> bytes:
    """
    计算Modbus CRC-16校验
    CRC-16/MODBUS: 多项式 0xA001
    """
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    # 返回低字节在前，高字节在后
    return bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def build_modbus_write_frame(device_addr: int, reg_addr: int, value: int) -> bytes:
    """
    构建Modbus写单个寄存器帧 (功能码0x06)
    
    格式: [设备地址][功能码0x06][寄存器高字节][寄存器低字节][数据高字节][数据低字节][CRC低][CRC高]
    """
    frame = bytes([
        device_addr,           # 设备地址
        0x06,                  # 功能码: 写单个寄存器
        (reg_addr >> 8) & 0xFF,  # 寄存器地址高字节
        reg_addr & 0xFF,         # 寄存器地址低字节
        (value >> 8) & 0xFF,     # 数据高字节
        value & 0xFF             # 数据低字节
    ])
    crc = calculate_crc16(frame)
    return frame + crc


def modbus_write_register(ser: serial.Serial, device_addr: int, reg_addr: int, value: int,
                         timeout: float = 0.5) -> tuple:
    """
    通过Modbus写入寄存器

    返回: (是否成功, 错误信息)
    """
    try:
        # 清空缓冲区
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        # 发送写寄存器命令
        frame = build_modbus_write_frame(device_addr, reg_addr, value)
        ser.write(frame)

        # 等待响应 (给传感器足够时间处理)
        time.sleep(0.1)

        # 读取响应 (写寄存器返回与发送相同的8字节)
        response = ser.read(8)

        if len(response) == 0:
            return False, "无响应"

        if len(response) != 8:
            return False, f"响应长度错误: 期望8字节, 实际{len(response)}字节"

        # 验证响应
        if response[:6] != frame[:6]:
            return False, f"响应数据不匹配: 发送{frame[:6].hex()}, 收到{response[:6].hex()}"

        # 验证CRC
        resp_crc = response[6:8]
        calc_crc = calculate_crc16(response[:6])
        if resp_crc != calc_crc:
            return False, f"CRC校验失败"

        return True, "成功"

    except Exception as e:
        return False, f"异常: {e}"


def modbus_read_register(ser: serial.Serial, device_addr: int, reg_addr: int,
                        timeout: float = 0.5) -> tuple:
    """
    通过Modbus读取单个寄存器

    返回: (值, 是否成功, 错误信息)
    """
    try:
        # 清空缓冲区
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        # 构建读寄存器帧 (功能码0x03)
        frame = bytes([
            device_addr,
            0x03,                  # 功能码: 读保持寄存器
            (reg_addr >> 8) & 0xFF,
            reg_addr & 0xFF,
            0x00, 0x01             # 读取1个寄存器
        ])
        crc = calculate_crc16(frame)
        frame += crc

        ser.write(frame)
        time.sleep(0.05)

        # 读取响应: [地址][功能码0x03][字节数][数据高][数据低][CRC低][CRC高]
        response = ser.read(7)

        if len(response) != 7:
            return None, False, f"响应长度错误: 期望7字节, 实际{len(response)}字节"

        if response[0] != device_addr:
            return None, False, "设备地址不匹配"

        if response[1] != 0x03:
            return None, False, f"功能码错误: 期望0x03, 实际0x{response[1]:02X}"

        if response[2] != 0x02:
            return None, False, f"字节数错误: 期望2, 实际{response[2]}"

        # 验证CRC
        resp_crc = response[5:7]
        calc_crc = calculate_crc16(response[:5])
        if resp_crc != calc_crc:
            return None, False, "CRC校验失败"

        value = (response[3] << 8) | response[4]
        return value, True, "成功"

    except Exception as e:
        return None, False, f"异常: {e}"


def wait_for_power_on(ser: serial.Serial, timeout: float = 30.0, verbose: bool = False) -> bool:
    """
    等待传感器上电

    循环检测传感器是否在线，直到检测到响应或超时
    检测方式: 发送触发信号，检查是否能收到数据响应
    """
    print("等待传感器上电...")
    print("  请在倒计时结束前给传感器上电")
    print(f"  超时时间: {timeout}秒")
    print("  提示: 传感器上电后需要约650ms初始化时间")
    print("  检测方式: 发送触发信号，等待4字节响应\n")

    start_time = time.time()
    check_count = 0
    last_debug_time = 0

    while time.time() - start_time < timeout:
        elapsed = time.time() - start_time
        remaining = int(timeout - elapsed)

        # 每50ms检测一次（更频繁地检测）
        try:
            ser.reset_input_buffer()
            ser.reset_output_buffer()
            ser.write(b'\x01')  # 触发信号
            time.sleep(0.05)     # 等待响应
            data = ser.read(4)
            check_count += 1

            # 调试输出
            if verbose and len(data) > 0 and elapsed - last_debug_time > 1:
                print(f"  [调试] 收到 {len(data)} 字节: {data.hex()}")
                last_debug_time = elapsed

            # 检查是否是有效的数据帧 (帧头为0xFF且有4字节)
            if len(data) == 4 and data[0] == 0xFF:
                print(f"\n  ✓ 检测到传感器上电! (尝试{check_count}次, 耗时{elapsed:.2f}秒)")
                if verbose:
                    distance = data[1] * 256 + data[2]
                    print(f"  当前距离: {distance} mm")
                return True
            # 也接受其他长度的响应（可能传感器正在初始化）
            elif len(data) > 0 and verbose and check_count % 20 == 0:
                print(f"  [调试] 收到非标准响应: {data.hex()} (长度{len(data)})")

        except Exception as e:
            if verbose and check_count % 20 == 0:
                print(f"  [调试] 异常: {e}")

        # 显示倒计时（每秒更新一次）
        if check_count % 20 == 0:
            print(f"\r  等待中... {remaining}秒 (已检测{check_count}次)  ", end="", flush=True)

        time.sleep(0.05)

    print(f"\n  等待超时 ({timeout}秒, 共检测{check_count}次)")
    return False


def configure_sensor(ser: serial.Serial, wait_power_on: bool = True, verbose: bool = False) -> bool:
    """
    配置传感器参数

    1. 等待传感器上电（可选）
    2. 设置电源降噪等级为3
    3. 设置算法模式为2

    注意: UART受控模式下，Modbus协议仅在上电后500ms内有效
    """
    print("=" * 50)
    print("传感器配置模式")
    print("=" * 50)

    # 等待传感器上电
    if wait_power_on:
        if not wait_for_power_on(ser, timeout=30.0, verbose=verbose):
            print("未检测到传感器，配置失败")
            return False
        # 注意: 传感器上电后需要约650ms才能输出数据
        # 但Modbus配置窗口只有500ms，所以检测到上电后要立即配置
        # 这里的"上电"检测实际上已经是传感器能响应触发信号的时候了
    else:
        # 不等待，直接尝试配置
        print("直接尝试配置（假设传感器已上电）...")

    # 快速配置（必须在上电后500ms内完成）
    print("\n正在快速配置...")

    # 设置电源降噪等级为3
    print(f"  设置电源降噪等级为 {POWER_NOISE_LEVEL}...", end=" ")
    success, msg = modbus_write_register(ser, DEVICE_ADDRESS, REG_POWER_NOISE_REDUCTION, POWER_NOISE_LEVEL)
    if success:
        print("✓")
    else:
        print(f"✗ ({msg})")
        if verbose:
            print(f"  [调试] 帧数据: {build_modbus_write_frame(DEVICE_ADDRESS, REG_POWER_NOISE_REDUCTION, POWER_NOISE_LEVEL).hex()}")
        return False

    # 设置算法模式为2
    print(f"  设置算法模式为 {ALGORITHM_MODE}...", end=" ")
    success, msg = modbus_write_register(ser, DEVICE_ADDRESS, REG_ALGORITHM_MODE, ALGORITHM_MODE)
    if success:
        print("✓")
    else:
        print(f"✗ ({msg})")
        return False

    print("\n传感器配置完成!")
    print("=" * 50 + "\n")
    return True


def calculate_checksum(data: bytes) -> int:
    """计算校验和: (帧头 + Data_H + Data_L) & 0x00FF"""
    return sum(data[:3]) & 0xFF


def parse_distance(data: bytes) -> tuple:
    """
    解析传感器返回的数据帧
    
    返回: (距离值mm, 是否成功, 错误信息)
    """
    if len(data) != 4:
        return None, False, f"数据长度错误: 期望4字节, 实际{len(data)}字节"
    
    # 检查帧头
    if data[0] != 0xFF:
        return None, False, f"帧头错误: 期望0xFF, 实际0x{data[0]:02X}"
    
    # 校验和检查
    expected_sum = calculate_checksum(data)
    actual_sum = data[3]
    if expected_sum != actual_sum:
        return None, False, f"校验和错误: 期望0x{expected_sum:02X}, 实际0x{actual_sum:02X}"
    
    # 计算距离值
    distance = data[1] * 256 + data[2]
    
    # 检查特殊值
    if distance == 0xFFFE:
        return None, False, "检测到同频干扰"
    if distance == 0xFFFD:
        return None, False, "未检测到物体"
    
    return distance, True, None


def read_sensor(ser: serial.Serial) -> tuple:
    """
    触发并读取传感器数据
    
    返回: (距离值mm, 是否成功, 错误信息)
    """
    try:
        # 清空缓冲区
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        
        # 发送触发信号（任意字节数据即可触发测量）
        ser.write(b'\x01')
        
        # 等待并读取4字节响应
        # 根据文档 T2=8~42ms, 所以稍微等待一下
        time.sleep(0.05)
        
        data = ser.read(4)
        
        if len(data) < 4:
            return None, False, f"读取超时: 只收到{len(data)}字节"
        
        return parse_distance(data)
        
    except serial.SerialException as e:
        return None, False, f"串口错误: {e}"


def main():
    """主函数"""
    # 解析命令行参数
    parser = argparse.ArgumentParser(
        description='超声波液位传感器测试程序 (带Modbus配置)',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Modbus配置:
  电源降噪等级: 3 (适用于较长距离USB供电)
  算法模式: 2 (液面晃动过滤等级2，处理值输出，响应时间≥2s)

注意:
  UART受控模式下，Modbus配置仅在上电后500ms内有效。
  如果配置失败，请重新上电传感器后立即运行脚本。

使用方法:
  1. 直接运行(假设传感器已上电):
     python ultrasonic_sensor_reader.py -p COM3

  2. 等待上电模式(推荐):
     python ultrasonic_sensor_reader.py -p COM3 -w
     # 然后手动给传感器上电，脚本会自动检测并完成配置

  3. 跳过配置(仅读取数据):
     python ultrasonic_sensor_reader.py -p COM3 --skip-config

  4. 调试模式:
     python ultrasonic_sensor_reader.py -p COM3 -v
        '''
    )
    parser.add_argument(
        '-p', '--port',
        default=DEFAULT_PORT,
        help=f'串口号 (默认: {DEFAULT_PORT})'
    )
    parser.add_argument(
        '--skip-config',
        action='store_true',
        help='跳过传感器配置'
    )
    parser.add_argument(
        '-v', '--verbose',
        action='store_true',
        help='显示调试信息'
    )
    parser.add_argument(
        '-w', '--wait-power',
        action='store_true',
        help='等待传感器上电模式: 脚本会循环检测传感器，在上电后500ms内自动完成配置'
    )
    args = parser.parse_args()

    serial_port = args.port

    print(f"超声波液位传感器测试程序")
    print(f"串口: {serial_port}, 波特率: {BAUDRATE}")
    print(f"读取间隔: {READ_INTERVAL}秒")
    print("-" * 50)

    try:
        # 打开串口
        with serial.Serial(
            port=serial_port,
            baudrate=BAUDRATE,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=TIMEOUT
        ) as ser:
            print(f"成功打开串口 {serial_port}")

            # 配置传感器（除非跳过）
            if not args.skip_config:
                if not configure_sensor(ser, wait_power_on=args.wait_power, verbose=args.verbose):
                    print("\n提示: UART受控模式下Modbus配置仅在上电后500ms内有效。")
                    print("      如需配置，请重新上电传感器后立即运行脚本。\n")
                    print("      或者使用 --wait-power 参数让脚本自动等待上电\n")
            else:
                print("跳过传感器配置\n")

            print("按 Ctrl+C 停止程序\n")

            while True:
                timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
                distance, success, error_msg = read_sensor(ser)

                if success:
                    print(f"[{timestamp}] 距离: {distance} mm")
                else:
                    print(f"[{timestamp}] 错误: {error_msg}")

                # 等待下一次读取
                time.sleep(READ_INTERVAL)

    except serial.SerialException as e:
        print(f"串口打开失败: {e}")
        print(f"请检查 {serial_port} 是否被占用或设备是否正确连接")
    except KeyboardInterrupt:
        print("\n程序已停止")


if __name__ == '__main__':
    main()
