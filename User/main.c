/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : Jac0b_Shi (SHU-SPE-Sandrone)
 * Version            : V1.0.0
 * Date               : 2025/11/30
 * Description        : 浸水检测报警系统 - 主程序
 *                      基于CH32V203 RISC-V微控制器和ESP8266 WiFi模块
 *                      支持实时浸水检测和企业微信报警推送
 * Repository         : https://gitee.com/SHU-SPE-Sandrone/water-immersion-alarm
 * Issues             : https://gitee.com/SHU-SPE-Sandrone/water-immersion-alarm/issues
 *********************************************************************************
 * Copyright (c) 2025 SHU-SPE-Sandrone
 *
 * This project is licensed under the GNU Lesser General Public License v3.0
 * You may obtain a copy of the license at:
 *     https://www.gnu.org/licenses/lgpl-3.0.html
 *
 * 本项目基于WCH官方CH32V203示例代码修改而来
 * Original work Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 *
 * 主要功能：
 * - 通过ADC检测浸水传感器模拟量（阈值可配置）
 * - 双LED指示系统状态（正常/报警）
 * - ESP8266 WiFi模块自动连接网络
 * - 通过企业微信Webhook API推送报警消息（支持HTTPS/SSL）
 * - 基于FreeRTOS的实时任务调度
 * - 完整的状态管理和错误处理
 *******************************************************************************/

/*
 *@Note
 * 硬件连接说明：
 *
 * ADC输入：
 * - PA0：浸水传感器模拟量输入（0-3.3V，阈值1000mV）
 *
 * LED指示灯（共阳极，低电平点亮）：
 * - PB0：正常状态指示灯（绿色）- 无水时点亮
 * - PB1：报警状态指示灯（红色）- 检测到浸水时点亮
 *
 * 调试串口（USART1，115200波特率，8N1）：
 * - PA9：USART1_TX - 系统日志输出
 * - PA10：USART1_RX - 保留（暂未使用）
 *
 * ESP8266通信（USART3，115200波特率，DMA模式）：
 * - PB10：USART3_TX - 发送AT指令到ESP8266
 * - PB11：USART3_RX - 接收ESP8266响应（DMA接收）
 * - PA8：ESP8266_RST - 硬件复位控制（低电平有效）
 *
 * 配置说明：
 * - WiFi和Webhook配置：编辑 config.env 后运行 generate_config.py
 * - 检测阈值配置：修改本文件中的 WATER_THRESHOLD_MV 宏定义
 * - 详细日志开关：修改本文件中的 DEBUG_MODE 宏定义
 */

#include "debug.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"
#include "ch32v20x_usart.h"
#include "ch32v20x_adc.h"
#include "ch32v20x_misc.h"
#include <stdbool.h>
#include "string.h"
#include "stdio.h"
#include "config.h"  // 包含配置文件（WiFi和Webhook配置）

/*********************************************************************
 * 全局变量定义
 *********************************************************************/

/* 浸水检测相关全局变量 */
volatile uint8_t water_status = 0;       // 当前水位状态：0=无水，1=有水
volatile uint8_t last_water_status = 0;  // 上一次的水位状态，用于检测状态变化
volatile uint16_t adc_value = 0;         // ADC原始转换结果（0-4095）
volatile uint16_t voltage_mv = 0;        // 转换后的电压值（单位：毫伏）

/* ESP8266通信相关全局变量 */
#define ESP_RX_BUFFER_SIZE 256           // 定义缓冲区大小为256字节
volatile uint8_t esp_tx_buffer[ESP_RX_BUFFER_SIZE];  // ESP8266发送缓冲区
volatile uint8_t esp_rx_buffer[ESP_RX_BUFFER_SIZE];  // ESP8266接收缓冲区（DMA使用）
volatile uint16_t esp_rx_index = 0;                   // 接收缓冲区当前索引
volatile uint8_t esp_response_received = 0;           // 响应接收完成标志
volatile uint8_t esp_initialized = 0;                 // ESP8266初始化完成标志

/*********************************************************************
 * 系统配置常量定义
 *********************************************************************/

/* 浸水检测阈值配置 */
#define WATER_THRESHOLD_MV    1000       // 判定为浸水的电压阈值（毫伏）
#define NO_WATER_THRESHOLD_MV 500        // 判定为无水的电压阈值（毫伏）
#define WATER_CONFIRM_COUNT 2            // 确认浸水状态需要的连续检测次数
#define NO_WATER_CONFIRM_COUNT 5         // 确认无水状态需要的连续检测次数

/* 调试配置 */
#define DEBUG_MODE 0                     // 调试模式开关：1=详细日志，0=关键信息

/* 状态确认计数器 */
uint8_t water_counter = 0;               // 连续检测到浸水的计数
uint8_t no_water_counter = 0;            // 连续检测到无水的计数

/*********************************************************************
 * 网络通信配置
 *********************************************************************/

/* 注意: WiFi 和 Webhook 配置现已移至 config.h
 * 请通过修改 config.env 并运行 generate_config.py 来更新配置
 *
 * 配置项：
 * - WIFI_SSID: WiFi网络名称
 * - WIFI_PASSWORD: WiFi密码
 * - WEBHOOK_KEY: 企业微信机器人Key
 * - WEBHOOK_URL: 企业微信Webhook完整URL（自动生成）
 */

/*********************************************************************
 * 硬件引脚定义
 *********************************************************************/

/* ESP8266硬件复位引脚 */
#define ESP8266_RST_PORT GPIOA           // 复位引脚端口
#define ESP8266_RST_PIN  GPIO_Pin_1      // 复位引脚编号

/*********************************************************************
 * 函数声明
 *********************************************************************/
void ESP8266_RST_Control(uint8_t state);
void ESP8266_HardReset(void);
uint8_t ESP8266_SendCommand(char* cmd, uint32_t timeout);
uint8_t ESP8266_Init(void);
uint8_t ESP8266_SendWebhookAlert(char* alert_msg);

/*********************************************************************
 * @fn      ADC_Function_Init
 *
 * @brief   初始化ADC功能
 *
 * @return  none
 */
void ADC_Function_Init(void)
{
    ADC_InitTypeDef ADC_InitStructure = {0};
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    // 使能GPIOA和ADC1时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div8);  // 设置ADC时钟

    // 配置PA0为模拟输入
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 复位ADC1
    ADC_DeInit(ADC1);
    
    // ADC1配置
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &ADC_InitStructure);

    // 启用ADC1
    ADC_Cmd(ADC1, ENABLE);

    // ADC校准
    ADC_ResetCalibration(ADC1);
    while(ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while(ADC_GetCalibrationStatus(ADC1));
}

