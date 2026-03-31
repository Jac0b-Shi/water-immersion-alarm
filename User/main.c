/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : Jac0b_Shi (SHU-SPE-Sandrone)
 * Version            : V1.3.0
 * Date               : 2026/03/31
 * Description        : 浸水检测报警系统 - 主程序
 *                      基于CH32V208 RISC-V微控制器，支持多种通信方式
 *                      实时浸水检测和企业微信报警推送
 * Repository         : https://gitee.com/SHU-SPE-Sandrone/water-immersion-alarm
 * Issues             : https://gitee.com/SHU-SPE-Sandrone/water-immersion-alarm/issues
 *********************************************************************************
 * Copyright (c) 2025-2026 SHU-SPE-Sandrone
 *
 * This project is licensed under the GNU Lesser General Public License v3.0
 * You may obtain a copy of the license at:
 *     https://www.gnu.org/licenses/lgpl-3.0.html
 *
 * 本项目基于WCH官方CH32V20x示例代码修改而来
 * Original work Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 *
 * 主要功能：
 * - 通过ADC检测浸水传感器模拟量（阈值可配置，默认1000mV）
 * - 双LED指示系统状态（绿色=正常，红色=报警）
 * - 支持多种通信方式（可在config.h中配置）：
 *   - CH32V208内置10M以太网（推荐，需HTTP代理转发HTTPS）
 *   - ESP8266 WiFi模块
 *   - BC260 NB-IoT模块（需HTTP代理转发HTTPS）
 * - 通过企业微信Webhook API推送报警消息
 * - 完整的状态管理和去抖动处理
 *******************************************************************************/

/*
 *@Note
 * 硬件连接说明（CH32V208WBU6）：
 *
 * ADC输入：
 * - PA0：浸水传感器模拟量输入（ADC值与电压mV约为1:1关系）
 *
 * LED指示灯（共阳极，低电平点亮）：
 * - PB0：正常状态指示灯（绿色）- 无水时点亮
 * - PB1：报警状态指示灯（红色）- 检测到浸水时点亮
 *
 * 调试串口（USART1，115200波特率，8N1）：
 * - PA9：USART1_TX - 系统日志输出
 * - PA10：USART1_RX - 保留
 *
 * 内置10M以太网（CH32V208特有）：
 * - 使用内置PHY，需连接RJ45网口
 * - ELED1/ELED2：以太网状态指示灯
 * - 注意：不支持SSL/TLS，需通过HTTP代理服务器转发HTTPS请求
 *
 * ESP8266 WiFi（可选，USART3，115200波特率）：
 * - PB10：USART3_TX
 * - PB11：USART3_RX
 * - PB12：ESP8266_RST（低电平复位）
 *
 * BC260 NB-IoT（可选，USART2，9600波特率）：
 * - PA2：USART2_TX
 * - PA3：USART2_RX
 * - PA1：BC260_RST（高电平复位）
 *
 * 配置说明：
 * - 网络配置：编辑 config.env 后运行 generate_config.py 生成 config.h
 * - 检测阈值：修改 WATER_THRESHOLD_MV 宏（默认1000mV）
 * - 调试模式：修改 DEBUG_MODE 宏（1=详细日志，0=精简日志）
 * - 通信模块：在 config.h 中设置 ENABLE_ETHERNET/ENABLE_ESP8266/ENABLE_BC260
 */

#include "debug.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"
#include "ch32v20x_usart.h"
#include "ch32v20x_adc.h"
#include "ch32v20x_misc.h"
#include "ch32v20x_tim.h"
#include <stdbool.h>
#include "string.h"
#include "stdio.h"
#include "config.h"  // 包含配置文件（WiFi和Webhook配置）

#if ENABLE_ETHERNET
#include "eth_driver.h"
#endif

// 调试信息：芯片型号检测
#ifdef CH32V20x_D6
    #pragma message("编译目标：CH32V203系列")
#elif defined(CH32V20x_D8)
    #pragma message("编译目标：CH32V203RBT")
#elif defined(CH32V20x_D8W)
    #pragma message("编译目标：CH32V208WBU6")
#endif

/*********************************************************************
 * 全局变量定义
 *********************************************************************/

#if ENABLE_ETHERNET
/* 调试用：TIM2中断计数器（定义在ch32v20x_it.c中） */
extern volatile uint32_t tim2_isr_count;
#endif

/* 浸水检测相关全局变量 */
volatile uint8_t water_status = 0;       // 当前水位状态：0=无水，1=有水
volatile uint8_t last_water_status = 0;  // 上一次的水位状态，用于检测状态变化
volatile uint16_t adc_value = 0;         // ADC原始转换结果（0-4095）
volatile uint16_t voltage_mv = 0;        // 转换后的电压值（单位：毫伏）

/* ESP8266通信相关全局变量 */
#if ENABLE_ESP8266
#define ESP_RX_BUFFER_SIZE 256           // 定义缓冲区大小为256字节
volatile uint8_t esp_tx_buffer[ESP_RX_BUFFER_SIZE];  // ESP8266发送缓冲区
volatile uint8_t esp_rx_buffer[ESP_RX_BUFFER_SIZE];  // ESP8266接收缓冲区（DMA使用）
volatile uint16_t esp_rx_index = 0;                   // 接收缓冲区当前索引
volatile uint8_t esp_response_received = 0;           // 响应接收完成标志
volatile uint8_t esp_initialized = 0;                 // ESP8266初始化完成标志
#endif

/* BC260 NB-IoT通信相关全局变量 */
#if ENABLE_BC260
#define BC260_RX_BUFFER_SIZE 256           // BC260接收缓冲区大小
volatile uint8_t BC260_rx_buffer[BC260_RX_BUFFER_SIZE];  // BC260接收缓冲区
volatile uint16_t BC260_rx_index = 0;                    // 接收缓冲区当前索引
volatile uint8_t BC260_response_received = 0;            // 响应接收完成标志
volatile uint8_t BC260_initialized = 0;                  // BC260初始化完成标志
volatile uint8_t BC260_network_attached = 0;             // 网络附着状态
#endif

/* 以太网通信相关全局变量 */
#if ENABLE_ETHERNET
uint8_t MACAddr[6];                                      // MAC地址
uint8_t IPAddr[4] = ETH_IP_ADDR;                         // IP地址
uint8_t GWIPAddr[4] = ETH_GATEWAY;                       // 网关IP地址
uint8_t IPMask[4] = ETH_NETMASK;                         // 子网掩码
uint8_t DNSAddr[4] = ETH_DNS_SERVER;                     // DNS服务器地址

volatile uint8_t eth_initialized = 0;                    // 以太网初始化完成标志
volatile uint8_t eth_link_status = 0;                    // 以太网链接状态
volatile uint8_t eth_socket_connected = 0;               // Socket连接状态

uint8_t eth_socket_id = 0;                               // 当前使用的socket ID
uint16_t eth_src_port_counter = 0;                       // 源端口计数器（用于动态分配）
uint8_t eth_socket[WCHNET_MAX_SOCKET_NUM];               // Socket数组
uint8_t eth_recv_buf[WCHNET_MAX_SOCKET_NUM][RECE_BUF_LEN]; // Socket接收缓冲区
#endif

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
#if ENABLE_ESP8266
#define ESP8266_RST_PORT GPIOB           // 复位引脚端口
#define ESP8266_RST_PIN  GPIO_Pin_12     // 复位引脚编号（PB12）
#endif

/* BC260 NB-IoT硬件复位引脚 */
#if ENABLE_BC260
#define BC260_RST_PORT    GPIOA           // 复位引脚端口
#define BC260_RST_PIN     GPIO_Pin_1      // 复位引脚编号（PA1）
#endif

/*********************************************************************
 * 函数声明
 *********************************************************************/
#if ENABLE_ESP8266
void ESP8266_RST_Control(uint8_t state);
void ESP8266_HardReset(void);
uint8_t ESP8266_SendCommand(char* cmd, uint32_t timeout);
uint8_t ESP8266_Init(void);
uint8_t ESP8266_SendWebhookAlert(char* alert_msg);
#endif

/* BC260 NB-IoT相关函数声明 */
#if ENABLE_BC260
void BC260_RST_Control(uint8_t state);
void BC260_HardReset(void);
void BC260_RecoveryReset(void);
uint8_t BC260_SendCommand(char* cmd, char* expected_resp, uint32_t timeout);
uint8_t BC260_CheckNetworkAttach(void);
uint8_t BC260_Init(void);
uint8_t BC260_ReceiveHTTPResponse(char* buffer, uint16_t buffer_size, uint32_t timeout);
uint8_t BC260_SendAlert(char* alert_msg);
#endif

