---
apply: always
---

# 水浸检测报警系统 - 项目记忆

## 项目定位

- 项目是基于 FreeRTOS 的浸水检测报警固件，主控当前仅维护 `CH32V208WBU6`。
- 项目核心职责是采集浸水传感器模拟量、做状态去抖、通过通信模块上报告警到企业微信。
- `CH32V203C8T6` 已不再是当前维护目标，后续修改应默认围绕 `CH32V208WBU6` 展开。

## 硬件要点

- 传感器输入为 `PA0`，ADC 电压值用于判断浸水状态。
- 状态指示灯为共阳极双 LED，低电平点亮：
  - `PB0` 绿色表示正常
  - `PB1` 红色表示报警
- 调试串口为 `USART1`：
  - `PA9` 为 TX
  - `PA10` 为 RX
  - 波特率 `115200`
- `BC260` 使用 `USART2`：
  - `PA2` 为 TX
  - `PA3` 为 RX
  - `PA1` 为复位
  - `BC260` 复位是高电平有效
- 以太网使用 `CH32V208` 内置 10M Ethernet，驱动位于 `User/eth_driver.c`。

## 通信模块现状

- `ESP8266` 路径已弃用，不再维护。代码中若启用 `ENABLE_ESP8266`，应视为错误配置。
- 当前有效通信路径只有两种：
  - `BC260 NB-IoT`
  - `CH32V208` 内置以太网
- 以太网与 `BC260` 都不能直接完成企业微信所需的 HTTPS 通信，依赖 HTTP 代理转发。

## 检测与状态逻辑

- 默认浸水阈值为 `1000mV`。
- 默认无水阈值为 `500mV`。
- 状态去抖采用双阈值和计数确认：
  - 浸水确认次数默认 `2`
  - 无水确认次数默认 `5`
- `main.c` 是核心业务文件，包含检测、状态管理、通信发送和大量调试输出逻辑。

## 配置系统

- 配置源文件是项目根目录下的 `config.env`，它不应提交到版本控制。
- `User/config.h` 由 `generate_config.py` 生成，不应手工维护。
- 修改通信开关、Webhook、代理地址或密钥时，正确流程是：
  1. 修改 `config.env`
  2. 运行 `python generate_config.py`
  3. 重新配置/编译工程
- `User/config.h.template` 和 `config.env.example` 体现的是模板和示例，不代表用户当前真实配置。

## 构建与编译记忆

- 工程使用 `CMake` 构建，工具链优先查找 `riscv-wch-elf-gcc`，找不到时回退到 `riscv-none-elf-gcc`。
- 当前构建脚本在配置阶段输出 `Compile target: CH32V208WBU6`，这是稳定的目标提示信息。
- `User/main.c` 里还保留了 `#pragma message("编译目标：CH32V208WBU6")`，但它只会在 `main.c` 实际重编时出现。
- 当 `ENABLE_ETHERNET=0` 时，`CMakeLists.txt` 会将 `User/eth_driver.c` 从编译源列表中移除，避免出现 empty translation unit 警告。

## 网络与安全约束

- 企业微信通过 `WEBHOOK_KEY` 标识目标机器人。
- 项目支持通过代理将 HTTP 转发到企业微信 HTTPS 接口。
- `BC260` 和以太网可使用不同代理：
  - 以太网代理通常是内网地址
  - `BC260` 代理通常是公网地址
- UDP 报文支持密钥校验，生产环境应启用 `UDP_SECRET_KEY`。
- 设备可通过 `IMEI` 作为唯一标识，便于多设备区分来源。

## 重要文件

- `User/main.c`：主业务逻辑
- `User/eth_driver.c`：以太网驱动
- `User/ch32v20x_it.c`：中断处理
- `User/config.h`：生成后的配置头文件
- `generate_config.py`：配置生成脚本
- `config.env.example`：配置示例
- `tools/webhook_proxy.py`：HTTP/UDP 代理脚本

## 协作偏好

- 处理这个项目时，应优先修正根因，不要用关闭告警或手工绕过配置的方式掩盖问题。
- 遇到模块开关相关问题，优先检查 `config.env`、`User/config.h` 和 `CMakeLists.txt` 是否一致。
- 如果文档、模板、代码三者出现冲突，以当前 `main.c`、`generate_config.py`、`CMakeLists.txt` 的实现为准，再回补文档。