/*********************************************************************
 * @fn      GPIO_Init_For_Sensor
 *
 * @brief   初始化传感器相关的GPIO引脚
 *          PA0 - 传感器输入（模拟输入，通过ADC读取）
 *          PA1 - ESP8266 RST控制引脚（推挽输出）
 *          PB0 - LED1控制（推挽输出）
 *          PB1 - LED2控制（推挽输出）
 *
 * @return  none
 */
void GPIO_Init_For_Sensor(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    // 配置PA1为推挽输出（ESP8266 RST控制）
    GPIO_InitStructure.GPIO_Pin = ESP8266_RST_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ESP8266_RST_PORT, &GPIO_InitStructure);
    
    // 配置PB0和PB1为推挽输出（LED控制）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // 初始化LED状态 - 默认无水状态（LED1亮，LED2灭）
    GPIO_SetBits(GPIOB, GPIO_Pin_1);  // 熄灭LED2（PB1）
    GPIO_ResetBits(GPIOB, GPIO_Pin_0); // 点亮LED1（PB0）
    
    // 初始化ESP8266 RST引脚为高电平（非复位状态）
    GPIO_SetBits(ESP8266_RST_PORT, ESP8266_RST_PIN);
}

/*********************************************************************
 * @fn      ESP8266_RST_Control
 *
 * @brief   控制ESP8266的RST引脚
 *
 * @param   state - 0表示复位(Low)，1表示正常运行(High)
 *
 * @return  none
 */
void ESP8266_RST_Control(uint8_t state)
{
    if(state)
    {
        GPIO_SetBits(ESP8266_RST_PORT, ESP8266_RST_PIN);  // 拉高RST引脚
    }
    else
    {
        GPIO_ResetBits(ESP8266_RST_PORT, ESP8266_RST_PIN);  // 拉低RST引脚
    }
}

/*********************************************************************
 * @fn      ESP8266_HardReset
 *
 * @brief   对ESP8266执行硬复位
 *
 * @return  none
 */
void ESP8266_HardReset(void)
{
    printf("Performing ESP8266 hard reset...\r\n");
    ESP8266_RST_Control(0);  // 拉低RST引脚
    Delay_Ms(100);           // 保持低电平一段时间
    ESP8266_RST_Control(1);  // 拉高RST引脚
    Delay_Ms(2000);          // 等待模块重新启动
    printf("ESP8266 hard reset completed.\r\n");
}

/*********************************************************************
 * @fn      USART1_Init
 *
 * @brief   初始化USART1，用于串口输出
 *
 * @return  none
 */
void USART1_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};

    // 配置PA9为USART1_TX（复用推挽输出）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 配置PA10为USART1_RX（浮空输入）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // USART1配置
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;

    USART_Init(USART1, &USART_InitStructure);
    USART_Cmd(USART1, ENABLE);
}


/*********************************************************************
 * @fn      USART3_Init
 *
 * @brief   初始化USART3，用于与ESP8266通信（使用DMA）
 *
 * @return  none
 */
void USART3_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};
    DMA_InitTypeDef DMA_InitStructure = {0};

    // 配置PB10为USART3_TX（复用推挽输出）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // 配置PB11为USART3_RX（浮空输入）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // USART3配置 - 8数据位，1停止位，无校验位（8N1）
    USART_InitStructure.USART_BaudRate = 115200;  // ESP8266默认波特率
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;

    USART_Init(USART3, &USART_InitStructure);
    
    // 配置DMA1 Channel3用于USART3_RX
    // CH32V203的USART3_RX对应DMA1_Channel3
    DMA_DeInit(DMA1_Channel3);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART3->DATAR;  // 外设地址
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)esp_rx_buffer;        // 内存地址
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;                     // 从外设到内存
    DMA_InitStructure.DMA_BufferSize = sizeof(esp_rx_buffer);              // 缓冲区大小
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;       // 外设地址不递增
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;                // 内存地址递增
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;// 8位
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;        // 8位
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;                          // 普通模式（不循环）
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;                    // 高优先级
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;                           // 非内存到内存
    DMA_Init(DMA1_Channel3, &DMA_InitStructure);

    // 使能USART3的DMA接收
    USART_DMACmd(USART3, USART_DMAReq_Rx, ENABLE);

    // 使能DMA1 Channel3
    DMA_Cmd(DMA1_Channel3, ENABLE);

    // 使能USART3
    USART_Cmd(USART3, ENABLE);

#if DEBUG_MODE
    printf("[DEBUG] USART3 enabled (DMA MODE)\r\n");
    printf("[DEBUG] USART3 CTLR1=0x%04X\r\n", (unsigned int)USART3->CTLR1);
    printf("[DEBUG] DMA1_Channel3 configured for RX\r\n");
#endif
}

/*********************************************************************
 * @fn      ADC_Read_Voltage
 *
 * @brief   读取PA0引脚的ADC值并转换为电压值(mV)
 *
 * @return  电压值(单位:mV)
 */
uint16_t ADC_Read_Voltage(void)
{
    uint16_t adc_val;
    uint32_t voltage;
    
    // 配置ADC通道0(PA0)
    ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_239Cycles5);
    
    // 启动ADC转换
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    
    // 等待转换完成
    while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));
    
    // 读取ADC值
    adc_val = ADC_GetConversionValue(ADC1);
    
    // 清除标志位
    ADC_ClearFlag(ADC1, ADC_FLAG_EOC);
    
    // 转换为电压值(单位:mV)，假设VDD为3.3V
    // ADC值范围: 0-4095 对应 0-3300mV
    voltage = (uint32_t)adc_val * 3300 / 4095;
    
    return (uint16_t)voltage;
}

/*********************************************************************
 * @fn      Sensor_Status_Check
 *
 * @brief   检查传感器状态（通过ADC读取电压值）
 *
 * @return  none
 */
