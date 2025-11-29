# 浸水报警器项目

这是一个基于CH32V203芯片的浸水传感器项目，可以检测是否有水并控制LED指示灯。

## 功能特性

- 检测是否有水（通过PA0引脚输入）
- 控制两个LED指示灯：
  - PB0连接的LED：无水时点亮
  - PB1连接的LED：有水时点亮
- 支持串口打印和SDI虚拟串口调试打印

## 硬件连接

- PA0：传感器输入（高电平表示有水，低电平表示无水）
- PB0：LED1控制（共阳极，低电平点亮）- 无水时点亮
- PB1：LED2控制（共阳极，低电平点亮）- 有水时点亮
- PA9：USART1_TX（串口输出）
- PA10：USART1_RX（串口输入）

## 打印方式选择

本项目支持两种调试打印方式：

### 1. USART串口打印（默认）

通过物理串口(UART)进行调试信息输出。

### 2. SDI虚拟串口打印

通过SDI(Serial Debug Interface)进行调试信息输出，需要配合WCH-LinkUtility 1.8及以上版本使用。

## 构建方式

### 构建SDI版本

```bash
npm run build:sdi
```

或者使用CMake命令：
```bash
cmake -DSDI_PRINT=TRUE .
make
```

### 构建USART版本

```bash
npm run build:usart
```

或者使用CMake命令：
```bash
cmake -DSDI_PRINT=FALSE .
make
```

使用默认设置（默认为USART版本）：

```bash
npm run build
```

或者使用CMake命令：
```bash
cmake .
make
```

## 清理构建产物

手动清理构建产物（跨平台兼容）：
```bash
# Unix/Linux/macOS
rm -rf *.elf *.hex *.bin *.lst *.map

# Windows CMD
del *.elf *.hex *.bin *.lst *.map

# Windows PowerShell
Remove-Item *.elf,*.hex,*.bin,*.lst,*.map
```

## 使用说明

1. 连接好硬件电路
2. 根据需要选择合适的构建方式
3. 下载固件到CH32V203芯片
4. 观察LED状态变化和调试输出信息

无论选择哪种构建方式，都将生成名为 `water-immersion-alarm.elf` 的可执行文件，区别在于其内部使用的打印方式不同。

## 许可证

本项目采用 LGPL-3.0-only 许可证，详见 [LICENSE](LICENSE) 文件。