# 浸水检测报警系统

[![License: LGPL v3](https://img.shields.io/badge/License-LGPL%20v3-blue.svg)](https://www.gnu.org/licenses/lgpl-3.0)
[![Platform](https://img.shields.io/badge/Platform-CH32V203-green.svg)](https://www.wch.cn/)

基于CH32V203 RISC-V微控制器和ESP8266 WiFi模块的智能浸水检测报警系统，支持实时监测、企业微信推送等功能。

## 功能特性

### 核心功能
- 🌊 **实时浸水检测**：通过ADC模拟量输入检测水位，阈值可配置（默认1000mV）
- 💡 **双LED状态指示**：共阳极LED分别指示正常/报警状态
- 📱 **企业微信推送**：通过ESP8266模块实时推送报警信息到企业微信群
- 🔄 **自动状态管理**：智能检测浸水/解除状态变化，避免重复报警

### 通信功能
- **USART1**：调试信息输出（115200波特率）
- **USART3**：ESP8266 AT指令通信（115200波特率，DMA模式）
- **WiFi连接**：自动连接配置的WiFi网络
- **HTTPS通信**：通过SSL加密连接企业微信Webhook API

### 系统特性
- ⚡ **FreeRTOS支持**：基于实时操作系统，系统响应快速稳定
- 🔌 **硬件复位控制**：可通过GPIO控制ESP8266硬件复位
- 📊 **状态监控**：实时显示WiFi连接状态、IP地址、信号强度
- 🔍 **详细调试信息**：可选的详细日志输出，便于开发调试

## 硬件要求

### 主控芯片
- **型号**：CH32V203C8T6
- **架构**：RISC-V 32位内核
- **主频**：96MHz
- **Flash**：64KB
- **RAM**：20KB

### 外设模块
- **WiFi模块**：ESP01S (ESP8266芯片)（AT固件v2.2.0及以上）
- **浸水传感器**：模拟量输出型（0-3.3V）
- **LED指示灯**：共阳极LED × 2
- **电源**：3.3V稳压电源

## 引脚连接

### CH32V203引脚定义

| 功能 | 引脚 | 说明 |
|------|------|------|
| **传感器输入** | PA0 | ADC输入，检测浸水传感器电压值 |
| **无水指示灯** | PB0 | 共阳极LED，低电平点亮 |
| **浸水指示灯** | PB1 | 共阳极LED，低电平点亮 |
| **调试串口TX** | PA9 | USART1发送，115200波特率 |
| **调试串口RX** | PA10 | USART1接收，115200波特率 |
| **ESP8266 TX** | PB10 | USART3接收ESP8266数据 |
| **ESP8266 RX** | PB11 | USART3发送数据到ESP8266 |
| **ESP8266复位** | PA8 | GPIO输出，控制ESP8266硬件复位 |

### ESP8266连接

| ESP8266引脚 | CH32V203引脚 | 说明 |
|-----------|------------|------|
| TX        | PB10       | 串口发送 |
| RX        | PB11       | 串口接收 |
| RST       | PA8        | 硬件复位 |
| VCC       | 3.3V       | 电源（需要足够电流） |
| GND       | GND        | 地 |
| GPIO0     | 悬空/GND     | 使能引脚 |

## 软件环境

### 开发工具
- **IDE**：CLion 2025+
- **编译器**：RISC-V GCC 12 (MounRiver Studio 工具链，首选) / @xpack riscv-none-embed-gcc
- **CMake**：3.20+
- **调试器**：WCH-LinkE / CH340G

### 依赖库
- FreeRTOS（已集成）
- CH32V20x外设驱动库（已集成）

## 快速开始

### 1. 克隆项目

```bash
git clone https://gitee.com/SHU-SPE-Sandrone/water-immersion-alarm.git
cd water-immersion-alarm
```

### 2. 配置参数

项目使用配置文件管理敏感信息，确保安全性：

```bash
# 复制配置模板
cp config.env.example config.env

# 编辑配置文件，填入你的实际配置
# Windows: notepad config.env
# Linux/Mac: nano config.env
```

配置文件内容：

```env
# WiFi 配置
WIFI_SSID=你的WiFi名称
WIFI_PASSWORD=你的WiFi密码

# 企业微信 Webhook 配置
WEBHOOK_KEY=你的企业微信机器人Key
```

生成配置头文件：

```bash
# 运行配置生成脚本
python generate_config.py config.env User/config.h
```

> **注意**：`config.env` 和 `User/config.h` 已添加到 `.gitignore`，不会被提交到版本控制系统。

其他可选配置（在 `User/main.c` 中）：

```c
// 浸水检测阈值（单位：mV）
#define WATER_THRESHOLD_MV 1000      // 判定为浸水的电压阈值
#define NO_WATER_THRESHOLD_MV 500    // 判定为无水的电压阈值
```

### 3. 构建项目

#### 使用CMake（推荐）

```bash
# 配置
cmake -B build -G Ninja

# 编译
cmake --build build --target water-immersion-alarm.elf
```

#### 使用CLion
1. 打开项目
2. 选择配置：`RelWithDebInfo-RISC-V (WCH Toolchain)`
3. 点击构建按钮

#### 工具链路径参考
如果需要手动配置工具链，可以参考以下路径：
```
D:\MounRiver\MounRiver_Studio2\resources\app\resources\win32\components\WCH\Toolchain\RISC-V Embedded GCC12\bin\riscv-wch-elf-gcc.exe
```
这是我 MounRiver Studio 2 的安装路径。

### 4. 烧录固件

使用WCH-Link工具烧录生成的 `.hex` 或 `.elf` 文件到CH32V203。

### 5. 查看运行状态

通过串口工具（115200, 8N1）连接USART1查看系统运行日志。

## 使用说明

### 系统启动流程

1. **初始化硬件**：GPIO、ADC、USART等外设
2. **ESP8266初始化**：硬件复位、AT指令测试
3. **WiFi连接**：自动连接配置的WiFi网络
4. **获取IP地址**：显示分配的IP和MAC地址
5. **发送启动通知**：通过企业微信推送系统启动消息
6. **进入监测循环**：每5秒检测一次浸水状态

### 状态指示

| LED状态 | 说明 |
|---------|------|
| 绿色LED（PB0）亮 | 正常状态，无浸水 |
| 红色LED（PB1）亮 | 检测到浸水 |
| 两个LED都不亮 | 系统异常或初始化中 |

### 企业微信消息格式

#### 系统启动消息
```
【系统启动】
浸水检测系统已成功启动
WiFi: 你的WiFi名称
阈值: 1000mV
状态: 正常运行
```

#### 浸水警报
```
【严重警报】检测到浸水情况！当前电压：1500mV
```

#### 解除警报
```
【解除警报】浸水情况已解除，当前电压：500mV
```

## 调试信息

### 启用详细日志

在 `main.c` 中取消注释：

```c
#define DEBUG_VERBOSE  // 启用详细调试信息
```

### 日志级别

- `[INFO]`：一般信息
- `[DEBUG]`：调试信息
- `[WARN]`：警告信息
- `[ERROR]`：错误信息

## 常见问题

### Q: ESP8266无法连接？
A: 
1. 检查串口连接是否正确（TX-RX交叉）
2. 确认ESP8266供电充足（建议单独供电）
3. 检查AT固件版本（需2.2.0+）
4. 查看串口波特率是否为115200

### Q: WiFi连接失败？
A:
1. 确认SSID和密码配置正确
2. 检查WiFi信号强度
3. 确认路由器支持2.4GHz频段
4. 查看ESP8266是否已保存旧的WiFi配置

### Q: 消息推送失败？
A:
1. 检查企业微信Webhook Key是否正确
2. 确认网络连接正常
3. 查看SSL连接是否成功建立
4. 检查ESP8266固件是否支持SSL

### Q: 误报警？
A:
1. 调整 `WATER_THRESHOLD_MV` 阈值
2. 检查传感器接线
3. 确认传感器工作正常
4. 避免电磁干扰

## 技术特点

### DMA通信
- USART3采用DMA接收模式，提高数据接收效率
- 256字节循环缓冲区，避免数据丢失
- 自动处理ESP8266的多行响应

### 状态管理
- 智能状态机管理浸水检测
- 防抖处理，避免频繁触发
- 状态变化才触发通知，节省流量

### 错误处理
- 自动重试机制（最多3次）
- 超时检测和恢复
- 详细的错误日志

### 低功耗设计
- FreeRTOS任务休眠
- 按需激活外设
- 定时检测机制

## 企业微信机器人配置

### 创建群机器人

1. 在企业微信中创建一个群聊
2. 点击群聊右上角 `...` -> `群机器人` -> `添加机器人`
3. 填写机器人名称和头像
4. 获取 Webhook 地址（格式：`https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=xxxxxxxx`）
5. 复制 `key=` 后面的字符串，填入代码中的 `WECOM_WEBHOOK_KEY`

### 测试机器人

使用curl命令测试：

```bash
curl 'https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=你的key' \
   -H 'Content-Type: application/json' \
   -d '{
        "msgtype": "text",
        "text": {
            "content": "测试消息"
        }
   }'
```

## 技术细节

### DMA接收机制

系统使用DMA1_Channel3实现USART3的高效数据接收：

- **循环缓冲区**：256字节环形缓冲区
- **自动接收**：无需CPU干预，DMA自动搬运数据
- **实时监控**：通过`DMA_GetCurrDataCounter`实时获取接收进度
- **响应检测**：自动识别`\r\n`结束符、`OK`、`ERROR`等响应

### SSL/TLS连接

ESP8266建立HTTPS连接的过程：

1. DNS解析域名（自动完成）
2. TCP三次握手
3. SSL/TLS握手（需要3-5秒）
4. 发送HTTP请求
5. 接收服务器响应
6. 关闭连接

**注意**：SSL握手比普通TCP连接慢，需要给予足够超时时间。

### 状态防抖算法

为避免传感器信号抖动导致误报：

- **浸水检测**：连续2次检测电压>1000mV才确认
- **无水检测**：连续5次检测电压<500mV才确认
- **中间区域**：500-1000mV保持当前状态不变

### 电压-ADC转换

```
ADC值 = 电压值 × 4095 / 3300
```

例如：
- 0mV → ADC=0
- 1000mV → ADC=1241
- 3300mV → ADC=4095

## 性能指标

| 指标 | 数值 |
|------|------|
| 检测周期 | 5秒 |
| 响应时间 | <10秒（包括网络延迟） |
| ADC分辨率 | 12位（0-4095） |
| 电压测量范围 | 0-3.3V |
| 测量精度 | ±50mV |
| WiFi连接时间 | 3-10秒 |
| SSL握手时间 | 3-5秒 |
| 消息推送时间 | 1-3秒 |

## 常见问题

### Q: ESP8266无法连接？
A: 
1. 检查串口连接是否正确（TX-RX交叉）
2. 确认ESP8266供电充足（建议单独供电）
3. 检查AT固件版本（需2.2.0+）
4. 查看串口波特率是否为115200

### Q: WiFi连接失败？
A:
1. 确认SSID和密码配置正确
2. 检查WiFi信号强度
3. 确认路由器支持2.4GHz频段
4. 查看ESP8266是否已保存旧的WiFi配置

### Q: 消息推送失败？
A:
1. 检查企业微信Webhook Key是否正确
2. 确认网络连接正常
3. 查看SSL连接是否成功建立
4. 检查ESP8266固件是否支持SSL

### Q: 误报警？
A:
1. 调整 `WATER_THRESHOLD_MV` 阈值
2. 检查传感器接线
3. 确认传感器工作正常
4. 避免电磁干扰

### Q: 串口无输出？
A:
1. 检查波特率是否为115200
2. 确认PA9(TX)已正确连接
3. 查看是否选择了SDI模式（需要WCH-Link）
4. 重新烧录固件

### Q: LED不亮？
A:
1. 确认LED极性正确（共阳极）
2. 检查限流电阻是否合适
3. 测量PB0、PB1引脚电平
4. 检查GPIO初始化代码

## 调试技巧

### 1. 查看ESP8266原始响应

启用详细调试模式：

```c
#define DEBUG_MODE 1  // 在main.c中修改
```

### 2. 使用交互模式测试

系统支持USART1交互测试模式：

```
输入: AT
输出: AT命令响应

输入: AT+GMR
输出: 固件版本信息
```

### 3. 检查DMA状态

在代码中添加：

```c
printf("DMA Remaining: %d\r\n", DMA_GetCurrDataCounter(DMA1_Channel3));
printf("Received: %d\r\n", sizeof(esp_rx_buffer) - DMA_GetCurrDataCounter(DMA1_Channel3));
```

### 4. 监控网络连接

```c
ESP8266_SendCommand("AT+CIPSTATUS", 2000);  // 查询连接状态
ESP8266_SendCommand("AT+PING=\"www.baidu.com\"", 5000);  // 测试网络连通性
```

## 优化建议

### 降低功耗
1. 延长检测间隔（修改`CHECK_INTERVAL_MS`）
2. 使用低功耗模式（需要硬件支持）
3. 按需激活ESP8266（使用硬件复位控制）

### 提高稳定性
1. 增加看门狗定时器
2. 添加异常恢复机制
3. 实现心跳检测
4. 记录错误日志

### 增强功能
1. 添加温湿度传感器
2. 支持多个浸水传感器
3. 实现本地数据存储
4. 添加Web配置界面

## 技术支持

如遇到问题，请按以下步骤：

1. 查阅本文档的"常见问题"部分
2. 查看串口调试输出
3. 在[Issues](https://gitee.com/SHU-SPE-Sandrone/water-immersion-alarm/issues)中搜索类似问题
4. 提交新Issue，附带：
   - 硬件配置信息
   - 完整的串口日志
   - 问题复现步骤
   - 已尝试的解决方法

## 项目结构

```
water-immersion-alarm/
├── User/                   # 用户代码
│   ├── main.c             # 主程序（1500+行）
│   ├── ch32v20x_conf.h    # 外设配置
│   ├── ch32v20x_it.c      # 中断处理
│   └── FreeRTOSConfig.h   # RTOS配置
├── Core/                   # 核心启动代码
├── Debug/                  # 调试支持
├── FreeRTOS/              # FreeRTOS源码
├── Peripheral/            # 外设驱动库
├── Startup/               # 启动文件
├── Ld/                    # 链接脚本
├── CMakeLists.txt         # CMake构建配置
├── package.json           # NPM脚本
├── LICENSE                # LGPL-3.0许可证
└── README.md              # 本文件
```

## 开发计划

- [ ] 添加历史数据记录
- [ ] 支持多个传感器
- [ ] Web配置界面
- [ ] OTA固件升级
- [ ] 电池供电支持
- [ ] 温湿度监测

## 贡献指南

欢迎提交Issue和Pull Request！

1. Fork本项目
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 开启Pull Request

## 许可证

本项目采用 LGPL-3.0 许可证，详见 [LICENSE](LICENSE) 文件。

Copyright (c) 2025 SHU-SPE-Sandrone

基于WCH官方CH32V203示例代码修改而来。
Original work Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.

## 致谢

- [WCH](https://www.wch.cn/) - CH32V203芯片和开发工具
- [Espressif](https://www.espressif.com/) - ESP8266 WiFi模块
- [FreeRTOS](https://www.freertos.org/) - 实时操作系统

## 联系方式

- **作者**：Jac0b_Shi (SHU-SPE-Sandrone)
- **仓库地址**：https://gitee.com/SHU-SPE-Sandrone/water-immersion-alarm
- **问题反馈**：https://gitee.com/SHU-SPE-Sandrone/water-immersion-alarm/issues

---

**⚠️ 安全提示**：
- `config.env` 包含敏感信息，已添加到 `.gitignore`，切勿上传到公开仓库
- 定期更换WiFi密码和Webhook Key
- 生产环境使用时请做好备份和容错措施
- 本项目仅供学习交流使用，请根据实际情况评估安全性后再用于生产环境