void Sensor_Status_Check(void)
{
    // 读取ADC值和电压值
    adc_value = ADC_GetConversionValue(ADC1);
    voltage_mv = ADC_Read_Voltage();
    
    // 根据电压阈值判断是否有水，使用去抖动机制
    if(voltage_mv >= WATER_THRESHOLD_MV)
    {
        // 增加有水计数器
        water_counter++;
        no_water_counter = 0; // 重置无水计数器
        
        // 只有当连续多次检测到高电压时才确认为有水状态
        if(water_counter >= WATER_CONFIRM_COUNT)
        {
            water_status = 1;  // 确认为有水
            water_counter = 0;  // 重置计数器
        }
    }
    else if(voltage_mv < NO_WATER_THRESHOLD_MV)
    {
        // 增加无水计数器
        no_water_counter++;
        water_counter = 0; // 重置有水计数器
        
        // 只有当连续多次检测到低电压时才确认为无水状态
        if(no_water_counter >= NO_WATER_CONFIRM_COUNT)
        {
            water_status = 0;  // 确认为无水
            no_water_counter = 0; // 重置计数器
        }
    }
    else
    {
        // 电压在中间区域时保持当前状态不变，重置两个计数器
        water_counter = 0;
        no_water_counter = 0;
    }
}

/*********************************************************************
 * @fn      LED_Control
 *
 * @brief   根据传感器状态控制LED显示
 *          无水时：点亮PB0的LED，熄灭PB1的LED
 *          有水时：点亮PB1的LED，熄灭PB0的LED
 *
 * @return  none
 */
void LED_Control(void)
{
    if(water_status == 0)
    {
        // 无水状态
        GPIO_ResetBits(GPIOB, GPIO_Pin_0);  // 点亮LED1（PB0）
        GPIO_SetBits(GPIOB, GPIO_Pin_1);    // 熄灭LED2（PB1）
    }
    else
    {
        // 有水状态
        GPIO_SetBits(GPIOB, GPIO_Pin_0);    // 熄灭LED1（PB0）
        GPIO_ResetBits(GPIOB, GPIO_Pin_1);  // 点亮LED2（PB1）
    }
}

/*********************************************************************
 * @fn      ESP8266_HardwareTest
 *
 * @brief   测试ESP8266硬件连接 - 检查是否有数据回传
 *
 * @return  1-检测到设备 0-未检测到设备
 */
uint8_t ESP8266_HardwareTest(void)
{
    printf("Performing ESP8266 hardware test...\r\n");
    
#if DEBUG_MODE
    printf("[DEBUG] About to send AT command\r\n");
#endif
    
    // 最多重试3次
    for(int retry = 0; retry < 3; retry++)
    {
        // 清空接收缓冲区
        memset((void*)esp_rx_buffer, 0, sizeof(esp_rx_buffer));
        esp_rx_index = 0;
        esp_response_received = 0;

#if DEBUG_MODE
        // 检查USART3中断状态
        printf("[DEBUG] USART3 SR=0x%04X\r\n", (unsigned int)USART3->STATR);
        printf("[DEBUG] USART3 CTLR1=0x%04X\r\n", (unsigned int)USART3->CTLR1);
        printf("[DEBUG] Before sending - esp_rx_index = %d\r\n", esp_rx_index);
#endif
        
        // 发送AT命令，应该会收到响应
        printf("Sending AT command to check for response...\r\n");
        const char* cmd = "AT\r\n";
        while(*cmd)
        {
            while(USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET);
            USART_SendData(USART3, *cmd);
#if DEBUG_MODE
            USART_SendData(USART1, *cmd);  // 调试输出：发送相同字符到USART1
            if (*cmd == '\r') {
                printf("[DEBUG] Sent \\r to USART3\r\n");
            } else if (*cmd == '\n') {
                printf("[DEBUG] Sent \\n to USART3\r\n");
            } else {
                printf("[DEBUG] Sent '%c' to USART3\r\n", *cmd);
            }
#endif
            cmd++;
        }
        
#if DEBUG_MODE
        printf("[DEBUG] AT command sent, waiting for response\r\n");
        printf("[DEBUG] USART3 SR after sending=0x%04X\r\n", (unsigned int)USART3->STATR);
        printf("[DEBUG] USART3 CTLR1 after sending=0x%04X\r\n", (unsigned int)USART3->CTLR1);
#endif
        
        // 等待3秒，使用DMA接收数据
        uint32_t start_time = 0;
        uint32_t last_dma_count = 0;

        while(!esp_response_received && start_time < 3000)
        {
            // 读取DMA传输剩余字节数
            uint32_t dma_remaining = DMA_GetCurrDataCounter(DMA1_Channel3);
            uint32_t bytes_received = sizeof(esp_rx_buffer) - dma_remaining;

            // 更新接收索引
            esp_rx_index = bytes_received;

            // 检查是否有新数据
            if(bytes_received > last_dma_count)
            {
                last_dma_count = bytes_received;

                // 检查是否收到完整的响应 (以 \r\n 结尾)
                if(esp_rx_index >= 2 &&
                   esp_rx_buffer[esp_rx_index-2] == '\r' &&
                   esp_rx_buffer[esp_rx_index-1] == '\n')
                {
                    esp_response_received = 1;
                    esp_rx_buffer[esp_rx_index] = '\0';
                    break;
                }
            }

            Delay_Ms(10);
            start_time += 10;
        }
        
#if DEBUG_MODE
        printf("[DEBUG] Wait completed, esp_rx_index = %d\r\n", esp_rx_index);
        printf("[DEBUG] USART3 SR after waiting=0x%04X\r\n", (unsigned int)USART3->STATR);

        // 显示接收到的原始数据
        if (esp_rx_index > 0) {
            printf("[DEBUG] Received data (%d bytes): ", esp_rx_index);
            for (int i = 0; i < esp_rx_index; i++) {
                if (esp_rx_buffer[i] >= 32 && esp_rx_buffer[i] <= 126) {
                    printf("%c", esp_rx_buffer[i]);
                } else if (esp_rx_buffer[i] == '\r') {
                    printf("<CR>");
                } else if (esp_rx_buffer[i] == '\n') {
                    printf("<LF>");
                } else {
                    printf("[%02X]", esp_rx_buffer[i]);
                }
            }
            printf("\r\n");
        }
#endif
        
        // 在非调试模式下只显示简化的接收信息
#if !DEBUG_MODE
        // 检查是否收到任何数据
        if(esp_rx_index > 0)
        {
            // 只在接收到大量数据或特殊响应时显示
            if(esp_rx_index > 50 || strstr((char*)esp_rx_buffer, "ERROR") ||
               strstr((char*)esp_rx_buffer, "busy"))
            {
                printf("Received %d bytes from ESP8266\r\n", esp_rx_index);
            }
        }
#else
        // 调试模式下显示完整数据
        if(esp_rx_index > 0)
        {
            printf("Received %d bytes of data from ESP8266:\r\n", esp_rx_index);
            for(int i = 0; i < esp_rx_index; i++)
            {
                if(esp_rx_buffer[i] >= 32 && esp_rx_buffer[i] <= 126)
                {
                    printf("%c", esp_rx_buffer[i]);  // 可打印字符
                }
                else
                {
                    printf("[%02X]", esp_rx_buffer[i]);  // 十六进制显示不可打印字符
                }
            }
            printf("\r\n");
        }
#endif

        // 检查响应（无论调试模式）
        if(esp_rx_index > 0)
        {
            // 检查是否收到ERROR响应
            if(strstr((char*)esp_rx_buffer, "ERROR"))
            {
                // 对于 CIPCLOSE 命令，ERROR 通常意味着连接已经关闭
                if(strstr(cmd, "CIPCLOSE"))
                {
                    return 1;  // 连接已关闭，视为成功
                }

                printf("Received ERROR response, waiting before retry...\r\n");
                Delay_Ms(1000); // 等待1秒后重试
                continue; // 继续下一次循环重试
            }
            
            return 1;
        }
        else
        {
            printf("No response from ESP8266.\r\n");
            
#if DEBUG_MODE
            // 添加更多诊断信息
            printf("[DIAGNOSTIC INFO]\r\n");
            printf("CH32 TX Pin (PB10) -> ESP8266 RX Pin\r\n");
            printf("CH32 RX Pin (PB11) -> ESP8266 TX Pin\r\n");
            printf("Check if you have connected:\r\n");
            printf("- VCC to 3.3V (NOT 5V!)\r\n");
            printf("- GND to GND\r\n");
            printf("- TX to RX and RX to TX\r\n");
            printf("- Ensure sufficient power supply for ESP8266\r\n");
#endif
        }
    }
    
    return 0;
}