/* 以太网相关函数声明 */
#if ENABLE_ETHERNET
void TIM2_Init(void);
void ETH_HandleGlobalInt(void);
void ETH_HandleSocketInt(uint8_t socketid, uint8_t intstat);
uint8_t ETH_ConnectToServer(const char* host, uint16_t port);
uint8_t ETH_SendWebhookAlert(char* alert_msg);
void ETH_PrintMacAddr(void);
#endif

/*********************************************************************
 * @fn      Soft_Delay_Ms
 *
 * @brief   软件循环延时（毫秒级）
 *          使用CPU空循环实现，不依赖硬件定时器
 *          适用于定时器资源被占用或中断禁用的场景
 *
 * @param   ms - 延时毫秒数
 *
 * @return  none
 *
 * @note    延时精度受系统时钟和编译优化影响
 *          120MHz时钟下约12000次循环≈1ms
 */
static void Soft_Delay_Ms(uint32_t ms)
{
    volatile uint32_t i, j;
    volatile uint32_t dummy = 0;
    for(i = 0; i < ms; i++)
    {
        for(j = 0; j < 12000; j++)
        {
            dummy++;
        }
    }
    (void)dummy;
}

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
 *          PA1 - BC260 RST控制引脚（推挽输出）
 *          PB0 - LED1控制（推挽输出）
 *          PB1 - LED2控制（推挽输出）
 *          PB12 - ESP8266 RST控制引脚（推挽输出）
 *
 * @return  none
 */
void GPIO_Init_For_Sensor(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

#if DEBUG_MODE
    printf("[DEBUG] Starting GPIO initialization\r\n");
    printf("[DEBUG] SystemCoreClock = %ld Hz\r\n", SystemCoreClock);
#endif

    // 确保GPIOB时钟使能 - 这是修复PB0/PB1无输出的关键
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    
#if DEBUG_MODE
    printf("[DEBUG] GPIOB clock enabled\r\n");
#endif

#if ENABLE_BC260
    // 配置PA1为推挽输出（BC260 RST控制）
    GPIO_InitStructure.GPIO_Pin = BC260_RST_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BC260_RST_PORT, &GPIO_InitStructure);
#endif

#if ENABLE_ESP8266
    // 配置PB12为推挽输出（ESP8266 RST控制）
    GPIO_InitStructure.GPIO_Pin = ESP8266_RST_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ESP8266_RST_PORT, &GPIO_InitStructure);
#endif

    // 配置PB0和PB1为推挽输出（LED控制）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

#if DEBUG_MODE
    printf("[DEBUG] GPIO initialization completed\r\n");
    printf("[DEBUG] PB0/PB1 configured as push-pull output\r\n");
#endif

    // 初始化LED状态 - 默认无水状态（LED1亮，LED2灭）
    GPIO_SetBits(GPIOB, GPIO_Pin_1);  // 熄灭LED2（PB1）
    GPIO_ResetBits(GPIOB, GPIO_Pin_0); // 点亮LED1（PB0）
    
#if DEBUG_MODE
    printf("[DEBUG] Initial LED state set - PB0=LOW(ON), PB1=HIGH(OFF)\r\n");
    printf("[DEBUG] PB0 INDR=0x%04X, OUTDR=0x%04X\r\n", (unsigned int)GPIOB->INDR, (unsigned int)GPIOB->OUTDR);
#endif
    
#if ENABLE_ESP8266
    // 初始化ESP8266 RST引脚为高电平（非复位状态）
    GPIO_SetBits(ESP8266_RST_PORT, ESP8266_RST_PIN);
#endif

#if ENABLE_BC260
    // 初始化BC260 RST引脚为低电平（非复位状态，高电平复位模式）
    GPIO_ResetBits(BC260_RST_PORT, BC260_RST_PIN);
#endif

#if DEBUG_MODE
    printf("[DEBUG] RST pins initialized\r\n");
#endif
}

#if ENABLE_ESP8266
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
#endif /* ENABLE_ESP8266 - ESP8266_RST_Control and ESP8266_HardReset */

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

#if ENABLE_ESP8266
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
#endif /* ENABLE_ESP8266 */

#if ENABLE_BC260
/*********************************************************************
 * @fn      USART2_Init
 *
 * @brief   初始化USART2，用于与BC260 NB-IoT模块通信
 *          PA2 - USART2_TX
 *          PA3 - USART2_RX
 *
 * @return  none
 */
void USART2_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};

    // 配置PA2为USART2_TX（复用推挽输出）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 配置PA3为USART2_RX（浮空输入）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // USART2配置 - 9600波特率，8数据位，1停止位，无校验位（8N1）
    // BC260默认波特率为9600
    USART_InitStructure.USART_BaudRate = 9600;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;

    USART_Init(USART2, &USART_InitStructure);

    // 使能USART2
    USART_Cmd(USART2, ENABLE);

#if DEBUG_MODE
    printf("[DEBUG] USART2 enabled for BC260 NB-IoT (9600 baud)\r\n");
    printf("[DEBUG] USART2 CTLR1=0x%04X\r\n", (unsigned int)USART2->CTLR1);
#endif
}
#endif /* ENABLE_BC260 */

/*********************************************************************
 * @fn      ADC_Read_Voltage
 *
 * @brief   读取浸水传感器电压值
 *          通过ADC采样PA0引脚的模拟信号
 *
 * @return  电压值(单位:mV)
 *
 * @note    实测ADC值与电压(mV)约为1:1关系
 *          采样时间239.5周期，适合高阻抗信号源
 */
uint16_t ADC_Read_Voltage(void)
{
    uint16_t adc_val;

    // 配置ADC通道0(PA0)，使用较长采样时间保证精度
    ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_239Cycles5);
    
    // 触发软件转换
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    
    // 等待转换完成
    while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));
    
    // 读取转换结果
    adc_val = ADC_GetConversionValue(ADC1);
    
    // 清除转换完成标志
    ADC_ClearFlag(ADC1, ADC_FLAG_EOC);
    
    // 返回电压值(mV) - ADC值与电压1:1映射
    return adc_val;
}

/*********************************************************************
 * @fn      Sensor_Status_Check
 *
 * @brief   检查浸水传感器状态
 *          读取ADC电压值，通过去抖动算法判断浸水状态
 *
 * @return  none
 *
 * @note    使用双阈值+计数器去抖动：
 *          - 电压 >= WATER_THRESHOLD_MV 且连续 WATER_CONFIRM_COUNT 次 → 确认浸水
 *          - 电压 < NO_WATER_THRESHOLD_MV 且连续 NO_WATER_CONFIRM_COUNT 次 → 确认干燥
 *          - 电压在中间区域 → 保持当前状态
 */
void Sensor_Status_Check(void)
{
    // 读取传感器电压值
    voltage_mv = ADC_Read_Voltage();
    adc_value = voltage_mv;  // ADC值与电压1:1映射

    // 输出传感器状态日志
    printf("[SENSOR] ADC: %d, Voltage: %dmV, Threshold: %dmV, Status: %s\r\n",
           adc_value, voltage_mv, WATER_THRESHOLD_MV,
           (voltage_mv >= WATER_THRESHOLD_MV) ? "WET" : "DRY");

    // 去抖动状态机
    if(voltage_mv >= WATER_THRESHOLD_MV)
    {
        // 检测到高电压（可能浸水）
        water_counter++;
        no_water_counter = 0;

        if(water_counter >= WATER_CONFIRM_COUNT)
        {
            water_status = 1;  // 确认浸水
            water_counter = 0;
        }
    }
    else if(voltage_mv < NO_WATER_THRESHOLD_MV)
    {
        // 检测到低电压（可能干燥）
        no_water_counter++;
        water_counter = 0;

        if(no_water_counter >= NO_WATER_CONFIRM_COUNT)
        {
            water_status = 0;  // 确认干燥
            no_water_counter = 0;
        }
    }
    else
    {
        // 电压在迟滞区域，保持当前状态
        water_counter = 0;
        no_water_counter = 0;
    }
}

/*********************************************************************
 * @fn      LED_Control
 *
 * @brief   根据传感器状态控制LED显示
 *          无水时：点亮PB0的LED（LED3/绿色），熄灭PB1的LED（LED4/红色）
 *          有水时：点亮PB1的LED（LED4/红色），熄灭PB0的LED（LED3/绿色）
 *
 * @return  none
 */
void LED_Control(void)
{
    static uint8_t last_led_state = 0xFF;  // 用于检测LED状态变化

    if(water_status == 0)
    {
        // 无水状态
        GPIO_ResetBits(GPIOB, GPIO_Pin_0);  // 点亮LED3（PB0）- 共阳极，低电平点亮
        GPIO_SetBits(GPIOB, GPIO_Pin_1);    // 熄灭LED4（PB1）

        if(last_led_state != 0)
        {
            printf("[LED] State: DRY - LED3(PB0)=ON(green), LED4(PB1)=OFF\r\n");
            last_led_state = 0;
        }
    }
    else
    {
        // 有水状态
        GPIO_SetBits(GPIOB, GPIO_Pin_0);    // 熄灭LED3（PB0）
        GPIO_ResetBits(GPIOB, GPIO_Pin_1);  // 点亮LED4（PB1）- 共阳极，低电平点亮

        if(last_led_state != 1)
        {
            printf("[LED] State: WET - LED3(PB0)=OFF, LED4(PB1)=ON(red)\r\n");
            last_led_state = 1;
        }
    }
}

