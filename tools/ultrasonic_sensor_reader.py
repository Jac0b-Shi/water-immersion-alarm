#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
超声波液位传感器测试脚本 (L07A传感器)
串口配置: 115200/8/N/1
数据格式: 4字节帧 [帧头0xFF, Data_H, Data_L, SUM校验和]

说明:
  - 串口底层是有序字节流，一帧可能被拆成多次 read() 返回
  - 但拼接后的字节顺序仍应是完整4字节帧，不能重排成其他顺序
  - 特殊值 0xFFFE 表示同频干扰，0xFFFD 表示未检测到物体

硬件参数定义:
  - 最大有效距离: 默认从 config.env 的 ULTRASONIC_MAX_DISTANCE_MM 读取
  - 波特率: 默认从 config.env 的 ULTRASONIC_UART_BAUDRATE 读取
  - 数据位: 8位
  - 校验: 无
  - 停止位: 1位

使用方法:
    python ultrasonic_sensor_reader.py -p COM3
    python ultrasonic_sensor_reader.py --port COM4
    python ultrasonic_sensor_reader.py --config ..\\config.env
    python ultrasonic_sensor_reader.py --max-distance 4000
    python ultrasonic_sensor_reader.py -p COM3 --window-size 10 --trim-ratio 0.1
"""

import serial
import time
import argparse
import os
from collections import deque
from typing import Optional, Tuple, List


# ==================== 传感器硬件参数定义 ====================
DEFAULT_PORT = 'COM3'           # 默认串口号
SENSOR_BAUD_RATE = 115200       # 波特率
MAX_VALID_DISTANCE_MM = 3000    # 最大有效距离(mm)
TIMEOUT = 1                     # 读取超时时间(秒)

# ==================== 默认采样窗口参数 ====================
DEFAULT_WINDOW_SIZE = 8         # 默认采样窗口大小
DEFAULT_READ_INTERVAL = 0.1     # 默认读取间隔(秒)
DEFAULT_REPORT_INTERVAL = 5.0   # 默认报告间隔(秒)
DEFAULT_TRIM_RATIO = 0.1        # 默认修剪比例(10%)


def load_config(config_path: Optional[str] = None) -> dict:
    """从 config.env 加载配置"""
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
        except OSError as exc:
            print(f"加载配置文件失败: {exc}")
    return config


class UltrasonicSensor:
    """超声波传感器类，封装传感器操作"""

    def __init__(
        self,
        port: str,
        baudrate: int = SENSOR_BAUD_RATE,
        timeout: float = TIMEOUT,
        max_valid_distance_mm: int = MAX_VALID_DISTANCE_MM
    ):
        """
        初始化传感器
        
        Args:
            port: 串口号
            baudrate: 波特率
            timeout: 超时时间(秒)
        """
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.max_valid_distance_mm = max_valid_distance_mm
        self.ser: Optional[serial.Serial] = None

    def open(self) -> bool:
        """打开串口连接"""
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=self.timeout
            )
            return True
        except serial.SerialException as e:
            print(f"串口打开失败: {e}")
            return False

    def close(self):
        """关闭串口连接"""
        if self.ser and self.ser.is_open:
            self.ser.close()
            self.ser = None

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False

    def trigger_measurement(self) -> bool:
        """
        触发传感器进行测量
        向UART发送触发信号(0x01)
        """
        if not self.ser or not self.ser.is_open:
            return False
        try:
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
            self.ser.write(b'\x01')
            return True
        except serial.SerialException:
            return False

    def parse_data(self, data: bytes) -> Tuple[Optional[int], bool, Optional[str]]:
        """
        解析传感器返回数据
        验证数据格式和校验，并提取距离信息
        
        数据格式: [帧头0xFF][Data_H][Data_L][SUM校验和]
        校验和计算: (header + data_h + data_l) & 0xFF
        
        Args:
            data: 原始字节数据
            
        Returns:
            (距离值mm, 是否成功, 错误信息)
        """
        if len(data) < 4:
            return None, False, f"数据长度不足: 期望至少4字节, 实际{len(data)}字节"

        # 在数据中搜索帧头0xFF
        start_idx = -1
        for i in range(len(data) - 3):
            if data[i] == 0xFF:
                start_idx = i
                break

        if start_idx < 0 or start_idx + 4 > len(data):
            return None, False, "未找到有效的数据帧头(0xFF)"

        header = data[start_idx]
        data_h = data[start_idx + 1]
        data_l = data[start_idx + 2]
        checksum = data[start_idx + 3]

        # 计算校验和
        calc_sum = (header + data_h + data_l) & 0xFF

        if checksum != calc_sum:
            return None, False, f"校验和错误: 期望0x{calc_sum:02X}, 实际0x{checksum:02X}"

        # 计算距离值
        distance = (data_h << 8) | data_l

        # 检查特殊值
        if distance == 0xFFFE:
            return None, False, "检测到同频干扰"
        if distance == 0xFFFD:
            return None, False, "未检测到物体"

        # 检查最大有效距离
        if distance > self.max_valid_distance_mm:
            return None, False, f"距离超出有效范围: {distance}mm > {self.max_valid_distance_mm}mm"

        return distance, True, None

    def read_raw(self, wait_time: float = 0.05) -> Tuple[Optional[int], bool, Optional[str]]:
        """
        触发测量并读取原始数据
        
        Args:
            wait_time: 触发后等待响应的时间(秒)
            
        Returns:
            (距离值mm, 是否成功, 错误信息)
        """
        if not self.trigger_measurement():
            return None, False, "发送触发信号失败"

        time.sleep(wait_time)

        try:
            # 读取可用数据。一次read()可能只拿到部分字节，但顺序应保持原始串口顺序。
            data = self.ser.read(self.ser.in_waiting or 4)
            return self.parse_data(data)
        except serial.SerialException as e:
            return None, False, f"串口错误: {e}"


def compute_trimmed_mean(samples: List[int], trim_ratio: float = DEFAULT_TRIM_RATIO) -> Tuple[Optional[int], bool]:
    """
    计算一组16位无符号整数的修剪平均值
    去除最高和最低的trim_ratio比例样本以减少异常值影响
    
    Args:
        samples: 样本列表
        trim_ratio: 修剪比例(0.0-0.5)，默认0.1表示去除10%最高和最低值
        
    Returns:
        (修剪平均值, 是否成功)
    """
    if not samples or trim_ratio < 0 or trim_ratio >= 0.5:
        return None, False

    count = len(samples)
    if count == 0:
        return None, False

    # 排序
    sorted_samples = sorted(samples)

    # 计算修剪数量
    trim_count = int(count * trim_ratio)
    if trim_count * 2 >= count:
        trim_count = 0

    # 计算修剪后的平均值
    trimmed_samples = sorted_samples[trim_count:count - trim_count]
    if not trimmed_samples:
        trimmed_samples = sorted_samples

    # 使用整数运算避免浮点数，并四舍五入
    total = sum(trimmed_samples)
    used = len(trimmed_samples)
    mean = (total + used // 2) // used

    return mean, True


class SampleWindow:
    """采样窗口类，管理滑动窗口采样"""

    def __init__(self, size: int = DEFAULT_WINDOW_SIZE, trim_ratio: float = DEFAULT_TRIM_RATIO):
        """
        初始化采样窗口
        
        Args:
            size: 窗口大小
            trim_ratio: 修剪比例
        """
        self.size = size
        self.trim_ratio = trim_ratio
        self.samples: deque = deque(maxlen=size)
        self.start_time: Optional[float] = None

    def reset(self):
        """重置采样窗口"""
        self.samples.clear()
        self.start_time = None

    def add_sample(self, value: int) -> bool:
        """
        添加样本到窗口
        
        Args:
            value: 距离值(mm)
            
        Returns:
            窗口是否已满
        """
        if self.start_time is None:
            self.start_time = time.time()
        self.samples.append(value)
        return len(self.samples) >= self.size

    def is_full(self) -> bool:
        """检查窗口是否已满"""
        return len(self.samples) >= self.size

    def get_filtered_value(self) -> Tuple[Optional[int], bool]:
        """
        获取滤波后的值
        
        Returns:
            (滤波后的值, 是否成功)
        """
        if len(self.samples) == 0:
            return None, False
        return compute_trimmed_mean(list(self.samples), self.trim_ratio)

    def get_elapsed_time(self) -> float:
        """获取窗口开始后的经过时间(秒)"""
        if self.start_time is None:
            return 0.0
        return time.time() - self.start_time

    def get_stats(self) -> dict:
        """获取窗口统计信息"""
        samples_list = list(self.samples)
        return {
            'count': len(samples_list),
            'min': min(samples_list) if samples_list else 0,
            'max': max(samples_list) if samples_list else 0,
            'avg': sum(samples_list) // len(samples_list) if samples_list else 0,
            'elapsed_ms': self.get_elapsed_time() * 1000
        }


def main():
    """主函数"""
    parser = argparse.ArgumentParser(
        description='超声波液位传感器测试程序 (L07A)',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
滤波算法说明:
  使用10%修剪平均值(Trimmed Mean)滤波，去除最高和最低10%的异常值
  
采样模式:
  1. 简单模式: 每次读取直接输出
  2. 窗口模式: 收集N个样本后计算滤波值输出

使用示例:
  # 简单读取模式
  python ultrasonic_sensor_reader.py -p COM3
  
  # 窗口采样模式(窗口大小10，每0.1秒采样一次，每1秒报告)
  python ultrasonic_sensor_reader.py -p COM3 -w 10 -i 0.1 -r 1.0
  
  # 从 config.env 读取波特率和最大有效距离
  python ultrasonic_sensor_reader.py -p COM3 --config ..\\config.env

  # 调整修剪比例(去除20%极值)
  python ultrasonic_sensor_reader.py -p COM3 -w 10 --trim-ratio 0.2

  # 直接覆盖最大有效距离
  python ultrasonic_sensor_reader.py -p COM3 --max-distance 4000
        '''
    )
    parser.add_argument(
        '-p', '--port',
        default=DEFAULT_PORT,
        help=f'串口号 (默认: {DEFAULT_PORT})'
    )
    parser.add_argument(
        '--config',
        help='指定 config.env 路径'
    )
    parser.add_argument(
        '--baudrate',
        type=int,
        default=None,
        help='串口波特率，默认优先读取 config.env 中的 ULTRASONIC_UART_BAUDRATE'
    )
    parser.add_argument(
        '--max-distance',
        type=int,
        default=None,
        help='最大有效距离(mm)，默认优先读取 config.env 中的 ULTRASONIC_MAX_DISTANCE_MM'
    )
    parser.add_argument(
        '-w', '--window-size',
        type=int,
        default=0,
        help='采样窗口大小(0表示不使用窗口模式，直接输出每次读取值)'
    )
    parser.add_argument(
        '-i', '--interval',
        type=float,
        default=DEFAULT_READ_INTERVAL,
        help=f'采样间隔(秒) (默认: {DEFAULT_READ_INTERVAL})'
    )
    parser.add_argument(
        '-r', '--report-interval',
        type=float,
        default=DEFAULT_REPORT_INTERVAL,
        help=f'报告间隔(秒)，仅窗口模式有效 (默认: {DEFAULT_REPORT_INTERVAL})'
    )
    parser.add_argument(
        '--trim-ratio',
        type=float,
        default=DEFAULT_TRIM_RATIO,
        help=f'修剪比例(0.0-0.5)，用于去除极值 (默认: {DEFAULT_TRIM_RATIO})'
    )
    parser.add_argument(
        '-v', '--verbose',
        action='store_true',
        help='显示调试信息'
    )
    args = parser.parse_args()

    config = load_config(args.config)
    baudrate = args.baudrate or int(config.get('ULTRASONIC_UART_BAUDRATE', SENSOR_BAUD_RATE))
    max_valid_distance_mm = args.max_distance or int(config.get('ULTRASONIC_MAX_DISTANCE_MM', MAX_VALID_DISTANCE_MM))

    print("=" * 60)
    print("超声波液位传感器测试程序 (L07A)")
    print("=" * 60)
    print(f"串口: {args.port}")
    print(f"波特率: {baudrate}")
    print(f"最大有效距离: {max_valid_distance_mm}mm")

    use_window_mode = args.window_size > 0
    if use_window_mode:
        print(f"\n[窗口采样模式]")
        print(f"  窗口大小: {args.window_size}")
        print(f"  采样间隔: {args.interval}秒")
        print(f"  报告间隔: {args.report_interval}秒")
        print(f"  修剪比例: {args.trim_ratio * 100:.0f}%")
    else:
        print(f"\n[简单读取模式]")
        print(f"  读取间隔: {args.interval}秒")

    print("-" * 60)
    print("按 Ctrl+C 停止程序\n")

    try:
        with UltrasonicSensor(
            args.port,
            baudrate=baudrate,
            max_valid_distance_mm=max_valid_distance_mm
        ) as sensor:
            if not sensor.ser:
                return

            print(f"成功打开串口 {args.port}\n")

            if use_window_mode:
                # 窗口采样模式
                window = SampleWindow(args.window_size, args.trim_ratio)
                last_report_time = time.time()
                consecutive_errors = 0

                while True:
                    # 读取原始数据
                    distance, success, error_msg = sensor.read_raw()
                    timestamp = time.strftime("%Y-%m-%d %H:%M:%S")

                    if success:
                        consecutive_errors = 0
                        window_full = window.add_sample(distance)

                        if args.verbose:
                            print(f"[{timestamp}] 采样: {distance} mm "
                                  f"(窗口 {len(window.samples)}/{args.window_size})")

                        # 检查是否需要报告
                        current_time = time.time()
                        time_since_last_report = current_time - last_report_time

                        # 窗口满或达到报告间隔时报告
                        if window_full or (time_since_last_report >= args.report_interval and len(window.samples) > 0):
                            filtered_value, filter_success = window.get_filtered_value()
                            stats = window.get_stats()

                            if filter_success:
                                print(f"[{timestamp}] " + "=" * 40)
                                print(f"  滤波距离: {filtered_value} mm")
                                print(f"  窗口统计: 样本数={stats['count']}, "
                                      f"范围=[{stats['min']}-{stats['max']}]mm, "
                                      f"原始平均={stats['avg']}mm")
                                print(f"  窗口耗时: {stats['elapsed_ms']:.1f}ms")
                                print("=" * 55)
                            else:
                                print(f"[{timestamp}] 滤波计算失败")

                            # 重置窗口
                            window.reset()
                            last_report_time = current_time
                    else:
                        consecutive_errors += 1
                        if args.verbose or consecutive_errors <= 3:
                            print(f"[{timestamp}] 读取错误: {error_msg}")
                        if consecutive_errors > 10:
                            print(f"[{timestamp}] 连续错误次数过多，程序退出")
                            break

                    time.sleep(args.interval)

            else:
                # 简单读取模式
                consecutive_errors = 0

                while True:
                    timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
                    distance, success, error_msg = sensor.read_raw()

                    if success:
                        consecutive_errors = 0
                        print(f"[{timestamp}] 距离: {distance} mm")
                    else:
                        consecutive_errors += 1
                        if args.verbose or consecutive_errors <= 3:
                            print(f"[{timestamp}] 错误: {error_msg}")
                        if consecutive_errors > 10:
                            print(f"[{timestamp}] 连续错误次数过多，程序退出")
                            break

                    time.sleep(args.interval)

    except KeyboardInterrupt:
        print("\n\n程序已停止")
    except Exception as e:
        print(f"\n程序异常: {e}")


if __name__ == '__main__':
    main()