/*********************************************************************
 * @fn      ESP8266_TestConnection
 *
 * @brief   测试ESP8266连接和波特率
 *
 * @return  1-成功 0-失败
 */
uint8_t ESP8266_TestConnection(void)
{
    printf("Testing ESP8266 connection...\r\n");
    
    // 首先进行硬件测试
    if(!ESP8266_HardwareTest())
    {
        printf("ESP8266 hardware test failed!\r\n");
        
        // 直接使用115200波特率重试5次，每次间隔1秒
        for(int retry = 0; retry < 5; retry++) 
        {
            printf("Retry %d/5: Sending AT command at baudrate 115200...\r\n", retry+1);
            
            // 清空接收缓冲区
            memset((void*)esp_rx_buffer, 0, sizeof(esp_rx_buffer));
            esp_rx_index = 0;
            esp_response_received = 0;

            // 发送测试命令"AT\r\n"
            const char* cmd = "AT\r\n";
            while(*cmd)
            {
                // 等待发送完成
                uint32_t timeout = 1000000; // 设置超时，避免死循环
                while(USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET && --timeout);
                if(timeout == 0) {
                    printf("[ERROR] USART3 TX timeout\r\n");
                    return 0;
                }
                
                USART_SendData(USART3, *cmd);
#if DEBUG_MODE
                USART_SendData(USART1, *cmd);  // 调试输出：发送相同字符到USART1
                if (*cmd == '\r') {
                    printf("[DEBUG] Sent \\r to USART3\r\n");
                } else if (*cmd == '\n') {
                    printf("[DEBUG] Sent \\n to USART3\r\n");
                } else {
                    printf("[DEBUG] Sent '%c' to USART3\r\n", *cmd);
                }
#endif
                cmd++;
            }
            
            // 等待3秒，使用轮询接收数据
            uint32_t start_time = 0;
            while(!esp_response_received && start_time < 3000)
            {
                // 在每次延时周期中多次检查RXNE
                for(int check = 0; check < 10; check++)
                {
                    if(USART_GetFlagStatus(USART3, USART_FLAG_RXNE) != RESET)
                    {
                        uint8_t data = USART_ReceiveData(USART3);

                        if(esp_rx_index < sizeof(esp_rx_buffer) - 1)
                        {
                            esp_rx_buffer[esp_rx_index++] = data;

                            if(esp_rx_index >= 2 &&
                               esp_rx_buffer[esp_rx_index-2] == '\r' &&
                               esp_rx_buffer[esp_rx_index-1] == '\n')
                            {
                                esp_response_received = 1;
                                esp_rx_buffer[esp_rx_index] = '\0';
                                break;
                            }
                        }
                        else
                        {
                            esp_rx_index = 0;
                        }
                    }

                    for(volatile int i = 0; i < 1000; i++);
                }

                if(esp_response_received) break;

                Delay_Ms(1);
                start_time += 1;
            }
            
            printf("[DEBUG] Bytes received at retry %d: %d\r\n", retry+1, esp_rx_index);


            if(esp_rx_index > 0)
            {
                printf("Received response at retry %d:\r\n", retry+1);
                for(int j = 0; j < esp_rx_index; j++)
                {
                    if(esp_rx_buffer[j] >= 32 && esp_rx_buffer[j] <= 126)
                    {
                        printf("%c", esp_rx_buffer[j]);
                    }
                    else if(esp_rx_buffer[j] == '\r')
                    {
                        printf("\\r");
                    }
                    else if(esp_rx_buffer[j] == '\n')
                    {
                        printf("\\n\n");
                    }
                    else
                    {
                        printf("[%02X]", esp_rx_buffer[j]);
                    }
                }
                printf("\r\n");
                
                // 检查是否收到ERROR响应
                if(strstr((char*)esp_rx_buffer, "ERROR"))
                {
                    // 对于 CIPCLOSE 命令，ERROR 通常意味着连接已经关闭
                    if(strstr(cmd, "CIPCLOSE"))
                    {
                        return 1;  // 连接已关闭，视为成功
                    }

                    printf("Received ERROR response, waiting before retry...\r\n");
                    Delay_Ms(1000); // 等待1秒后重试
                    continue; // 继续下一次循环重试
                }
                
                return 1;
            }
            else
            {
                printf("No response at retry %d\r\n", retry+1);
            }
        }
        return 0;
    }
    
    // 发送测试命令
    if(ESP8266_SendCommand("AT", 2000))
    {
        printf("ESP8266 responded at baudrate: 115200\r\n");
        return 1;
    }
    
    printf("ESP8266 did not respond at baudrate 115200\r\n");
    printf("You may need to flash AT firmware to your ESP8266.\r\n");
    return 0;
}