#if ENABLE_ESP8266
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
    char *escaped_msg = json_escape(alert_msg);
    sprintf(json_body,
            "{\"msgtype\":\"text\",\"text\":{\"content\":\"%s\"}}",
            escaped_msg);
    free(escaped_msg);
    
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
#endif /* ENABLE_ESP8266 */

#if ENABLE_BC260
/*********************************************************************
 * BC260 NB-IoT 模块相关函数
 *********************************************************************/

/*********************************************************************
 * @fn      BC260_RST_Control
 *
 * @brief   控制BC260的RST引脚（高电平复位模式）
 *
 * @param   state - 0表示正常运行(Low)，1表示复位(High)
 *
 * @return  none
 */
void BC260_RST_Control(uint8_t state)
{
    if(state)
    {
        GPIO_SetBits(BC260_RST_PORT, BC260_RST_PIN);  // 拉高RST引脚（复位）
    }
    else
    {
        GPIO_ResetBits(BC260_RST_PORT, BC260_RST_PIN);  // 拉低RST引脚（正常运行）
    }
}

/*********************************************************************
 * @fn      BC260_HardReset
 *
 * @brief   对BC260执行硬复位（高电平复位模式）
 *          拉高RST引脚复位，保持至少300ms
 *
 * @return  none
 */
void BC260_HardReset(void)
{
    printf("Performing BC260 hard reset (active high)...\r\n");
    
    // 确保RST引脚先处于低电平状态（正常运行）
    BC260_RST_Control(0);
    Delay_Ms(100);
    
    // 拉高RST引脚复位（至少300ms）
    BC260_RST_Control(1);
    Delay_Ms(300);        // 保持高电平300ms，确保可靠复位
    
    // 释放RST引脚（恢复低电平）
    BC260_RST_Control(0);
    
    // 等待模块重新启动
    // BC260从深睡眠唤醒或复位后需要较长时间初始化
    printf("Waiting for BC260 to boot...\r\n");
    Delay_Ms(8000);       // 等待8秒，确保模块完全启动
    
    printf("BC260 hard reset completed.\r\n");
}

/*********************************************************************
 * @fn      BC260_SendCommand
 *
 * @brief   发送AT指令给BC260并等待响应
 *
 * @param   cmd - 要发送的命令字符串
 * @param   expected_resp - 期望的响应字符串（如"OK"），NULL表示不检查
 * @param   timeout - 超时时间(毫秒)
 *
 * @return  1-成功 0-失败
 */
