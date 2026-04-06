# 浸水检测报警系统

[![License: LGPL v3](https://img.shields.io/badge/License-LGPL%20v3-blue.svg)](https://www.gnu.org/licenses/lgpl-3.0)
[![Platform](https://img.shields.io/badge/Platform-CH32V208-green.svg)](https://www.wch.cn/)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)](https://gitee.com/SHU-SPE-Sandrone/water-immersion-alarm)

基于 CH32V208WBU6 的浸水检测报警系统，当前维护 `BC260 NB-IoT` 与 `CH32V208 内置以太网` 两种通信方式。

> 当前维护范围不包含 ESP8266。代码中仍保留部分历史条件编译分支，但不再作为有效功能路径维护。

## 当前状态

- 维护目标：`CH32V208WBU6`
- RTOS：`FreeRTOS`
- 通信路径：`BC260 NB-IoT`、`内置以太网`
- 配置流程：`config.env -> generate_config.py -> User/config.h -> 重新编译`
- 推送方式：设备通过 HTTP 代理转发到企业微信 HTTPS Webhook

## 🚀 版本更新 (V1.2.0)

**重要更新**：修复BC260 NB-IoT模块稳定性问题，支持高电平复位模式。

### V1.2.0 修复内容
- ✅ **BC260 RST引脚修复** - 支持高电平复位模式，提高初始化稳定性
- ✅ **HTTP响应解析优化** - 正确处理+QIURC数据上报格式
- ✅ **AT+QISEND时序修复** - 正确处理OK和>提示符的时序关系
- ✅ **增加详细调试输出** - HTTP请求/响应内容预览便于排查问题

### V1.1.0 新增功能
- ✅ **CH32V208内置以太网支持** - 直接RJ45连接，无需额外WiFi模块
- ✅ **HTTP代理转发功能** - 解决以太网无法直接访问HTTPS的问题
- ✅ **模块化通信架构** - 当前维护 BC260 NB-IoT 与内置以太网
- ✅ **增强的调试功能** - 更丰富的日志输出和状态监控

### 技术改进
- 🔄 **重构配置管理系统** - 统一的config.h配置文件
- 🔄 **优化DMA通信机制** - 更稳定的串口数据传输
- 🔄 **改进错误处理** - 更完善的异常恢复机制
- 🔄 **增强安全防护** - 敏感信息自动保护

## 功能特性

### 核心功能
- 🌊 **实时浸水检测**：通过ADC模拟量输入检测水位，阈值可配置（默认1000mV）
- 💡 **双LED状态指示**：共阳极LED分别指示正常/报警状态
- 📱 **企业微信推送**：通过BC260 NB-IoT或内置以太网推送报警信息
- 🔄 **自动状态管理**：智能检测浸水/解除状态变化，避免重复报警

### 通信功能
- **USART1**：调试信息输出（115200波特率）
- **USART2**：BC260 NB-IoT AT指令通信（9600波特率）
- **内置以太网**：CH32V208特有10M以太网（需HTTP代理）
- **多模块支持**：当前维护 NB-IoT/以太网两种通信方式

### 系统特性
- ⚡ **FreeRTOS支持**：基于实时操作系统，系统响应快速稳定
- 🔌 **硬件复位控制**：可通过GPIO控制BC260硬件复位
- 📊 **状态监控**：输出传感器状态、网络附着状态、链路状态和调试日志
- 🔍 **详细调试信息**：可选的详细日志输出，便于开发调试

## 硬件要求

### 主控芯片
- **型号**：CH32V208WBU6
- **架构**：RISC-V 32位内核
- **主频**：120MHz
- **Flash**：480KB
- **RAM**：64KB
- **特性**：CH32V208支持内置10M以太网

### 外设模块
- **NB-IoT模块**：Quectel BC260Y-CN（支持移动/电信NB-IoT网络）
- **以太网**：CH32V208内置10M以太网 + RJ45接口
- **浸水传感器**：模拟量输出型（0-3.3V）
- **LED指示灯**：共阳极LED × 2
- **电源**：3.3V稳压电源

## 引脚连接

### CH32V208引脚定义

| 功能 | 引脚 | 说明 |
|------|------|------|
| **传感器输入** | PA0 | ADC输入，检测浸水传感器电压值 |
| **无水指示灯** | PB0 | 共阳极LED，低电平点亮 |
| **浸水指示灯** | PB1 | 共阳极LED，低电平点亮 |
| **调试串口TX** | PA9 | USART1发送，115200波特率 |
| **调试串口RX** | PA10 | USART1接收，115200波特率 |
| **BC260 TX** | PA2 | USART2发送数据到BC260 |
| **BC260 RX** | PA3 | USART2接收BC260数据 |
| **BC260复位** | PA1 | GPIO输出，高电平复位 |

### BC260 NB-IoT连接

| BC260引脚 | CH32V208引脚 | 说明 |
|-----------|------------|------|
| TXD       | PA3        | 串口发送（接MCU RX） |
| RXD       | PA2        | 串口接收（接MCU TX） |
| RESET_N   | PA1        | 硬件复位（高电平有效） |
| VCC       | 3.3V       | 电源 |
| GND       | GND        | 地 |
| SIM卡     | -          | 插入NB-IoT专用SIM卡 |

> **注意**：BC260 的 RESET_N 引脚是高电平复位。

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

配置文件内容示例：

```env
# 企业微信 Webhook 配置
WEBHOOK_KEY=你的企业微信机器人Key

# BC260 NB-IoT HTTP代理配置（使用NB-IoT时填写）
# 注意：BC260不支持HTTPS，需要通过HTTP代理转发
BC260_PROXY_IP=你的公网代理服务器IP
BC260_PROXY_PORT=8080

# 以太网HTTP代理配置（使用内置以太网时填写）
HTTP_PROXY_IP=192.168.1.100
HTTP_PROXY_PORT=8080

# 模块启用开关
ENABLE_ESP8266=0    # 已弃用，不建议启用
ENABLE_BC260=1      # 1=启用BC260 NB-IoT模块
ENABLE_ETHERNET=0   # 1=启用内置以太网

# UDP密钥验证（建议生产环境启用）
UDP_SECRET_KEY=your_secret_key_here
```

生成配置头文件：

```bash
# 运行配置生成脚本
python generate_config.py config.env User/config.h
```

> **注意**：`config.env` 和 `User/config.h` 已添加到 `.gitignore`，不会被提交到版本控制系统。

其他编译期参数（在 `User/main.c` 中）：

```c
// 浸水检测阈值（单位：mV）
#define WATER_THRESHOLD_MV 1000      // 判定为浸水的电压阈值
#define NO_WATER_THRESHOLD_MV 500    // 判定为无水的电压阈值

// 调试模式
#define DEBUG_MODE 0                 // 1=启用详细调试输出，0=精简日志
```

### 3. 构建项目

#### 使用CMake（推荐）

```bash
# 配置
cmake -B build -G Ninja

# 编译
cmake --build build --target water-immersion-alarm.elf
```

> 配置阶段会输出 `Compile target: CH32V208WBU6`。如果修改了 `config.env`，请先重新生成 `User/config.h`，必要时重新运行 CMake 配置。

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

使用WCH-Link工具烧录生成的 `.hex` 或 `.elf` 文件到 CH32V208。

### 5. 查看运行状态

通过串口工具（115200, 8N1）连接USART1查看系统运行日志。

## 使用说明

### 系统启动流程

根据启用的通信模块，启动流程如下：

**BC260 NB-IoT模式：**
1. **初始化硬件**：GPIO、ADC、USART1/2等外设
2. **BC260初始化**：硬件复位（高电平复位）、AT指令测试
3. **SIM卡检测**：验证SIM卡状态
4. **网络附着**：附着到移动网络，获取信号强度
5. **发送启动通知**：通过HTTP代理推送系统启动消息
6. **进入监测循环**：每5秒检测一次浸水状态

**以太网模式：**
1. **初始化硬件**：GPIO、ADC、以太网MAC等
2. **以太网初始化**：初始化WCHNET协议栈
3. **等待PHY链接**：检测网线连接状态
4. **发送启动通知**：通过HTTP代理推送系统启动消息
5. **进入监测循环**：每5秒检测一次浸水状态

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
通信方式: BC260 或 Ethernet
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

## 代理服务器部署

由于CH32V208和BC260不支持SSL/TLS，需要通过HTTP代理服务器转发HTTPS请求到企业微信。

### 使用Systemd部署（推荐）

#### 1. 复制代理脚本到服务器

```bash
# 将 webhook_proxy.py 复制到服务器
scp tools/webhook_proxy.py ubuntu@your-server:/home/ubuntu/
```

#### 2. 创建Systemd服务文件

```bash
sudo nano /etc/systemd/system/webhook-proxy.service
```

写入以下内容（替换为你的实际配置）：

```ini
[Unit]
Description=Webhook Proxy for Water Immersion Alarm
After=network.target

[Service]
Type=simple
User=ubuntu
WorkingDirectory=/home/ubuntu
Environment="WEBHOOK_KEY=your_webhook_key_here"
Environment="UDP_SECRET_KEY=your_secret_key_here"
ExecStart=/usr/bin/python3 /home/ubuntu/webhook_proxy.py
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

#### 3. 多密钥配置（多设备场景）

支持多个传感器使用不同密钥，用逗号分隔：

```ini
Environment="UDP_SECRET_KEY=DEVICE_A_KEY,DEVICE_B_KEY,DEVICE_C_KEY"
```

#### 4. 启动和管理服务

```bash
# 重新加载systemd配置
sudo systemctl daemon-reload

# 设置开机自启
sudo systemctl enable webhook-proxy.service

# 启动服务
sudo systemctl start webhook-proxy.service

# 查看状态
sudo systemctl status webhook-proxy.service

# 查看实时日志
sudo journalctl -u webhook-proxy.service -f

# 重启服务
sudo systemctl restart webhook-proxy.service

# 停止服务
sudo systemctl stop webhook-proxy.service
```

### UDP密钥验证

为防止伪造消息，建议启用UDP密钥验证：

1. **服务器配置**：在 `config.env` 或 systemd 环境变量中设置 `UDP_SECRET_KEY`
2. **设备配置**：每个设备在 `config.env` 中配置对应的密钥
3. **数据包格式**：`[IMEI]:[密钥][3字节HEX数据]`，例如 `869999051234567:MYKEY089209`

消息中包含设备IMEI，企业微信推送会显示设备标识，方便识别多个传感器。

## 调试信息

### 启用详细日志

在 `User/main.c` 中修改：

```c
#define DEBUG_MODE 1  // 启用详细调试输出
```

### 日志级别

- `[INFO]`：一般信息
- `[DEBUG]`：调试信息
- `[WARN]`：警告信息
- `[ERROR]`：错误信息

## 常见问题

### Q: BC260初始化失败？
A:
1. 检查串口连接：TX(PA2)→BC260 RX, RX(PA3)→BC260 TX
2. 确认SIM卡正确插入且已开通NB-IoT服务（非普通4G）
3. 检查信号强度（AT+CSQ返回0-31，越大越好）
4. 确认RST引脚连接正确（PA1→RESET_N，高电平复位）
5. 检查模块供电是否稳定（3.3V，建议使用独立LDO）

### Q: BC260 RST复位异常？
A:
1. 确认硬件连接：PA1连接到BC260的RESET_N引脚
2. BC260的RESET_N是**高电平复位**，确保有下拉电阻保持低电平
3. 如果复位后无法通信，尝试拔掉RST线手动复位测试
4. 检查是否有其他设备占用RST引脚

### Q: NB-IoT网络附着失败？
A:
1. 确认SIM卡已开通NB-IoT服务（联系运营商确认）
2. 检查APN设置（中国移动使用`cmnbiot`）
3. 确认所在位置有NB-IoT信号覆盖
4. 尝试将模块移动到信号更好的位置
5. 检查是否欠费或SIM卡被锁定

### Q: 消息推送失败（NB-IoT）？
A:
1. 检查HTTP代理服务器是否运行正常
2. 确认代理服务器IP和端口配置正确
3. 验证代理服务器可以访问企业微信HTTPS接口
4. 使用`tools/bc260_test.py`脚本单独测试通信
5. 检查串口日志中的HTTP响应内容

### Q: 消息推送失败？
A:
1. 检查企业微信Webhook Key是否正确
2. 检查网络连接和代理服务是否正常
3. 确认 `config.env` 中代理地址、端口和密钥配置正确
4. 查看串口日志中的 HTTP 响应、附着状态或链路状态

### Q: 误报警？
A:
1. 调整 `WATER_THRESHOLD_MV` 阈值
2. 检查传感器接线
3. 确认传感器工作正常
4. 避免电磁干扰

## 技术特点

### DMA通信
- 串口收发采用 DMA/缓冲区机制，降低 CPU 轮询开销
- 使用固定大小缓冲区避免连续响应丢包
- 配合调试日志便于分析 AT 响应和网络收发行为

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
5. 复制 `key=` 后面的字符串，填入 `config.env` 中的 `WEBHOOK_KEY`

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

### 代理转发模型

设备端不直接访问企业微信 HTTPS 接口，而是先将 HTTP/UDP 请求发送到代理，再由代理转发到企业微信：

1. 设备采集状态并生成消息
2. 通过 BC260 或以太网发送到代理
3. 代理校验密钥并解析设备标识
4. 代理转发到企业微信 Webhook

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

| 指标 | BC260 模式 | 以太网模式 |
|------|------------|------------|
| 检测周期 | 5秒 | 5秒 |
| ADC分辨率 | 12位 | 12位 |
| 电压测量范围 | 0-3.3V | 0-3.3V |
| 响应时间 | 10-30秒 | <5秒 |
| 网络建立时间 | 30-60秒 | 即时 |
| HTTPS能力 | 需HTTP代理 | 需HTTP代理 |
| 典型场景 | 无有线网络、远程部署 | 有局域网或固定布线 |

## 调试技巧

### 1. 启用详细调试模式

在 `User/main.c` 中修改：

```c
#define DEBUG_MODE 1  // 启用详细调试输出
```

这将显示：
- AT指令发送和响应详情
- HTTP请求/响应内容预览
- 原始十六进制数据

### 2. 使用Python脚本测试BC260

项目提供了BC260测试脚本，可在PC上验证模块功能：

```bash
# 基本AT测试
python tools/bc260_test.py --port COM3

# 自动加载配置并测试消息发送
python tools/bc260_test.py --port COM3 --auto-config

# 详细调试模式
python tools/bc260_test.py --port COM3 --auto-config --verbose
```

### 3. BC260调试命令

通过USART1交互模式或代码中添加以下命令：

```c
// 测试AT通信
BC260_SendCommand("AT", "OK", 2000);

// 查询信号强度
BC260_SendCommand("AT+CSQ", "OK", 2000);

// 查询网络附着状态
BC260_SendCommand("AT+CGATT?", "OK", 2000);

// 查询模块信息
BC260_SendCommand("ATI", "OK", 2000);

// 查询SIM卡状态
BC260_SendCommand("AT+CIMI", "OK", 2000);
```

### 4. 检查HTTP代理连接

```c
// 测试TCP连接到代理服务器
BC260_SendCommand("AT+QIOPEN=0,0,\"TCP\",\"x.x.x.x\",8080", "OK", 15000);

// 查询连接状态
BC260_SendCommand("AT+QISTATE?", "OK", 3000);

// 关闭连接
BC260_SendCommand("AT+QICLOSE=0", "OK", 5000);
```

## 优化建议

### 降低功耗
1. 延长检测间隔（修改`CHECK_INTERVAL_MS`）
2. 使用低功耗模式（需要硬件支持）
3. 按需启停通信模块，减少无效网络建立

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
│   ├── main.c             # 主程序（2900+行，支持多通信模块）
│   ├── ch32v20x_conf.h    # 外设配置
│   ├── ch32v20x_it.c      # 中断处理
│   ├── eth_driver.c/h     # 以太网驱动（CH32V208）
│   └── FreeRTOSConfig.h   # RTOS配置
├── tools/                  # 调试工具脚本
│   ├── bc260_test.py      # BC260 NB-IoT测试脚本
│   ├── bc260_diagnose.py  # BC260诊断工具
│   └── test_proxy.py      # HTTP代理测试
├── Core/                   # 核心启动代码
├── Debug/                  # 调试支持
├── FreeRTOS/              # FreeRTOS源码
├── Peripheral/            # 外设驱动库
├── Startup/               # 启动文件
├── Ld/                    # 链接脚本
├── config.env.example     # 配置模板
├── generate_config.py     # 配置生成脚本
├── CMakeLists.txt         # CMake构建配置
├── package.json           # NPM脚本
├── LICENSE                # LGPL-3.0许可证
└── README.md              # 本文件
```

## 开发计划

- [x] **BC260 NB-IoT支持** (V1.2.0已完成)
- [x] **HTTP代理转发** (V1.1.0已完成)
- [x] **CH32V208内置以太网** (V1.1.0已完成)
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

基于WCH官方 CH32V20x 示例代码修改而来，当前仅维护 CH32V208 目标。
Original work Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.

## 致谢

- [WCH](https://www.wch.cn/) - CH32V208芯片和开发工具
- [Quectel](https://www.quectel.com/) - BC260 NB-IoT模块
- [FreeRTOS](https://www.freertos.org/) - 实时操作系统

## 联系方式

- **作者**：Jac0b_Shi (SHU-SPE-Sandrone)
- **仓库地址**：https://gitee.com/SHU-SPE-Sandrone/water-immersion-alarm
- **问题反馈**：https://gitee.com/SHU-SPE-Sandrone/water-immersion-alarm/issues

---

**⚠️ 安全提示**：
- `config.env` 包含敏感信息，已添加到 `.gitignore`，切勿上传到公开仓库
- 定期更换 Webhook Key 和 UDP 密钥
- 生产环境使用时请做好备份和容错措施
- 本项目仅供学习交流使用，请根据实际情况评估安全性后再用于生产环境

**⚠️ 硬件注意事项**：
- **BC260 RST引脚**：高电平复位，确保有下拉电阻（建议10KΩ）保持默认低电平
- **电源稳定性**：BC260 峰值电流较大，建议使用独立 LDO 供电
- **SIM卡安全**：NB-IoT SIM卡与设备绑定，请妥善保管