/*********************************************************************
 * @fn      ESP8266_SendCommand
 *
 * @brief   发送AT指令给ESP8266
 *
 * @param   cmd - 要发送的命令字符串
 * @param   timeout - 超时时间(毫秒)
 *
 * @return  1-成功 0-失败
 */
uint8_t ESP8266_SendCommand(char* cmd, uint32_t timeout)
{
    // 最多重试3次
    for(int retry = 0; retry < 3; retry++)
    {
        // 清空接收缓冲区并重新初始化DMA
        memset((void*)esp_rx_buffer, 0, sizeof(esp_rx_buffer));
        esp_rx_index = 0;
        esp_response_received = 0;
        
        // 重新配置DMA以接收新数据
        DMA_Cmd(DMA1_Channel3, DISABLE);
        DMA_SetCurrDataCounter(DMA1_Channel3, sizeof(esp_rx_buffer));
        DMA_Cmd(DMA1_Channel3, ENABLE);

        // 发送命令（使用临时指针，避免修改原指针）
        printf("Sending command: %s\r\n", cmd);
        const char* p = cmd;  // 使用临时指针
        while(*p)
        {
            // 等待发送完成
            uint32_t send_timeout = 1000000; // 设置超时，避免死循环
            while(USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET && --send_timeout);
            if(send_timeout == 0) {
                printf("[ERROR] USART3 TX timeout\r\n");
                return 0;
            }
            
            USART_SendData(USART3, *p);
#if DEBUG_MODE
            USART_SendData(USART1, *p);  // 调试输出：发送相同字符到USART1
            if (*p == '\r') {
                printf("[DEBUG] Sent \\r to USART3\r\n");
            } else if (*p == '\n') {
                printf("[DEBUG] Sent \\n to USART3\r\n");
            } else {
                printf("[DEBUG] Sent '%c' to USART3\r\n", *p);
            }
#endif
            p++;
        }
        
        // 如果命令不是以\r\n结尾，自动添加
        if(strlen(cmd) < 2 || cmd[strlen(cmd)-2] != '\r' || cmd[strlen(cmd)-1] != '\n')
        {
            // 发送 \r\n
            while(USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET);
            USART_SendData(USART3, '\r');
#if DEBUG_MODE
            USART_SendData(USART1, '\r');
            printf("[DEBUG] Sent \\r to USART3\r\n");
#endif

            while(USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET);
            USART_SendData(USART3, '\n');
#if DEBUG_MODE
            USART_SendData(USART1, '\n');
            printf("[DEBUG] Sent \\n to USART3\r\n");
#endif
        }

#if DEBUG_MODE
        printf("[DEBUG] Command sending completed, waiting for response...\r\n");
#endif
        
        // 等待响应 - 使用DMA模式
        uint32_t start_time = 0;
        uint32_t last_dma_count = 0;

        while(!esp_response_received && start_time < timeout)
        {
            // 读取DMA传输剩余字节数
            uint32_t dma_remaining = DMA_GetCurrDataCounter(DMA1_Channel3);
            uint32_t bytes_received = sizeof(esp_rx_buffer) - dma_remaining;

            // 更新接收索引
            esp_rx_index = bytes_received;

            // 检查是否有新数据
            if(bytes_received > last_dma_count)
            {
                last_dma_count = bytes_received;

                // 添加字符串终止符以便检查
                if(esp_rx_index < sizeof(esp_rx_buffer))
                {
                    esp_rx_buffer[esp_rx_index] = '\0';
                }

                // 检查关键成功标志（WiFi连接场景）
                if(strstr((char*)esp_rx_buffer, "WIFI GOT IP") ||
                   (strstr((char*)esp_rx_buffer, "WIFI CONNECTED") && strstr((char*)esp_rx_buffer, "OK")))
                {
                    esp_response_received = 1;
                    break;
                }

                // 检查 AT+CIPSEND 的特殊响应（以 '>' 结尾）
                if(esp_rx_index >= 1 && esp_rx_buffer[esp_rx_index-1] == '>')
                {
                    esp_response_received = 1;
                    break;
                }

                // 检查是否收到完整的响应（以 \r\n 结尾）
                if(esp_rx_index >= 2 &&
                   esp_rx_buffer[esp_rx_index-2] == '\r' &&
                   esp_rx_buffer[esp_rx_index-1] == '\n')
                {
                    esp_response_received = 1;
                    break;
                }
            }

            Delay_Ms(10);
            start_time += 10;
        }

#if DEBUG_MODE
        printf("[DEBUG] After waiting %lu ms, esp_rx_index: %d, esp_response_received: %d\r\n",
               (unsigned long)start_time, esp_rx_index, esp_response_received);

        // 显示接收到的数据
        if (esp_rx_index > 0) {
            printf("[DEBUG] Received data: ");
            for (int i = 0; i < esp_rx_index; i++) {
                if (esp_rx_buffer[i] >= 32 && esp_rx_buffer[i] <= 126) {
                    printf("%c", esp_rx_buffer[i]);
                } else if (esp_rx_buffer[i] == '\r') {
                    printf("<CR>");
                } else if (esp_rx_buffer[i] == '\n') {
                    printf("<LF>");
                } else {
                    printf("[%02X]", esp_rx_buffer[i]);
                }
            }
            printf("\r\n");
        }
#endif

        if(esp_response_received)
        {
            printf("Received response: %s\r\n", (char*)esp_rx_buffer);
            // 检查常见的成功响应
            if(strstr((char*)esp_rx_buffer, "OK") || 
               strstr((char*)esp_rx_buffer, "no change") ||
               strstr((char*)esp_rx_buffer, "ready") ||
               strstr((char*)esp_rx_buffer, "CONNECT") ||  // ✅ 添加 CONNECT 判断（包括 "0,CONNECT", "WIFI CONNECTED" 等）
               strstr((char*)esp_rx_buffer, "WIFI GOT IP") ||
               (esp_rx_index >= 1 && esp_rx_buffer[esp_rx_index-1] == '>'))  // AT+CIPSEND 的 '>' 提示符
            {
                return 1;
            }
            // 如果收到 busy，等待更长时间后重试
            else if(strstr((char*)esp_rx_buffer, "busy"))
            {
                printf("Received busy response");

                // 对于 CIPSTART 命令，busy 通常意味着连接正在建立，应该等待结果
                if(strstr(cmd, "CIPSTART"))
                {
                    printf(", connection is establishing, waiting for result...\r\n");
                    Delay_Ms(3000); // 等待3秒让连接建立

                    // 再次检查是否有后续响应（CONNECT 或 ERROR）
                    esp_response_received = 0;
                    esp_rx_index = 0;

                    uint32_t wait_start = 0;
                    while(wait_start < 5000)  // 最多等待5秒
                    {
                        // 主动检查 DMA 接收的数据量
                        uint16_t dma_remaining = DMA_GetCurrDataCounter(DMA1_Channel3);
                        uint16_t dma_received = sizeof(esp_rx_buffer) - dma_remaining;

                        if(dma_received > esp_rx_index)
                        {
                            // 有新数据到达
                            esp_rx_index = dma_received;

                            // 添加字符串结束符
                            if(esp_rx_index < sizeof(esp_rx_buffer))
                            {
                                esp_rx_buffer[esp_rx_index] = '\0';
                            }

                            // 检查是否收到完整响应
                            if(esp_rx_index >= 2 &&
                               esp_rx_buffer[esp_rx_index-2] == '\r' &&
                               esp_rx_buffer[esp_rx_index-1] == '\n')
                            {
                                esp_response_received = 1;
                            }
                        }

                        if(esp_response_received)
                        {
                            printf("Received delayed response: %s\r\n", (char*)esp_rx_buffer);
                            if(strstr((char*)esp_rx_buffer, "CONNECT") || strstr((char*)esp_rx_buffer, "OK"))
                            {
                                return 1;  // 连接成功
                            }
                            else if(strstr((char*)esp_rx_buffer, "ERROR"))
                            {
                                printf("Connection failed after busy.\r\n");
                                Delay_Ms(2000);
                                continue;  // 重试
                            }
                        }
                        Delay_Ms(100);
                        wait_start += 100;
                    }

                    // 超时，可能连接还在建立，继续重试
                    printf("No response after busy, retrying...\r\n");
                }
                else
                {
                    printf(", waiting 5 seconds before retry...\r\n");
                    Delay_Ms(5000); // 等待5秒，让ESP8266完成当前操作
                }

                continue;
            }
            // 如果收到ERROR但不是DISCONNECT，才认为失败
            else if(strstr((char*)esp_rx_buffer, "ERROR") &&
                    !strstr((char*)esp_rx_buffer, "DISCONNECT"))
            {
                // 对于 CIPCLOSE 命令，ERROR 通常意味着连接已经关闭，这是正常的
                if(strstr(cmd, "CIPCLOSE"))
                {
                    printf("[DEBUG] Connection already closed (ERROR on CIPCLOSE is expected)\r\n");
                    return 1;  // 连接已关闭，视为成功
                }

                printf("Received ERROR response, waiting before retry...\r\n");
                Delay_Ms(2000);
                continue;
            }
            // 如果收到 DISCONNECT 但没有后续内容，等待更长时间看是否会连接
            else if(strstr((char*)esp_rx_buffer, "DISCONNECT"))
            {
                printf("WiFi disconnected, waiting for reconnection...\r\n");
                Delay_Ms(3000); // 等待3秒让ESP8266重新连接
                // 不返回失败，继续等待
            }
        }
        else
        {
            printf("Timeout waiting for response\r\n");
        }
    }
    
    return 0;
}