uint8_t BC260_SendCommand(char* cmd, char* expected_resp, uint32_t timeout)
{
    uint32_t start_time = 0;
    const char* p = cmd;

    // 清空接收缓冲区
    memset((void*)BC260_rx_buffer, 0, sizeof(BC260_rx_buffer));
    BC260_rx_index = 0;
    BC260_response_received = 0;

    // 发送命令
    printf("[BC260] Sending: %s\r\n", cmd);
    while(*p)
    {
        while(USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
        USART_SendData(USART2, *p);
        p++;
    }

    // 发送 \r\n
    while(USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
    USART_SendData(USART2, '\r');
    while(USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
    USART_SendData(USART2, '\n');

    // 等待响应（轮询模式）
    while(start_time < timeout)
    {
        // 检查是否有数据接收
        if(USART_GetFlagStatus(USART2, USART_FLAG_RXNE) != RESET)
        {
            uint8_t data = USART_ReceiveData(USART2);

            if(BC260_rx_index < sizeof(BC260_rx_buffer) - 1)
            {
                BC260_rx_buffer[BC260_rx_index++] = data;
                BC260_rx_buffer[BC260_rx_index] = '\0';

                // 检查是否收到完整响应
                if(BC260_rx_index >= 2 &&
                   BC260_rx_buffer[BC260_rx_index-2] == '\r' &&
                   BC260_rx_buffer[BC260_rx_index-1] == '\n')
                {
                    // 检查是否包含期望的响应
                    if(expected_resp != NULL)
                    {
                        if(strstr((char*)BC260_rx_buffer, expected_resp))
                        {
                            BC260_response_received = 1;
                            break;
                        }
                        // 继续等待更多数据
                    }
                    else
                    {
                        BC260_response_received = 1;
                        break;
                    }
                }
            }
        }

        Delay_Ms(1);
        start_time++;
    }

    if(BC260_rx_index > 0)
    {
        printf("[BC260] Response: %s\r\n", (char*)BC260_rx_buffer);
    }

    if(expected_resp != NULL)
    {
        if(strstr((char*)BC260_rx_buffer, expected_resp))
        {
            return 1;
        }
        else if(strstr((char*)BC260_rx_buffer, "ERROR"))
        {
            printf("[BC260] ERROR response received\r\n");
            return 0;
        }
    }

    return BC260_response_received;
}

/*********************************************************************
 * @fn      BC260_CheckNetworkAttach
 *
 * @brief   检查BC260是否已附着到移动网络
 *
 * @return  1-已附着 0-未附着
 */
uint8_t BC260_CheckNetworkAttach(void)
{
    printf("[BC260] Checking network attachment...\r\n");

    // 发送AT+CGATT?查询网络附着状态
    if(BC260_SendCommand("AT+CGATT?", "+CGATT:", 5000))
    {
        // 检查响应中是否包含"+CGATT: 1"表示已附着 (注意：标准响应包含空格)
        if(strstr((char*)BC260_rx_buffer, "+CGATT: 1"))
        {
            printf("[BC260] Network attached!\r\n");
            BC260_network_attached = 1;
            return 1;
        }
        else if(strstr((char*)BC260_rx_buffer, "+CGATT: 0"))
        {
            printf("[BC260] Network not attached yet.\r\n");
            BC260_network_attached = 0;
            return 0;
        }
    }

    printf("[BC260] Failed to check network attachment.\r\n");
    return 0;
}

/*********************************************************************
 * @fn      BC260_Init
 *
 * @brief   初始化BC260 NB-IoT模块
 *
 * @return  1-成功 0-失败
 */
uint8_t BC260_Init(void)
{
    uint8_t retry;
    uint8_t reset_attempt;
    uint32_t flush_time;

    printf("\r\n=== Initializing BC260 NB-IoT Module ===\r\n");

    for(reset_attempt = 0; reset_attempt < 2; reset_attempt++)
    {
        // 硬复位BC260
        BC260_HardReset();

        // 清空启动阶段可能输出的URC/日志，避免干扰AT响应判断
        // BC260启动时会输出大量URC（如+QNBIOTEVENT等），需要充分清空
        printf("[BC260] Flushing boot messages...\r\n");
        flush_time = 0;
        while(flush_time < 3000)  // 增加清空时间到3秒
        {
            if(USART_GetFlagStatus(USART2, USART_FLAG_RXNE) != RESET)
            {
                USART_ReceiveData(USART2);
            }
            Delay_Ms(1);
            flush_time++;
        }
        printf("[BC260] Boot messages flushed.\r\n");

        // 测试AT通信
        printf("[BC260] Testing AT communication (attempt %d/2)...\r\n", reset_attempt + 1);
        for(retry = 0; retry < 5; retry++)
        {
            if(BC260_SendCommand("AT", "OK", 2000))
            {
                printf("[BC260] AT communication OK!\r\n");
                break;
            }
            Delay_Ms(1000);
        }
        if(retry < 5)
        {
            break;  // AT通信成功，退出复位重试循环
        }

        printf("[BC260] AT communication failed after reset attempt %d.\r\n", reset_attempt + 1);
    }

    if(reset_attempt >= 2)
    {
        printf("[BC260] AT communication failed after 2 hard resets!\r\n");
        return 0;
    }

    Delay_Ms(500);

    // 关闭回显
    BC260_SendCommand("ATE0", "OK", 2000);
    Delay_Ms(500);

    // 查询模块信息
    BC260_SendCommand("ATI", "OK", 2000);
    Delay_Ms(500);

    // 查询IMEI
    BC260_SendCommand("AT+CGSN=1", "OK", 2000);
    Delay_Ms(500);

    // 查询SIM卡状态
    printf("[BC260] Checking SIM card...\r\n");
    for(retry = 0; retry < 3; retry++)
    {
        if(BC260_SendCommand("AT+CIMI", "OK", 3000))
        {
            printf("[BC260] SIM card detected!\r\n");
            break;
        }
        Delay_Ms(1000);
    }
    if(retry >= 3)
    {
        printf("[BC260] SIM card not detected!\r\n");
        return 0;
    }

    Delay_Ms(500);

    // 设置网络功能
    // 设置APN
    BC260_SendCommand("AT+CGDCONT=1,\"IP\",\"cmnbiot\"", "OK", 5000);
    Delay_Ms(500);

    // 开启射频功能
    BC260_SendCommand("AT+CFUN=1", "OK", 5000);
    Delay_Ms(1000);

    // 自动附着网络
    BC260_SendCommand("AT+CGATT=1", "OK", 10000);
    Delay_Ms(2000);

    // 等待网络附着（最多等待60秒）
    printf("[BC260] Waiting for network attachment...\r\n");
    for(retry = 0; retry < 30; retry++)
    {
        if(BC260_CheckNetworkAttach())
        {
            break;
        }
        Delay_Ms(2000);
        printf("[BC260] Still waiting... (%d/30)\r\n", retry + 1);
    }

    if(!BC260_network_attached)
    {
        printf("[BC260] Network attachment timeout! Module may work when network available.\r\n");
        // 不返回失败，允许后续尝试
    }

    // 查询信号强度
    BC260_SendCommand("AT+CSQ", "OK", 2000);

    BC260_initialized = 1;
    printf("[BC260] BC260 NB-IoT module initialized!\r\n");

    // 发送初始化完成消息（明确标注是BC260模块）
    printf("[BC260] Sending initialization complete message...\r\n");
    BC260_SendAlert("【NB-IoT系统启动】\\nBC260模块初始化完成\\n通信方式: NB-IoT\\n状态: 正常运行");

    return 1;
}

/*********************************************************************
 * @fn      BC260_RecoveryReset
 *
 * @brief   对BC260执行恢复性硬复位，用于发送失败等异常恢复
 *          复位后标记模块为未初始化状态，强制下次重新初始化
 *
 * @return  none
 */
void BC260_RecoveryReset(void)
{
    printf("[BC260] Performing recovery reset due to failure...\r\n");
    BC260_HardReset();
    BC260_initialized = 0;
    BC260_network_attached = 0;
    printf("[BC260] Recovery reset completed, module will re-init next time.\r\n");
}

/*********************************************************************
 * @fn      BC260_ReceiveHTTPResponse
 *
 * @brief   接收BC260通过+QIURC上报的HTTP响应数据
 *          BC260使用直吐模式，数据通过+QIURC: "recv",<connectID>,<length>,"<data>" URC上报
 *
 * @param   buffer - 接收缓冲区
 * @param   buffer_size - 缓冲区大小
 * @param   timeout - 超时时间(毫秒)
 *
 * @return  1-收到成功响应 0-未收到或解析失败
 */
uint8_t BC260_ReceiveHTTPResponse(char* buffer, uint16_t buffer_size, uint32_t timeout)
{
    uint32_t start_time = 0;
    uint16_t index = 0;
    uint8_t in_data = 0;       // 是否正在接收数据内容
    uint16_t data_len = 0;     // 期望的数据长度
    uint16_t recv_len = 0;     // 已接收的数据长度

    memset(buffer, 0, buffer_size);

    printf("[BC260] Waiting for HTTP response (timeout=%lums)...\r\n", (unsigned long)timeout);
    printf("[BC260] Note: Data is reported via +QIURC URC in transparent mode\r\n");

    while(start_time < timeout)
    {
        if(USART_GetFlagStatus(USART2, USART_FLAG_RXNE) != RESET)
        {
            uint8_t data = USART_ReceiveData(USART2);
            
            // 存储原始数据用于调试（限制大小）
            if(index < buffer_size - 1)
            {
                buffer[index++] = data;
                buffer[index] = '\0';
            }
            
            // 检查+QIURC: "recv" URC
            if(!in_data && strstr(buffer, "+QIURC: \"recv\""))
            {
                // 解析数据长度
                char* p = strstr(buffer, "+QIURC: \"recv\",");
                if(p)
                {
                    p += strlen("+QIURC: \"recv\",");
                    // 跳过connectID，找到长度
                    while(*p && *p != ',') p++;
                    if(*p == ',') p++;
                    // 解析长度
                    data_len = 0;
                    while(*p && *p >= '0' && *p <= '9')
                    {
                        data_len = data_len * 10 + (*p - '0');
                        p++;
                    }
                    if(*p == ',') p++;
                    // 跳过开头的引号
                    if(*p == '"') 
                    {
                        p++;
                    }
                    
                    printf("[BC260] +QIURC recv detected, expected data length: %d\r\n", data_len);
                    in_data = 1;
                    recv_len = 0;
                    
                    // 将已读取的数据内容复制到buffer开头
                    if(p && *p && p > buffer)
                    {
                        uint16_t content_len = strlen(p);
                        if(content_len > 0 && content_len < buffer_size)
                        {
                            memmove(buffer, p, content_len + 1);
                            recv_len = content_len;
                            index = content_len;
                        }
                    }
                }
            }
            else if(in_data)
            {
                // 继续接收数据内容
                recv_len++;
                
                // 检查数据结束（收到结束引号或达到期望长度）
                if(data_len > 0 && recv_len >= data_len)
                {
                    printf("[BC260] Data reception complete: %d bytes\r\n", recv_len);
                    // 保留数据但不退出，继续监听可能的其他URC
                }
            }
            
            // 检查连接关闭
            if(strstr(buffer, "+QIURC: \"closed\""))
            {
                printf("[BC260] Connection closed by peer\r\n");
            }
        }
        Delay_Ms(1);
        start_time++;
    }

    if(index > 0)
    {
        printf("[BC260] Total received %d bytes raw data\r\n", index);
        
        // 打印原始响应内容用于调试
        printf("[BC260] Raw response:\r\n");
        for(uint16_t i = 0; i < index && i < 500; i++)
        {
            if(buffer[i] >= 32 && buffer[i] <= 126)
            {
                printf("%c", buffer[i]);
            }
            else if(buffer[i] == '\r')
            {
                printf("\\r");
            }
            else if(buffer[i] == '\n')
            {
                printf("\\n\r\n");
            }
            else
            {
                printf("[%02X]", (uint8_t)buffer[i]);
            }
        }
        if(index > 500) printf("... (%d more bytes)", index - 500);
        printf("\r\n");

        // 检查HTTP状态码
        if(strstr(buffer, "HTTP/1.0 200") || strstr(buffer, "HTTP/1.1 200"))
        {
            printf("[BC260] HTTP 200 OK detected\r\n");
            
            // 检查企业微信返回
            if(strstr(buffer, "\"errcode\":0") || strstr(buffer, "\"errmsg\":\"ok\""))
            {
                printf("[BC260] Webhook response: success\r\n");
                return 1;
            }
            
            // 即使没找到明确的errcode，HTTP 200也算成功
            printf("[BC260] HTTP 200 received, assuming success\r\n");
            return 1;
        }
        
        // 如果没找到HTTP/1.x 200，但包含了成功标记
        if(strstr(buffer, "errcode") && strstr(buffer, "0"))
        {
            printf("[BC260] Success marker found in response\r\n");
            return 1;
        }
    }

    printf("[BC260] No valid HTTP response received\r\n");
    return 0;
}

/*********************************************************************
 * @fn      BC260_SendAlert
 *
 * @brief   通过BC260 NB-IoT发送警报消息到企业微信Webhook
 *          使用BC260Y-CN标准TCP/IP指令集(AT+QIxxx)
 *          注意：BC260不支持HTTPS，需要通过HTTP代理转发
 *
 * @param   alert_msg - 警报消息内容
 *
 * @return  1-成功 0-失败
 */
uint8_t BC260_SendAlert(char* alert_msg)
{
    char cmd[256];
    char json_body[256];
    char http_request[768];
    char response_buf[512];
    uint16_t json_len;
    uint16_t http_len;
    uint8_t proxy_ip[4] = BC260_PROXY_IP;
    const char* p;
    uint32_t start_time = 0;

    if(!BC260_initialized)
    {
        printf("[BC260] Module not initialized, skipping alert.\r\n");
        return 0;
    }

    // 检查网络附着状态
    if(!BC260_network_attached)
    {
        // 尝试重新检查网络状态
        if(!BC260_CheckNetworkAttach())
        {
            printf("[BC260] Network not attached, cannot send alert.\r\n");
            return 0;
        }
    }

    printf("[BC260] Sending alert via HTTP proxy: %s\r\n", alert_msg);

    // 准备 JSON body（与ESP8266相同格式）
    sprintf(json_body,
        "{\"msgtype\":\"text\",\"text\":{\"content\":\"%s\"}}",
        alert_msg);

    json_len = strlen(json_body);

    // 准备完整的 HTTP POST 请求（发送到代理服务器）
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

    printf("[BC260] HTTP request length: %d bytes\r\n", http_len);
    printf("[BC260] HTTP request preview:\r\n");
    // 打印HTTP请求前200字节用于调试
    for(int i = 0; i < http_len && i < 200; i++)
    {
        if(http_request[i] >= 32 && http_request[i] <= 126)
            printf("%c", http_request[i]);
        else if(http_request[i] == '\r')
            printf("\\r");
        else if(http_request[i] == '\n')
            printf("\\n\r\n");
        else
            printf("[%02X]", (uint8_t)http_request[i]);
    }
    if(http_len > 200) printf("... (%d more bytes)", http_len - 200);
    printf("\r\n");
    
    printf("[BC260] Connecting to proxy: %d.%d.%d.%d:%d\r\n",
           proxy_ip[0], proxy_ip[1], proxy_ip[2], proxy_ip[3], BC260_PROXY_PORT);

    uint8_t send_attempt;
    uint8_t prompt_received;
    uint8_t connected;
    uint8_t state_val;
    char* state_ptr;

    // 1. 关闭可能存在的旧连接，并等待资源完全释放
    BC260_SendCommand("AT+QICLOSE=0", "OK", 5000);
    Delay_Ms(2000);  // 等待 CLOSE OK 等延迟URC上报
    // 消费可能残留的URC数据
    start_time = 0;
    while(start_time < 2000)
    {
        if(USART_GetFlagStatus(USART2, USART_FLAG_RXNE) != RESET)
        {
            USART_ReceiveData(USART2);
        }
        Delay_Ms(1);
        start_time++;
    }

    // 2. 配置数据格式为文本字符串
    if(!BC260_SendCommand("AT+QICFG=\"dataformat\",0,0", "OK", 3000))
    {
        printf("[BC260] Failed to configure data format!\r\n");
        BC260_RecoveryReset();
        return 0;
    }

    // 3. 打开TCP连接 (contextID=0, connectID=0)
    sprintf(cmd, "AT+QIOPEN=0,0,\"TCP\",\"%d.%d.%d.%d\",%d",
            proxy_ip[0], proxy_ip[1], proxy_ip[2], proxy_ip[3], BC260_PROXY_PORT);
    if(!BC260_SendCommand(cmd, "OK", 15000))
    {
        printf("[BC260] Failed to open TCP connection!\r\n");
        BC260_RecoveryReset();
        return 0;
    }

    // 等待连接真正建立完成：轮询 QISTATE 直到 state=2 (Connected)，最多30秒
    printf("[BC260] Waiting for TCP connection to be fully established...\r\n");
    connected = 0;
    start_time = 0;
    while(start_time < 30000)
    {
        // 每500ms查询一次
        if((start_time % 500) == 0 && start_time > 0)
        {
            if(BC260_SendCommand("AT+QISTATE?", "OK", 3000))
            {
                // 解析 socket state: +QISTATE: 0,"TCP",...,state
                state_ptr = strstr((char*)BC260_rx_buffer, "+QISTATE: 0,");
                if(state_ptr)
                {
                    // 找到第5个逗号后的状态值
                    // 格式: +QISTATE: 0,"TCP","ip",port,local_port,state,...
                    uint8_t comma_cnt = 0;
                    char* p_state = state_ptr;
                    while(*p_state && comma_cnt < 5)
                    {
                        if(*p_state == ',') comma_cnt++;
                        p_state++;
                    }
                    if(*p_state)
                    {
                        state_val = (uint8_t)(*p_state - '0');
                        if(state_val == 2)
                        {
                            connected = 1;
                            printf("[BC260] TCP connection established (state=2).\r\n");
                            break;
                        }
                        else if(state_val == 1)
                        {
                            printf("[BC260] TCP connecting (state=1)...\r\n");
                        }
                    }
                }
            }
        }
        Delay_Ms(10);
        start_time += 10;
    }

    if(!connected)
    {
        printf("[BC260] TCP connection establishment timeout!\r\n");
        BC260_SendCommand("AT+QICLOSE=0", "OK", 5000);
        BC260_RecoveryReset();
        return 0;
    }

    // 4. 发送 AT+QISEND 命令，等待 > 提示符（带重试）
    // 注意：BC260在收到AT+QISEND后，会先返回OK，然后再输出">"提示符
    // 所以需要特殊处理，继续等待">"而不是只看OK响应
    prompt_received = 0;
    for(send_attempt = 0; send_attempt < 2; send_attempt++)
    {
        // 清空接收缓冲区
        memset((void*)BC260_rx_buffer, 0, sizeof(BC260_rx_buffer));
        BC260_rx_index = 0;
        
        sprintf(cmd, "AT+QISEND=0,%d", http_len);
        printf("[BC260] Sending: %s\r\n", cmd);
        
        // 发送命令
        p = cmd;
        while(*p)
        {
            while(USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
            USART_SendData(USART2, *p);
            p++;
        }
        while(USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
        USART_SendData(USART2, '\r');
        while(USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
        USART_SendData(USART2, '\n');
        
        // 等待OK响应，然后继续等待">"提示符（总共最多8秒）
        start_time = 0;
        uint8_t ok_received = 0;
        while(start_time < 8000)
        {
            if(USART_GetFlagStatus(USART2, USART_FLAG_RXNE) != RESET)
            {
                uint8_t data = USART_ReceiveData(USART2);
                if(BC260_rx_index < sizeof(BC260_rx_buffer) - 1)
                {
                    BC260_rx_buffer[BC260_rx_index++] = data;
                    BC260_rx_buffer[BC260_rx_index] = '\0';
                    
                    // 检查是否收到OK
                    if(!ok_received && strstr((char*)BC260_rx_buffer, "OK"))
                    {
                        ok_received = 1;
                        printf("[BC260] OK received, waiting for '>'...\r\n");
                    }
                    
                    // 检查是否收到">"提示符
                    if(strstr((char*)BC260_rx_buffer, ">"))
                    {
                        prompt_received = 1;
                        printf("[BC260] Send prompt '>' received (attempt %d/2).\r\n", send_attempt + 1);
                        break;
                    }
                    
                    // 检查错误
                    if(strstr((char*)BC260_rx_buffer, "ERROR"))
                    {
                        printf("[BC260] ERROR received.\r\n");
                        break;
                    }
                }
            }
            Delay_Ms(1);
            start_time++;
        }
        
        if(prompt_received)
        {
            break;
        }
        
        printf("[BC260] No send prompt '>' received (attempt %d/2).\r\n", send_attempt + 1);
        printf("[BC260] Response buffer: %s\r\n", (char*)BC260_rx_buffer);

        if(send_attempt == 0)
        {
            // 第一次失败，关闭并重新创建连接（参考Python脚本的策略）
            printf("[BC260] Retrying with new TCP connection...\r\n");
            BC260_SendCommand("AT+QICLOSE=0", "OK", 5000);
            Delay_Ms(2000);
            // 消费残留URC
            start_time = 0;
            while(start_time < 2000)
            {
                if(USART_GetFlagStatus(USART2, USART_FLAG_RXNE) != RESET)
                {
                    USART_ReceiveData(USART2);
                }
                Delay_Ms(1);
                start_time++;
            }
            
            // 发送空AT确认模块清醒
            BC260_SendCommand("AT", "OK", 3000);
            Delay_Ms(500);

            sprintf(cmd, "AT+QIOPEN=0,0,\"TCP\",\"%d.%d.%d.%d\",%d",
                    proxy_ip[0], proxy_ip[1], proxy_ip[2], proxy_ip[3], BC260_PROXY_PORT);
            if(!BC260_SendCommand(cmd, "OK", 15000))
            {
                printf("[BC260] Failed to reopen TCP connection!\r\n");
                BC260_RecoveryReset();
                return 0;
            }

            // 等待连接建立
            connected = 0;
            start_time = 0;
            while(start_time < 30000)
            {
                if((start_time % 500) == 0 && start_time > 0)
                {
                    if(BC260_SendCommand("AT+QISTATE?", "OK", 3000))
                    {
                        state_ptr = strstr((char*)BC260_rx_buffer, "+QISTATE: 0,");
                        if(state_ptr)
                        {
                            uint8_t comma_cnt = 0;
                            char* p_state = state_ptr;
                            while(*p_state && comma_cnt < 5)
                            {
                                if(*p_state == ',') comma_cnt++;
                                p_state++;
                            }
                            if(*p_state)
                            {
                                state_val = (uint8_t)(*p_state - '0');
                                if(state_val == 2)
                                {
                                    connected = 1;
                                    printf("[BC260] TCP reconnected (state=2).\r\n");
                                    break;
                                }
                            }
                        }
                    }
                }
                Delay_Ms(10);
                start_time += 10;
            }

            if(!connected)
            {
                printf("[BC260] TCP reconnection timeout!\r\n");
                BC260_SendCommand("AT+QICLOSE=0", "OK", 5000);
                BC260_RecoveryReset();
                return 0;
            }
            
            // 重新连接后给模块一点时间恢复
            Delay_Ms(2000);
        }
    }

    if(!prompt_received)
    {
        printf("[BC260] Failed to get send prompt after 2 attempts!\r\n");
        BC260_SendCommand("AT+QICLOSE=0", "OK", 5000);
        BC260_RecoveryReset();
        return 0;
    }

    // 5. 发送实际HTTP数据（字节流，不带换行）
    printf("[BC260] Sending HTTP data (%d bytes)...\r\n", http_len);
    p = http_request;
    while(*p)
    {
        while(USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
        USART_SendData(USART2, *p);
        p++;
    }

    // 6. 等待 SEND OK
    memset((void*)BC260_rx_buffer, 0, sizeof(BC260_rx_buffer));
    BC260_rx_index = 0;
    start_time = 0;
    while(start_time < 15000)
    {
        if(USART_GetFlagStatus(USART2, USART_FLAG_RXNE) != RESET)
        {
            uint8_t data = USART_ReceiveData(USART2);
            if(BC260_rx_index < sizeof(BC260_rx_buffer) - 1)
            {
                BC260_rx_buffer[BC260_rx_index++] = data;
                BC260_rx_buffer[BC260_rx_index] = '\0';

                if(strstr((char*)BC260_rx_buffer, "SEND OK"))
                {
                    printf("[BC260] SEND OK received.\r\n");
                    break;
                }
                if(strstr((char*)BC260_rx_buffer, "SEND FAIL"))
                {
                    printf("[BC260] SEND FAIL received!\r\n");
                    BC260_SendCommand("AT+QICLOSE=0", "OK", 2000);
                    BC260_RecoveryReset();
                    return 0;
                }
            }
        }
        Delay_Ms(1);
        start_time++;
    }

    if(!strstr((char*)BC260_rx_buffer, "SEND OK"))
    {
        printf("[BC260] Timeout waiting for SEND OK!\r\n");
        BC260_SendCommand("AT+QICLOSE=0", "OK", 2000);
        BC260_RecoveryReset();
        return 0;
    }

    // 7. 等待并接收HTTP响应
    // 服务器需要时间处理请求并返回响应，先延时等待
    printf("[BC260] Waiting for server response...\r\n");
    Delay_Ms(5000);  // 增加等待时间到5秒
    if(BC260_ReceiveHTTPResponse(response_buf, sizeof(response_buf), 15000))  // 增加接收超时到15秒
    {
        printf("[BC260] Alert sent successfully!\r\n");
        BC260_SendCommand("AT+QICLOSE=0", "OK", 2000);
        return 1;
    }
    else
    {
        printf("[BC260] No valid response received, but data may have been sent.\r\n");
        BC260_SendCommand("AT+QICLOSE=0", "OK", 2000);
        // 即使没收到响应，也可能已经发送成功了（和Python测试脚本情况类似）
        return 1;
    }
}
#endif /* ENABLE_BC260 */

#if ENABLE_ETHERNET
/*********************************************************************
 * 以太网模块相关函数
 *********************************************************************/

/*********************************************************************
 * @fn      TIM2_Init
 *
 * @brief   初始化TIM2定时器（用于WCHNET协议栈定时）
 *          配置为10ms周期的向上计数模式
 *
 * @return  none
 *
 * @note    当前使用轮询模式而非中断模式
 *          在主循环中手动调用WCHNET_TimeIsr()更新网络定时
 *          这样可避免中断与其他外设的冲突
 */
void TIM2_Init(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    // 配置定时器参数：周期=10ms (WCHNETTIMERPERIOD=10)
    TIM_TimeBaseStructure.TIM_Period = SystemCoreClock / 1000000;
    TIM_TimeBaseStructure.TIM_Prescaler = WCHNETTIMERPERIOD * 1000 - 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);

    // 注意：TIM2中断已禁用，使用主循环轮询方式调用WCHNET_TimeIsr()
    // 如需启用中断模式，取消以下注释：
    // TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    // NVIC_EnableIRQ(TIM2_IRQn);

    TIM_Cmd(TIM2, ENABLE);
}

/*********************************************************************
 * @fn      ETH_PrintMacAddr
 *
 * @brief   打印MAC地址到调试串口
 *
 * @return  none
 */
void ETH_PrintMacAddr(void)
{
    printf("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
           MACAddr[0], MACAddr[1], MACAddr[2],
           MACAddr[3], MACAddr[4], MACAddr[5]);
}

/*********************************************************************
 * @fn      ETH_HandleSocketInt
 *
 * @brief   处理Socket中断
 *
 * @param   socketid - socket id
 * @param   intstat - 中断状态
 *
 * @return  none
 */
void ETH_HandleSocketInt(uint8_t socketid, uint8_t intstat)
{
    if(intstat & SINT_STAT_RECV)    // 接收到数据
    {
        uint32_t len = WCHNET_SocketRecvLen(socketid, NULL);
        printf("[ETH] Socket %d received %lu bytes\r\n", socketid, (unsigned long)len);

        // 读取并丢弃接收的数据（HTTP响应）
        if(len > 0)
        {
            WCHNET_SocketRecv(socketid, NULL, &len);
        }
    }
    if(intstat & SINT_STAT_CONNECT)  // 连接成功
    {
        WCHNET_ModifyRecvBuf(socketid, (uint32_t)eth_recv_buf[socketid], RECE_BUF_LEN);
        eth_socket_connected = 1;
        printf("[ETH] Socket %d connected successfully\r\n", socketid);
    }
    if(intstat & SINT_STAT_DISCONNECT)  // 断开连接
    {
        eth_socket_connected = 0;
        printf("[ETH] Socket %d disconnected\r\n", socketid);
    }
    if(intstat & SINT_STAT_TIM_OUT)  // 超时
    {
        eth_socket_connected = 0;
        printf("[ETH] Socket %d timeout\r\n", socketid);
    }
}

/*********************************************************************
 * @fn      ETH_HandleGlobalInt
 *
 * @brief   处理全局以太网中断
 *
 * @return  none
 */
void ETH_HandleGlobalInt(void)
{
    uint8_t intstat;
    uint8_t i;
    uint8_t socketint;

    intstat = WCHNET_GetGlobalInt();

    if(intstat & GINT_STAT_UNREACH)
    {
        printf("[ETH] Unreachable interrupt\r\n");
    }
    if(intstat & GINT_STAT_IP_CONFLI)
    {
        printf("[ETH] IP conflict detected!\r\n");
    }
    if(intstat & GINT_STAT_PHY_CHANGE)
    {
        uint8_t phy_stat = WCHNET_GetPHYStatus();
        if(phy_stat & PHY_Linked_Status)
        {
            eth_link_status = 1;
            printf("[ETH] PHY Link Up\r\n");
        }
        else
        {
            eth_link_status = 0;
            printf("[ETH] PHY Link Down\r\n");
        }
    }
    if(intstat & GINT_STAT_SOCKET)
    {
        for(i = 0; i < WCHNET_MAX_SOCKET_NUM; i++)
        {
            socketint = WCHNET_GetSocketInt(i);
            if(socketint)
            {
                ETH_HandleSocketInt(i, socketint);
            }
        }
    }
}

/*********************************************************************
 * @fn      ETH_SendWebhookAlert
 *
 * @brief   通过以太网发送企业微信Webhook警报消息
 *
 * @param   alert_msg - 警报消息内容（UTF-8编码）
 *
 * @return  1-发送成功 0-发送失败
 *
 * @note    CH32V208内置10M以太网不支持SSL/TLS
 *          需要通过HTTP代理服务器转发到企业微信HTTPS接口
 *          代理服务器地址在config.h中配置(HTTP_PROXY_IP/PORT)
 */
uint8_t ETH_SendWebhookAlert(char* alert_msg)
{
    char json_body[256];
    char http_request[768];
    uint16_t json_len;
    uint32_t http_len;
    uint8_t ret;
    SOCK_INF TmpSocketInf;
    uint8_t socket_id = 0;
    uint32_t timeout;

    if(!eth_initialized)
    {
        printf("[ETH] Ethernet not initialized, skipping webhook alert.\r\n");
        return 0;
    }

    if(!eth_link_status)
    {
        printf("[ETH] Ethernet link down, cannot send alert.\r\n");
        return 0;
    }

    printf("[ETH] Sending webhook alert: %s\r\n", alert_msg);

    // 准备JSON body
    sprintf(json_body,
        "{\"msgtype\":\"text\",\"text\":{\"content\":\"%s\"}}",
        alert_msg);
    json_len = strlen(json_body);

    // 准备HTTP POST请求
    // 注意：企业微信需要HTTPS，此处为HTTP请求格式
    // 实际部署需要配置HTTP转HTTPS代理服务器
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

    printf("[ETH] HTTP request length: %lu bytes\r\n", (unsigned long)http_len);

    // 使用配置的HTTP代理服务器来转发HTTPS请求
    // CH32V208内置10M以太网不支持SSL，需要代理服务器转发到企业微信HTTPS接口
    uint8_t proxy_ip[4] = HTTP_PROXY_IP;
    printf("[ETH] Connecting to HTTP proxy: %d.%d.%d.%d:%d\r\n",
           proxy_ip[0], proxy_ip[1], proxy_ip[2], proxy_ip[3], HTTP_PROXY_PORT);

    // 创建TCP Socket连接到代理服务器
    memset(&TmpSocketInf, 0, sizeof(SOCK_INF));
    TmpSocketInf.IPAddr[0] = proxy_ip[0];
    TmpSocketInf.IPAddr[1] = proxy_ip[1];
    TmpSocketInf.IPAddr[2] = proxy_ip[2];
    TmpSocketInf.IPAddr[3] = proxy_ip[3];
    TmpSocketInf.DesPort = HTTP_PROXY_PORT;  // 代理服务器端口
    TmpSocketInf.SourPort = 10000 + (eth_src_port_counter++ % 1000);  // 动态源端口
    TmpSocketInf.ProtoType = PROTO_TYPE_TCP;
    TmpSocketInf.RecvBufLen = RECE_BUF_LEN;

    ret = WCHNET_SocketCreat(&socket_id, &TmpSocketInf);
    if(ret != WCHNET_ERR_SUCCESS)
    {
        printf("[ETH] Failed to create socket: 0x%02X\r\n", ret);
        return 0;
    }

    printf("[ETH] Socket %d created, connecting...\r\n", socket_id);

    eth_socket_connected = 0;
    ret = WCHNET_SocketConnect(socket_id);
    if(ret != WCHNET_ERR_SUCCESS)
    {
        printf("[ETH] Failed to initiate connection: 0x%02X\r\n", ret);
        WCHNET_SocketClose(socket_id, TCP_CLOSE_ABANDON);
        return 0;
    }

    // 等待连接建立
    timeout = 0;
    while(!eth_socket_connected && timeout < 5000)
    {
        // 轮询以太网硬件（因为ETH中断已禁用）
        WCHNET_ETHIsr();
        WCHNET_MainTask();
        if(WCHNET_QueryGlobalInt())
        {
            ETH_HandleGlobalInt();
        }
        // 更新网络定时器
        WCHNET_TimeIsr(WCHNETTIMERPERIOD);
        Soft_Delay_Ms(10);
        timeout += 10;
    }

    if(!eth_socket_connected)
    {
        printf("[ETH] Connection timeout!\r\n");
        WCHNET_SocketClose(socket_id, TCP_CLOSE_ABANDON);
        return 0;
    }

    // 发送HTTP请求
    ret = WCHNET_SocketSend(socket_id, (uint8_t*)http_request, &http_len);
    if(ret != WCHNET_ERR_SUCCESS)
    {
        printf("[ETH] Failed to send HTTP request: 0x%02X\r\n", ret);
        WCHNET_SocketClose(socket_id, TCP_CLOSE_NORMAL);
        return 0;
    }

    printf("[ETH] HTTP request sent, %lu bytes\r\n", (unsigned long)http_len);

    // 等待响应（使用循环轮询）
    timeout = 0;
    while(timeout < 2000)
    {
        WCHNET_ETHIsr();
        WCHNET_MainTask();
        if(WCHNET_QueryGlobalInt())
        {
            ETH_HandleGlobalInt();
        }
        WCHNET_TimeIsr(WCHNETTIMERPERIOD);
        Soft_Delay_Ms(10);
        timeout += 10;
    }

    // 关闭连接
    WCHNET_SocketClose(socket_id, TCP_CLOSE_NORMAL);

    printf("[ETH] Webhook alert sent!\r\n");
    return 1;
}

/*********************************************************************
 * @fn      ETH_ModuleInit
 *
 * @brief   初始化以太网模块
 *
 * @return  1-成功 0-失败
 */
uint8_t ETH_ModuleInit(void)
{
    uint8_t ret;
    uint8_t i;

    printf("\r\n=== Initializing Ethernet Module ===\r\n");

    // 检查系统时钟
    if((SystemCoreClock != 60000000) && (SystemCoreClock != 120000000))
    {
        printf("[ETH] Warning: Ethernet requires 60MHz or 120MHz clock!\r\n");
        printf("[ETH] Current clock: %lu Hz\r\n", (unsigned long)SystemCoreClock);
    }

    // 获取MAC地址
    WCHNET_GetMacAddr(MACAddr);
    ETH_PrintMacAddr();

    // 打印网络库版本
    printf("[ETH] WCHNET Library Version: 0x%02X\r\n", WCHNET_GetVer());

    // 打印网络配置
    printf("[ETH] IP Address:  %d.%d.%d.%d\r\n",
           IPAddr[0], IPAddr[1], IPAddr[2], IPAddr[3]);
    printf("[ETH] Gateway:     %d.%d.%d.%d\r\n",
           GWIPAddr[0], GWIPAddr[1], GWIPAddr[2], GWIPAddr[3]);
    printf("[ETH] Subnet Mask: %d.%d.%d.%d\r\n",
           IPMask[0], IPMask[1], IPMask[2], IPMask[3]);
    printf("[ETH] DNS Server:  %d.%d.%d.%d\r\n",
           DNSAddr[0], DNSAddr[1], DNSAddr[2], DNSAddr[3]);

    // 初始化以太网库（必须在定时器之前）
    printf("[ETH] Initializing library...\r\n");
    ret = ETH_LibInit(IPAddr, GWIPAddr, IPMask, MACAddr);
    if(ret != WCHNET_ERR_SUCCESS)
    {
        printf("[ETH] Library init failed: 0x%02X\r\n", ret);
        return 0;
    }
    printf("[ETH] Library initialized successfully\r\n");

    // 初始化定时器（用于协议栈定时）- 必须在库初始化之后
    printf("[ETH] Initializing TIM2 for WCHNET...\r\n");
    TIM2_Init();
    printf("[ETH] TIM2 initialized\r\n");

    // 初始化socket数组
    memset(eth_socket, 0xff, WCHNET_MAX_SOCKET_NUM);

    // 等待PHY链接
    printf("[ETH] Waiting for PHY link...");

    for(i = 0; i < 50; i++)  // 最多等待5秒
    {
        WCHNET_MainTask();
        if(WCHNET_QueryGlobalInt())
        {
            ETH_HandleGlobalInt();
        }

        if(eth_link_status)
        {
            printf("OK!\r\n");
            printf("[ETH] PHY link established!\r\n");
            break;
        }

        // 简单延时约100ms
        {
            volatile uint32_t d;
            for(d = 0; d < 1200000; d++);
        }
    }

    if(i >= 50)
    {
        printf("[ETH] Warning: PHY link not established. Check cable connection.\r\n");
    }

    eth_initialized = 1;
    printf("[ETH] Ethernet module initialized successfully!\r\n");
    printf("=== Ethernet Initialization Complete ===\r\n\r\n");

    return 1;
}
#endif /* ENABLE_ETHERNET */

/*********************************************************************
 * @fn      USART_Report_Status
 *
 * @brief   通过串口报告当前水位状态和电压值
 *          同时通过ESP8266 WiFi、BC260 NB-IoT或以太网发送警报
 *
 * @return  none
 */
void USART_Report_Status(void)
{
    static uint8_t webhook_sent = 0;  // 记录是否已发送过webhook
    static uint8_t nbiot_sent = 0;    // 记录是否已通过NB-IoT发送
    static uint8_t eth_sent = 0;      // 记录是否已通过以太网发送

    if(water_status != last_water_status)
    {
        // 状态发生变化才输出
        if(water_status == 0)
        {
            printf("Water Status: No Water Detected, Voltage: %umV\r\n", voltage_mv);
            
            // 如果之前是有水状态，现在变为无水状态，则发送恢复通知
            if(last_water_status == 1 && (webhook_sent || nbiot_sent || eth_sent))
            {
                char alert_msg[100];
                sprintf(alert_msg, "【解除警报】浸水情况已解除，当前电压：%dmV", voltage_mv);

#if ENABLE_ESP8266
                // 通过ESP8266 WiFi发送
                if(ESP8266_SendWebhookAlert(alert_msg)) {
                    webhook_sent = 0;  // 重置标记
                }
#endif

#if ENABLE_BC260
                // 通过BC260 NB-IoT发送
                if(BC260_SendAlert(alert_msg)) {
                    nbiot_sent = 0;  // 重置标记
                }
#endif

#if ENABLE_ETHERNET
                // 通过以太网发送
                if(ETH_SendWebhookAlert(alert_msg)) {
                    eth_sent = 0;  // 重置标记
                }
#endif
            }
        }
        else
        {
            printf("Water Status: Water Detected, Voltage: %dmV\r\n", voltage_mv);
            
            // 发送警报通知
            char alert_msg[100];
            sprintf(alert_msg, "【严重警报】检测到浸水情况！当前电压：%dmV", voltage_mv);

#if ENABLE_ESP8266
            // 通过ESP8266 WiFi发送
            if(ESP8266_SendWebhookAlert(alert_msg)) {
                webhook_sent = 1;  // 标记已发送
            }
#endif

#if ENABLE_BC260
            // 通过BC260 NB-IoT发送
            if(BC260_SendAlert(alert_msg)) {
                nbiot_sent = 1;  // 标记已发送
            }
#endif

#if ENABLE_ETHERNET
            // 通过以太网发送
            if(ETH_SendWebhookAlert(alert_msg)) {
                eth_sent = 1;  // 标记已发送
            }
#endif
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

    // 【重要】必须先初始化串口，再调用printf，否则程序会死锁
    // 初始化串口打印功能（根据配置选择SDI或USART）
#if (SDI_PRINT == SDI_PR_TRUE)
    SDI_Printf_Enable();  // 启用SDI打印
#else
    USART_Printf_Init(115200);  // 初始化串口并支持printf输出
#endif

#if DEBUG_MODE
    // 现在可以安全使用printf了
    printf("\r\n===== EARLY BOOT DEBUG =====\r\n");
    printf("[BOOT] Reset occurred, entering main()\r\n");
    printf("[BOOT] Chip: CH32V208WBU6\r\n");
    printf("[BOOT] Compile: %s %s\r\n", __DATE__, __TIME__);
    printf("[DEBUG] NVIC configured, SystemCoreClock updated\r\n");
    printf("[DEBUG] SystemCoreClock = %ld Hz\r\n", SystemCoreClock);
    printf("[DEBUG] HSE_VALUE = %ld Hz\r\n", HSE_VALUE);
    printf("[DEBUG] Delay_Init() completed\r\n");
#endif
    
    // 调试信息：打印芯片信息和系统时钟
    printf("\r\n=== 系统启动信息 ===\r\n");
    printf("芯片型号: CH32V208WBU6\r\n");
    printf("系统时钟: %ld Hz\r\n", SystemCoreClock);
    printf("HSE_VALUE: %ld Hz\r\n", HSE_VALUE);
    printf("编译时间: %s %s\r\n", __DATE__, __TIME__);
#ifdef CH32V20x_D8W
    printf("芯片系列: CH32V208 (D8W)\r\n");
#elif defined(CH32V20x_D8)
    printf("芯片系列: CH32V203 (D8)\r\n");
#elif defined(CH32V20x_D6)
    printf("芯片系列: CH32V203 (D6)\r\n");
#endif
    printf("=====================\r\n\r\n");
    
    // 串口已在main()开头初始化，这里只打印确认信息
#if (SDI_PRINT == SDI_PR_TRUE)
    printf("System Clock: %ld Hz\r\n", SystemCoreClock);
    printf("SDI Print Enabled\r\n");
#else
    printf("System Clock: %ld Hz\r\n", SystemCoreClock);
    printf("USART Print Enabled\r\n");
#endif

    // 统一使能所有外设时钟（包括DMA）
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_USART1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2 | RCC_APB1Periph_USART3, ENABLE);  // USART2用于BC260，USART3用于ESP8266
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);  // 使能DMA1时钟

#if DEBUG_MODE
    printf("[DEBUG] Peripheral clocks enabled (including DMA1)\r\n");
#endif
    
    // 初始化GPIO、ADC、USART1、USART2和USART3
    GPIO_Init_For_Sensor();
    ADC_Function_Init();
    USART1_Init();
#if ENABLE_BC260
    USART2_Init();  // BC260 NB-IoT通信
#endif
#if ENABLE_ESP8266
    USART3_Init();  // ESP8266 WiFi通信
#endif

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

#if ENABLE_ESP8266
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
#else
    // ESP8266未启用
    printf("\r\n=== Water Immersion Detection System Started ===\r\n");
    printf("Water detection threshold: %dmV\r\n", WATER_THRESHOLD_MV);
    printf("ESP8266 WiFi module: DISABLED\r\n");
#endif

#if ENABLE_BC260
    // 初始化BC260 NB-IoT模块
    printf("\r\nAttempting to initialize BC260 NB-IoT module...\r\n");
    if(BC260_Init())
    {
        printf("BC260 NB-IoT module initialized successfully!\r\n");
    }
    else
    {
        printf("Failed to initialize BC260 NB-IoT module! Continuing without NB-IoT.\r\n");
    }
#else
    printf("BC260 NB-IoT module: DISABLED\r\n");
#endif

#if ENABLE_ETHERNET
    // 初始化以太网模块
    printf("\r\nAttempting to initialize Ethernet module...\r\n");
    if(ETH_ModuleInit())
    {
        printf("Ethernet module initialized successfully!\r\n");

        // 等待PHY链接建立后再发送启动通知
        if(!eth_link_status)
        {
            printf("Waiting for PHY link before sending startup notification...\r\n");
            uint32_t link_wait = 0;
            while(!eth_link_status && link_wait < 3000)  // 最多等待3秒
            {
                WCHNET_ETHIsr();
                WCHNET_MainTask();
                if(WCHNET_QueryGlobalInt())
                {
                    ETH_HandleGlobalInt();
                }
                Soft_Delay_Ms(100);
                link_wait += 100;
            }
        }

        if(eth_link_status)
        {
            // 发送系统启动通知
            printf("Sending system startup notification via Ethernet...\r\n");
            char startup_msg[256];
            sprintf(startup_msg,
                    "【系统启动】\n"
                    "浸水检测系统已成功启动\n"
                    "IP: %d.%d.%d.%d\n"
                    "检测阈值: %dmV\n"
                    "状态: 正常运行",
                    IPAddr[0], IPAddr[1], IPAddr[2], IPAddr[3],
                    WATER_THRESHOLD_MV);

            if(ETH_SendWebhookAlert(startup_msg))
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
            printf("PHY link not available, skipping startup notification.\r\n");
        }
    }
    else
    {
        printf("Failed to initialize Ethernet module! Continuing without Ethernet.\r\n");
    }
#else
    printf("Ethernet module: DISABLED\r\n");
#endif

    // 主循环
    printf("\r\nEntering main loop...\r\n");
#if ENABLE_ESP8266
    printf("\r\n=== Interactive Test Mode ===\r\n");
    printf("Type 2 characters to send as command to ESP8266\r\n");
    printf("For example: type 'AT' to send 'AT\\r\\n'\r\n");
    printf("Or type 'XX' + Enter manually in your terminal if it supports\r\n");
    printf("Waiting for input...\r\n\r\n");

    char test_buffer[128];
    uint8_t test_index = 0;
#endif

    while(1)
    {
#if ENABLE_ESP8266
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
#endif /* ENABLE_ESP8266 */

#if ENABLE_ETHERNET
        /* ========== 以太网任务处理（轮询模式） ========== */

        // 处理网络协议栈主任务
        WCHNET_MainTask();
        if(WCHNET_QueryGlobalInt())
        {
            ETH_HandleGlobalInt();
        }

        // 更新网络定时器（替代TIM2中断）
        // time_counter每10次循环重置，不会溢出
        {
            static uint32_t time_counter = 0;
            if(++time_counter >= 10)  // 每10ms调用一次
            {
                WCHNET_TimeIsr(WCHNETTIMERPERIOD);
                time_counter = 0;
            }
        }

        // 轮询以太网硬件状态（替代ETH中断）
        WCHNET_ETHIsr();
#endif

        /* ========== 传感器检测任务（每秒一次） ========== */
        static uint32_t last_sensor_check = 0;
        static uint32_t global_timer = 0;
        static uint32_t heartbeat_counter = 0;

        // global_timer自增，使用无符号整数的自然溢出特性
        // 差值比较 (global_timer - last_sensor_check) 在溢出时仍然正确
        global_timer++;

        if(global_timer - last_sensor_check >= 1000)
        {
            // heartbeat_counter溢出后自动归零，不影响功能
            heartbeat_counter++;

            printf("[LOOP %lu] Checking sensor...\r\n", (unsigned long)heartbeat_counter);

            Sensor_Status_Check();   // 读取ADC电压值
            LED_Control();           // 更新LED状态
            USART_Report_Status();   // 处理状态变化和Webhook推送
            last_sensor_check = global_timer;
            
#if DEBUG_MODE
            printf("[HEARTBEAT %lu] LED3(PB0): %s, LED4(PB1): %s\r\n",
                   (unsigned long)heartbeat_counter,
                   (GPIOB->OUTDR & GPIO_Pin_0) ? "OFF" : "ON",
                   (GPIOB->OUTDR & GPIO_Pin_1) ? "OFF" : "ON");
#endif
        }

        // 主循环延时（约1ms）
        Soft_Delay_Ms(1);
    }
}

