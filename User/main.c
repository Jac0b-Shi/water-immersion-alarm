/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2023/12/29
 * Description        : 浸水传感器项目 - 基于CH32V203芯片
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * 注意: 本软件(修改或不修改)和二进制文件仅用于
 * 由南京沁恒微电子有限公司制造的微控制器产品
 *******************************************************************************/

/*
 *@Note
 * 浸水传感器项目主程序
 * PA0：模拟传感器输入（通过ADC读取电压值）
 * PB0：控制LED1（共阳极，低电平点亮）- 无水时点亮
 * PB1：控制LED2（共阳极，低电平点亮）- 有水时点亮
 * PA9：USART1_TX（串口输出）
 * PA10：USART1_RX（串口输入）
 */

#include "debug.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"
#include "ch32v20x_usart.h"
#include "ch32v20x_adc.h"

/* 全局变量定义 */
volatile uint8_t water_status = 0;  // 当前水位状态：0表示无水，1表示有水
volatile uint8_t last_water_status = 0;  // 上一次的水位状态
volatile uint16_t adc_value = 0;    // ADC转换结果
volatile uint16_t voltage_mv = 0;   // 电压值(单位:mV)

/* 阈值定义 - 1V = 1000mV */
#define WATER_THRESHOLD_MV    1000
#define NO_WATER_THRESHOLD_MV 500   // 无水状态的电压阈值
#define WATER_CONFIRM_COUNT 2       // 确认为有水状态需要的连续检测次数
#define NO_WATER_CONFIRM_COUNT 5    // 确认为无水状态需要的连续检测次数

/* 状态确认计数器 */
uint8_t water_counter = 0;          // 连续检测到高电压的次数
uint8_t no_water_counter = 0;       // 连续检测到低电压的次数

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
 *          PB0 - LED1控制（推挽输出）
 *          PB1 - LED2控制（推挽输出）
 *
 * @return  none
 */
void GPIO_Init_For_Sensor(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    // 配置PB0和PB1为推挽输出（LED控制）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // 初始化LED状态 - 默认无水状态（LED1亮，LED2灭）
    GPIO_SetBits(GPIOB, GPIO_Pin_1);  // 熄灭LED2（PB1）
    GPIO_ResetBits(GPIOB, GPIO_Pin_0); // 点亮LED1（PB0）
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
 * @fn      USART_Report_Status
 *
 * @brief   通过串口报告当前水位状态和电压值
 *
 * @return  none
 */
void USART_Report_Status(void)
{
    if(water_status != last_water_status)
    {
        // 状态发生变化才输出
        if(water_status == 0)
        {
            printf("Water Status: No Water Detected, Voltage: %dmV\r\n", voltage_mv);
        }
        else
        {
            printf("Water Status: Water Detected, Voltage: %dmV\r\n", voltage_mv);
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

    // 统一使能所有外设时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_USART1, ENABLE);
    
    // 初始化GPIO、ADC和USART1
    GPIO_Init_For_Sensor();
    ADC_Function_Init();
    USART1_Init();
    
    // 输出启动信息
    printf("Water Immersion Detection System Started\r\n");
    printf("Water detection threshold: %dmV\r\n", WATER_THRESHOLD_MV);

    // 主循环
    while(1)
    {
        // 检查传感器状态
        Sensor_Status_Check();
        
        // 控制LED显示
        LED_Control();
        
        // 通过串口报告状态
        USART_Report_Status();
        
        // 延时防止状态抖动
        Delay_Custom(500);
    }
}