/*********************************************************************
 * @fn      ESP8266_Init
 *
 * @brief   初始化ESP8266 WiFi模块
 *
 * @return  1-成功 0-失败
 */
uint8_t ESP8266_Init(void)
{
    char cmd[128];
    
    printf("Initializing ESP8266...\r\n");
    
    // 上电后先对ESP8266进行硬件复位
    printf("Performing hardware reset on ESP8266...\r\n");
    ESP8266_HardReset();
    
    // 首先测试连接
    if(!ESP8266_TestConnection())
    {
        printf("ESP8266 connection test failed!\r\n");
        return 0;
    }
    
    Delay_Ms(2000);  // 等待 ESP8266 完全启动

    // 设置WiFi模式为Station模式
    printf("Setting WiFi mode...\r\n");
    if(!ESP8266_SendCommand("AT+CWMODE=1", 3000))
    {
        printf("Failed to set WiFi mode!\r\n");
        return 0;
    }
    
    Delay_Ms(2000);  // 增加延时，避免 "busy p..."

    // 连接WiFi网络
    printf("Connecting to WiFi...\r\n");
    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"", WIFI_SSID, WIFI_PASSWORD);

    // 先检查是否已经连接
    if(ESP8266_SendCommand("AT+CWJAP?", 3000))
    {
        if(strstr((char*)esp_rx_buffer, WIFI_SSID))
        {
            printf("WiFi is already connected!\r\n");
            goto wifi_connected;
        }
    }

    // 尝试连接，给予充足的时间
    if(!ESP8266_SendCommand(cmd, 20000))
    {
        printf("WiFi connection command timeout, checking status...\r\n");
        Delay_Ms(2000);

        // 再次查询连接状态
        if(ESP8266_SendCommand("AT+CWJAP?", 3000))
        {
            if(strstr((char*)esp_rx_buffer, WIFI_SSID))
            {
                printf("WiFi connected after delay!\r\n");
                goto wifi_connected;
            }
        }

        printf("Failed to connect to WiFi!\r\n");
        return 0;
    }

wifi_connected:
    Delay_Ms(2000);  // 等待 WiFi 连接稳定

    // 获取IP地址
    printf("Getting IP address...\r\n");
    if(!ESP8266_SendCommand("AT+CIFSR", 3000))
    {
        printf("Failed to get IP address!\r\n");
    }
    
    Delay_Ms(2000);

    // 启用多连接
    printf("Enabling multiple connections...\r\n");
    if(!ESP8266_SendCommand("AT+CIPMUX=1", 3000))
    {
        printf("Failed to enable multiple connections!\r\n");
        return 0;
    }
    
    Delay_Ms(2000);

    printf("ESP8266 initialized successfully!\r\n");
    esp_initialized = 1;  // 标记ESP8266已初始化
    return 1;
}

/*********************************************************************
 * @fn      ESP8266_SendWebhookAlert
 *
 * @brief   通过ESP8266发送Webhook警报
 *
 * @param   alert_msg - 警报消息内容
 *
 * @return  1-成功 0-失败
 */
