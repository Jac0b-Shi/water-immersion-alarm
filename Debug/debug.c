/********************************** (C) COPYRIGHT  *******************************
 * File Name          : debug.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2021/06/06
 * Description        : This file contains all the functions prototypes for UART
 *                      Printf , Delay functions.
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/
#include "debug.h"
#include "ch32v20x_tim.h"
#include <string.h>

static volatile uint8_t  p_us = 0;
static volatile uint16_t p_ms = 0;

#define DEBUG_DATA0_ADDRESS  ((volatile uint32_t*)0xE0000380)
#define DEBUG_DATA1_ADDRESS  ((volatile uint32_t*)0xE0000384)

/*********************************************************************
 * @fn      Timer3_Delay_Us
 *
 * @brief   利用TIM3实现微秒级延时
 *
 * @param   n - 需要延时的微秒数
 *
 * @return  None
 */
static void Timer3_Delay_Us(uint32_t n)
{
    // 处理超过16位计数器最大值的情况，使用循环而非递归避免栈溢出
    while (n > 65535) {
        // 设置计数周期为最大值
        TIM_SetAutoreload(TIM3, 65535);
        
        // 清除更新标志位
        TIM_ClearFlag(TIM3, TIM_FLAG_Update);
        
        // 启动定时器
        TIM_Cmd(TIM3, ENABLE);
        
        // 等待计数完成
        while(!TIM_GetFlagStatus(TIM3, TIM_FLAG_Update));
        
        // 关闭定时器
        TIM_Cmd(TIM3, DISABLE);
        
        n -= 65535;
    }
    
    // 处理剩余的延时
    if (n > 0) {
        // 设置实际需要的计数周期
        TIM_SetAutoreload(TIM3, n);
        
        // 清除更新标志位
        TIM_ClearFlag(TIM3, TIM_FLAG_Update);
        
        // 启动定时器
        TIM_Cmd(TIM3, ENABLE);
        
        // 等待计数完成
        while(!TIM_GetFlagStatus(TIM3, TIM_FLAG_Update));
        
        // 关闭定时器
        TIM_Cmd(TIM3, DISABLE);
    }
}

/*********************************************************************
 * @fn      Timer3_Delay_Ms
 *
 * @brief   利用TIM3实现毫秒级延时
 *
 * @param   n - 需要延时的毫秒数
 *
 * @return  None
 */
static void Timer3_Delay_Ms(uint32_t n)
{
    TIM_SetAutoreload(TIM3, p_ms * n);
    TIM_ClearFlag(TIM3, TIM_FLAG_Update);
    TIM_Cmd(TIM3, ENABLE);
    while(!TIM_GetFlagStatus(TIM3, TIM_FLAG_Update));
    TIM_Cmd(TIM3, DISABLE);
}

/*********************************************************************
 * @fn      Delay_Init
 *
 * @brief   初始化延时功能，计算每微秒和每毫秒对应的时钟周期数
 *
 * @return  none
 */
void Delay_Init(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    
    // 使用向上取整公式确保p_us至少为1，避免时钟频率过低时延时失效
    p_us = (SystemCoreClock + 3999999) / 4000000;
    p_ms = p_us * 1000;
    
    // 确保p_us至少为1，防止在低时钟频率下计算结果为0导致延时失效
    if (p_us == 0) p_us = 1;
    
    // 初始化TIM3时钟和基本参数，避免在每次延时调用时重复初始化
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    
    TIM_TimeBaseStructure.TIM_Prescaler = SystemCoreClock / 1000000 - 1; // 设置为1MHz计数频率
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    // 先用最大值初始化，实际使用时会根据需要调整
    TIM_TimeBaseStructure.TIM_Period = 65535;  
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);
    
    // 默认关闭定时器，延时函数中再控制启停
    TIM_Cmd(TIM3, DISABLE);
}

/*********************************************************************
 * @fn      Delay_Us
 *
 * @brief   微秒级延时函数
 *
 * @param   n - 需要延时的微秒数
 *
 * @return  None
 */
void Delay_Us(uint32_t n)
{
    // 使用TIM3实现精确延时，避免直接访问SysTick寄存器可能引起的硬件异常
    Timer3_Delay_Us(n);
}

/*********************************************************************
 * @fn      Delay_Ms
 *
 * @brief   毫秒级延时函数
 *
 * @param   n - 需要延时的毫秒数
 *
 * @return  None
 */
void Delay_Ms(uint32_t n)
{
    // 使用TIM3实现精确延时，避免直接访问SysTick寄存器可能引起的硬件异常
    Timer3_Delay_Ms(n);
}

/*********************************************************************
 * @fn      USART_Printf_Init
 *
 * @brief   初始化USART外设，支持printf输出功能
 *
 * @param   baudrate - USART通信波特率
 *
 * @return  None
 */
void USART_Printf_Init(uint32_t baudrate)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;

#if(DEBUG == DEBUG_UART1)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

