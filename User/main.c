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
 * PA0：模拟传感器输入（高电平表示有水，低电平表示无水）
 * PB0：控制LED1（共阳极，低电平点亮）- 无水时点亮
 * PB1：控制LED2（共阳极，低电平点亮）- 有水时点亮
 * PA9：USART1_TX（串口输出）
 * PA10：USART1_RX（串口输入）
 */

#include "debug.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"
#include "ch32v20x_usart.h"

/* 全局变量定义 */
uint8_t water_status = 0;  // 0表示无水，1表示有水
uint8_t last_water_status = 0;  // 上一次的水位状态

/*********************************************************************
 * @fn      GPIO_Init_For_Sensor
 *
 * @brief   初始化传感器相关的GPIO引脚
 *          PA0 - 传感器输入（浮空输入）
 *          PB0 - LED1控制（推挽输出）
 *          PB1 - LED2控制（推挽输出）
 *
 * @return  none
 */
void GPIO_Init_For_Sensor(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    // 使能GPIOA和GPIOB时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);

    // 配置PA0为浮空输入（传感器输入）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

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

    // 使能GPIOA和USART1时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

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
 * @fn      Sensor_Status_Check
 *
 * @brief   检查传感器状态（PA0电平）
 *
 * @return  none
 */
void Sensor_Status_Check(void)
{
    // 读取PA0电平状态
    // 高电平表示检测到水，低电平表示未检测到水
    if(GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == Bit_SET)
    {
        water_status = 1;  // 有水
    }
    else
    {
        water_status = 0;  // 无水
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
 * @brief   通过串口报告当前水位状态
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
            printf("Water Status: No Water Detected\r\n");
        }
        else
        {
            printf("Water Status: Water Detected\r\n");
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
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
    
    // 同时初始化SDI和USART打印
#if (SDI_PRINT == SDI_PR_TRUE)
    SDI_Printf_Enable();  // 启用SDI打印
    printf("System Clock: %d Hz\r\n", SystemCoreClock);
    printf("SDI Print Enabled\r\n");
#else
    USART_Printf_Init(115200);  // 初始化串口并支持printf输出
    printf("System Clock: %d Hz\r\n", SystemCoreClock);
    printf("USART Print Enabled\r\n");
#endif

    // 初始化GPIO和USART1
    GPIO_Init_For_Sensor();
    USART1_Init();
    
    // 输出启动信息
    printf("Water Immersion Detection System Started\r\n");

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