uint8_t ESP8266_SendWebhookAlert(char* alert_msg)
{
    char cmd[128];
    char json_body[256];
    char http_request[512];
    uint16_t json_len;
    uint16_t http_len;

    // 只有ESP8266初始化成功才发送webhook
    if (!esp_initialized) {
        printf("ESP8266 not initialized, skipping webhook alert.\r\n");
        return 0;
    }
    
    printf("Sending webhook alert: %s\r\n", alert_msg);
    
    // 建立 SSL/TLS 连接（HTTPS，端口 443）
    sprintf(cmd, "AT+CIPSTART=0,\"SSL\",\"%s\",%d", "qyapi.weixin.qq.com", 443);

    // SSL 连接需要更长时间，重试机制
    uint8_t ssl_retry = 0;
    uint8_t ssl_connected = 0;

    for(ssl_retry = 0; ssl_retry < 3; ssl_retry++)
    {
        if(ESP8266_SendCommand(cmd, 10000))  // SSL 握手需要更长时间
        {
            ssl_connected = 1;
            printf("SSL connection established!\r\n");
            break;
        }

        if(ssl_retry < 2)
        {
            printf("SSL connection failed, retrying (%d/3)...\r\n", ssl_retry + 2);
            Delay_Ms(2000);
        }
    }

    if(!ssl_connected)
    {
        printf("Failed to establish SSL connection after 3 attempts!\r\n");
        return 0;
    }
    
    Delay_Ms(500);

    // 准备 JSON body
    sprintf(json_body,
        "{\"msgtype\":\"text\",\"text\":{\"content\":\"%s\"}}",
        alert_msg);
    
    json_len = strlen(json_body);

    // 准备完整的 HTTP POST 请求
    sprintf(http_request,
        "POST /cgi-bin/webhook/send?key=%s HTTP/1.1\r\n"
        "Host: qyapi.weixin.qq.com\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        WEBHOOK_KEY,
        json_len,
        json_body);

    http_len = strlen(http_request);
    
    printf("HTTP request length: %d bytes\r\n", http_len);

    // 发送HTTP请求
    sprintf(cmd, "AT+CIPSEND=0,%d", http_len);
    if(!ESP8266_SendCommand(cmd, 1000))
    {
        printf("Failed to prepare send!\r\n");
        ESP8266_SendCommand("AT+CIPCLOSE=0", 1000);
        return 0;
    }
    
    Delay_Ms(500);

    // 发送实际的HTTP数据
    printf("Sending HTTP data...\r\n");
    for(int i = 0; i < http_len; i++)
    {
        while(USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET);
        USART_SendData(USART3, http_request[i]);
    }
    
    // 等待服务器响应和关闭连接（企业微信会返回200 OK并主动关闭）
    // 给予足够时间让服务器完成响应和关闭操作
    Delay_Ms(2000);

    // 尝试关闭连接（保险操作）
    // 注意：如果服务器已发送0,CLOSED信号，此命令会返回ERROR，这是正常的
    ESP8266_SendCommand("AT+CIPCLOSE=0", 1000);

    printf("Webhook alert sent!\r\n");
    return 1;
}

/*********************************************************************
 * @fn      USART_Report_Status
 *
 * @brief   通过串口报告当前水位状态和电压值
 *
 * @return  none
 */
void USART_Report_Status(void)
{
    static uint8_t webhook_sent = 0;  // 记录是否已发送过webhook
    
    if(water_status != last_water_status)
    {
        // 状态发生变化才输出
        if(water_status == 0)
        {
            printf("Water Status: No Water Detected, Voltage: %dmV\r\n", voltage_mv);
            
            // 如果之前是有水状态，现在变为无水状态，则发送恢复通知
            if(last_water_status == 1 && webhook_sent)
            {
                char alert_msg[100];
                sprintf(alert_msg, "【解除警报】浸水情况已解除，当前电压：%dmV", voltage_mv);
                if(ESP8266_SendWebhookAlert(alert_msg)) {
                    webhook_sent = 0;  // 重置标记
                }
            }
        }
        else
        {
            printf("Water Status: Water Detected, Voltage: %dmV\r\n", voltage_mv);
            
            // 发送警报通知
            char alert_msg[100];
            sprintf(alert_msg, "【严重警报】检测到浸水情况！当前电压：%dmV", voltage_mv);
            if(ESP8266_SendWebhookAlert(alert_msg)) {
                webhook_sent = 1;  // 标记已发送
            }
        }
        last_water_status = water_status;
    }
}

/*********************************************************************
 * @fn      Delay_Custom
 *
 * @brief   自定义延时函数，防止状态抖动
 *
 * @param   nCount - 延时毫秒数
 *
 * @return  none
 */
void Delay_Custom(uint32_t nCount)
{
    Delay_Ms(nCount);
}

/*********************************************************************
 * @fn      main
 *
 * @brief   主程序
 *
 * @return  none
 */
int main(void)
{
    // 配置NVIC优先级分组并初始化系统时钟
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
    
    // 初始化串口打印功能（根据配置选择SDI或USART）
#if (SDI_PRINT == SDI_PR_TRUE)
    SDI_Printf_Enable();  // 启用SDI打印
    printf("System Clock: %ld Hz\r\n", SystemCoreClock);
    printf("SDI Print Enabled\r\n");
#else
    USART_Printf_Init(115200);  // 初始化串口并支持printf输出
    printf("System Clock: %ld Hz\r\n", SystemCoreClock);
    printf("USART Print Enabled\r\n");
#endif

    // 统一使能所有外设时钟（包括DMA）
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_USART1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);  // 只需要 USART3
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);  // 使能DMA1时钟

#if DEBUG_MODE
    printf("[DEBUG] Peripheral clocks enabled (including DMA1)\r\n");
#endif
    
    // 初始化GPIO、ADC、USART1和USART3
    GPIO_Init_For_Sensor();
    ADC_Function_Init();
    USART1_Init();
    USART3_Init();  // 在全局中断使能前配置USART3

#if DEBUG_MODE
    printf("[DEBUG] All peripherals initialized\r\n");
#endif
    
    // 使能全局中断（在所有NVIC配置完成后）
    __enable_irq();

#if DEBUG_MODE
    // 验证全局中断是否真的被启用（读取 mstatus 寄存器）
    uint32_t mstatus, mtvec, mie;
    __asm volatile ("csrr %0, mstatus" : "=r" (mstatus));
    __asm volatile ("csrr %0, mtvec" : "=r" (mtvec));
    __asm volatile ("csrr %0, mie" : "=r" (mie));

    printf("[DEBUG] Global interrupts enabled\r\n");
    printf("[DEBUG] mstatus register=0x%08lX (bit3=MIE)\r\n", (unsigned long)mstatus);
    printf("[DEBUG] MIE bit (global interrupt enable) = %s\r\n", (mstatus & 0x08) ? "ENABLED" : "DISABLED");
    printf("[DEBUG] mtvec (interrupt vector table) = 0x%08lX\r\n", (unsigned long)mtvec);
    printf("[DEBUG] mie (machine interrupt enable) = 0x%08lX\r\n", (unsigned long)mie);

    // 再次检查USART3状态
    printf("[DEBUG] USART3 STATR=0x%04X, CTLR1=0x%04X\r\n",
           (unsigned int)USART3->STATR, (unsigned int)USART3->CTLR1);