#elif(DEBUG == DEBUG_UART2)
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

#elif(DEBUG == DEBUG_UART3)
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

#endif

    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx;

#if(DEBUG == DEBUG_UART1)
    USART_Init(USART1, &USART_InitStructure);
    USART_Cmd(USART1, ENABLE);

#elif(DEBUG == DEBUG_UART2)
    USART_Init(USART2, &USART_InitStructure);
    USART_Cmd(USART2, ENABLE);

#elif(DEBUG == DEBUG_UART3)
    USART_Init(USART3, &USART_InitStructure);
    USART_Cmd(USART3, ENABLE);

#endif
}

/*********************************************************************
 * @fn      SDI_Printf_Enable
 *
 * @brief   初始化SDI打印功能
 *
 * @param   None
 *
 * @return  None
 */
void SDI_Printf_Enable(void)
{
    *(DEBUG_DATA0_ADDRESS) = 0;
    Delay_Init();
    Delay_Ms(1);
}

/*********************************************************************
 * @fn      _write
 *
 * @brief   支持printf函数的实际写入实现
 *
 * @param   fd   - 文件描述符（未使用）
 *          buf  - 待发送的数据缓冲区
 *          size - 数据长度
 *
 * @return  实际写入的数据长度，出错时返回-1
 */
__attribute__((used))
int _write(int fd, char *buf, int size)
{
    int i = 0;
    
    // 避免未使用参数警告
    (void)fd;

#if (SDI_PRINT == SDI_PR_TRUE)
    int writeSize = size;
    uint32_t timeout = 10000;  // 将超时计数器移至循环外部

    do
    {
        // 等待数据寄存器为空，添加超时保护防止死锁
        while ((*(DEBUG_DATA0_ADDRESS) != 0u) && timeout--)
        {
            // 添加小延迟以避免总线冲突
            Delay_Us(1);
        }
        // 超时检查，防止硬件异常导致死锁
        if (timeout == 0) return -1; // 超时退出

        if(writeSize > 7)
        {
            // 发送8字节数据（前1字节长度，后7字节数据）
            // 使用临时缓冲区确保不发生越界访问
            uint8_t temp[7] = {0};
            int bytesToCopy = (writeSize > 7) ? 7 : writeSize;
            memcpy(temp, buf + i, (size - i) > bytesToCopy ? bytesToCopy : (size - i));
            
            if(writeSize >= 7) {
                *(DEBUG_DATA1_ADDRESS) = (temp[3]) | ((uint32_t)temp[4]<<8) | ((uint32_t)temp[5]<<16) | ((uint32_t)temp[6]<<24);
                *(DEBUG_DATA0_ADDRESS) = (7u) | (temp[0]<<8) | (temp[1]<<16) | (temp[2]<<24);
            }

            i += 7;
            writeSize -= 7;
        }
        else
        {
            // 发送剩余数据，确保正确处理各种长度
            uint32_t data0_temp = writeSize;
            uint32_t data1_temp = 0;
            
            // 使用临时缓冲区避免越界读取
            uint8_t temp[7] = {0};
            memcpy(temp, buf + i, writeSize);
            
            // 根据实际剩余字节数逐字节构造data0和data1，避免强制类型转换导致越界读取
            for (int k = 0; k < 3 && k < writeSize; k++) {
                data0_temp |= (uint32_t)temp[k] << (8 + k * 8);
            }
            
            for (int k = 0; k < 4 && (3 + k) < writeSize; k++) {
                data1_temp |= (uint32_t)temp[3 + k] << (k * 8);
            }
            
            *(DEBUG_DATA1_ADDRESS) = data1_temp;
            *(DEBUG_DATA0_ADDRESS) = data0_temp;
            writeSize = 0;
        }

    } while (writeSize > 0);


#else
    for(i = 0; i < size; i++){
#if(DEBUG == DEBUG_UART1)
        while(USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
        USART_SendData(USART1, *buf++);
#elif(DEBUG == DEBUG_UART2)
        while(USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
        USART_SendData(USART2, *buf++);
#elif(DEBUG == DEBUG_UART3)
        while(USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET);
        USART_SendData(USART3, *buf++);
#endif
    }
#endif
    return size;
}

/*********************************************************************
 * @fn      _sbrk
 *
 * @brief   调整数据段的空间位置（用于内存管理）
 *
 * @param   incr - 增量大小
 *
 * @return  新的堆顶指针，失败时返回(void*)(-1)
 */
__attribute__((used))
void *_sbrk(ptrdiff_t incr)
{
    extern char _end[];
    extern char _heap_end[];
    static char *curbrk = NULL;  // 修改为NULL初始化

    // 首次调用时初始化curbrk
    if (curbrk == NULL) {
        curbrk = _end;
    }

    if (incr < 0 || (curbrk + incr < _end) || (curbrk + incr > _heap_end))
        return (void*)(-1);  // 修复指针运算警告

    curbrk += incr;
    return curbrk - incr;
}