#endif

    // 增加上电延时，确保ESP8266完全启动
    printf("Waiting for ESP8266 to initialize...\r\n");
    Delay_Ms(3000); // 3秒延时
    
    // 输出启动信息
    printf("\r\n=== Water Immersion Detection System Started ===\r\n");
    printf("Water detection threshold: %dmV\r\n", WATER_THRESHOLD_MV);
    printf("WiFi SSID: %s\r\n", WIFI_SSID);
    
    // 初始化ESP8266
    printf("Attempting to initialize ESP8266...\r\n");
    if(ESP8266_Init())
    {
        printf("ESP8266 WiFi module connected successfully!\r\n");
        esp_initialized = 1;
        
        // 发送系统启动通知
        printf("Sending system startup notification...\r\n");

        // 准备详细的启动消息
        char startup_msg[256];
        sprintf(startup_msg,
                "【系统启动】\n"
                "浸水检测系统已成功启动\n"
                "WiFi: %s\n"
                "阈值: %dmV\n"
                "状态: 正常运行",
                WIFI_SSID,
                WATER_THRESHOLD_MV);

        // 发送启动通知
        if(ESP8266_SendWebhookAlert(startup_msg))
        {
            printf("Startup notification sent successfully!\r\n");
        }
        else
        {
            printf("Failed to send startup notification.\r\n");
        }
    }
    else
    {
        printf("Failed to initialize ESP8266 WiFi module! Continuing without WiFi.\r\n");
    }

    // 主循环
    printf("Entering main loop...\r\n");
    printf("\r\n=== Interactive Test Mode ===\r\n");
    printf("Type 2 characters to send as command to ESP8266\r\n");
    printf("For example: type 'AT' to send 'AT\\r\\n'\r\n");
    printf("Or type 'XX' + Enter manually in your terminal if it supports\r\n");
    printf("Waiting for input...\r\n\r\n");

    char test_buffer[128];
    uint8_t test_index = 0;

    while(1)
    {
        // 检查 USART1 是否有输入
        if(USART_GetFlagStatus(USART1, USART_FLAG_RXNE) != RESET)
        {
            uint8_t c = USART_ReceiveData(USART1);

            // 显示接收到的字符（调试）
            printf("[RX: 0x%02X='%c'] ", c, (c >= 32 && c <= 126) ? c : '.');

            // 处理回车换行
            if(c == '\r' || c == '\n')
            {
                if(test_index > 0)
                {
                    // 有数据就发送
                    goto send_command;
                }
            }
            else if(c == 0x08 || c == 0x7F)  // 退格键
            {
                if(test_index > 0)
                {
                    test_index--;
                    printf("\b \b");
                }
            }
            else if(c >= 32 && c <= 126)  // 可打印字符
            {
                if(test_index < sizeof(test_buffer) - 1)
                {
                    test_buffer[test_index++] = c;
                    printf("%c", c);  // 回显

                    // 自动发送：输入2个字符后自动发送（方便测试AT）
                    if(test_index == 2)
                    {
                        printf(" (auto-send)");
                        Delay_Ms(100);  // 短暂延时
                        goto send_command;
                    }
                }
            }
            continue;

send_command:
            test_buffer[test_index] = '\0';

            printf("\r\n[TEST] Sending: '%s' (+ \\r\\n)\r\n", test_buffer);

            // 清空接收缓冲区并重新初始化DMA
            memset((void*)esp_rx_buffer, 0, sizeof(esp_rx_buffer));
            esp_rx_index = 0;

            // 重新配置DMA以接收新数据
            DMA_Cmd(DMA1_Channel3, DISABLE);
            DMA_SetCurrDataCounter(DMA1_Channel3, sizeof(esp_rx_buffer));
            DMA_Cmd(DMA1_Channel3, ENABLE);

            // 发送到 USART3
            for(int i = 0; i < test_index; i++)
            {
                while(USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET);
                USART_SendData(USART3, test_buffer[i]);
            }

            // 发送 \r\n
            while(USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET);
            USART_SendData(USART3, '\r');
            while(USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET);
            USART_SendData(USART3, '\n');

            printf("[TEST] Waiting 2s for response (DMA mode)...\r\n");

            // 等待并接收响应（使用DMA）
            uint32_t wait_time = 0;
            uint32_t last_received = 0;

            while(wait_time < 2000)
            {
                // 读取DMA已接收的字节数
                uint32_t dma_remaining = DMA_GetCurrDataCounter(DMA1_Channel3);
                uint32_t bytes_received = sizeof(esp_rx_buffer) - dma_remaining;
                esp_rx_index = bytes_received;

                // 如果有新数据，实时显示
                if(bytes_received > last_received)
                {
                    for(uint32_t i = last_received; i < bytes_received; i++)
                    {
                        uint8_t data = esp_rx_buffer[i];
                        if(data >= 32 && data <= 126)
                            printf("%c", data);
                        else if(data == '\r')
                            printf("<CR>");
                        else if(data == '\n')
                            printf("<LF>\r\n");
                        else
                            printf("[%02X]", data);
                    }
                    last_received = bytes_received;
                }

                Delay_Ms(10);
                wait_time += 10;
            }

            printf("\r\n[TEST] Total received: %d bytes\r\n", esp_rx_index);
            printf("[TEST] Hex dump: ");
            for(int i = 0; i < esp_rx_index && i < 50; i++)
            {
                printf("%02X ", esp_rx_buffer[i]);
            }
            printf("\r\n[TEST] Ready for next command\r\n\r\n");

            test_index = 0;
        }

        // 原有的传感器检测（每秒一次）
        static uint32_t last_sensor_check = 0;
        static uint32_t global_timer = 0;
        global_timer++;

        if(global_timer - last_sensor_check >= 1000)
        {
            Sensor_Status_Check();
            LED_Control();
            USART_Report_Status();
            last_sensor_check = global_timer;
        }

        Delay_Ms(1);
    }
}

