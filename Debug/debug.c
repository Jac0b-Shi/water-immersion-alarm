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

static uint8_t  p_us = 0;
static uint16_t p_ms = 0;

#define DEBUG_DATA0_ADDRESS  ((volatile uint32_t*)0xE0000380)
#define DEBUG_DATA1_ADDRESS  ((volatile uint32_t*)0xE0000384)

/*********************************************************************
 * @fn      Delay_Init
 *
 * @brief   Initializes Delay Funcation.
 *
 * @return  none
 */
void Delay_Init(void)
{
    // 修复SysTick延时计算错误，使用向上取整确保最小延时精度
    p_us = (SystemCoreClock + 3999999) / 4000000; // 向上取整到1μs分辨率
    p_ms = p_us * 1000;
}

/*********************************************************************
 * @fn      Delay_Us
 *
 * @brief   Microsecond Delay Time.
 *
 * @param   n - Microsecond number.
 *
 * @return  None
 */
void Delay_Us(uint32_t n)
{
    uint32_t i;

    SysTick->SR &= ~(1 << 0);
    i = (uint32_t)n * p_us;

    SysTick->CMP = i;
    SysTick->CTLR |= (1 << 4);
    SysTick->CTLR |= (1 << 5) | (1 << 0);

    while((SysTick->SR & (1 << 0)) != (1 << 0));
    SysTick->CTLR &= ~(1 << 0);
}

/*********************************************************************
 * @fn      Delay_Ms
 *
 * @brief   Millisecond Delay Time.
 *
 * @param   n - Millisecond number.
 *
 * @return  None
 */
void Delay_Ms(uint32_t n)
{
    uint32_t i;

    SysTick->SR &= ~(1 << 0);
    i = (uint32_t)n * p_ms;

    SysTick->CMP = i;
    SysTick->CTLR |= (1 << 4);
    SysTick->CTLR |= (1 << 5) | (1 << 0);

    while((SysTick->SR & (1 << 0)) != (1 << 0));
    SysTick->CTLR &= ~(1 << 0);
}

/*********************************************************************
 * @fn      USART_Printf_Init
 *
 * @brief   Initializes the USARTx peripheral.
 *
 * @param   baudrate - USART communication baud rate.
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
 * @brief   Initializes the SDI printf Function.
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
 * @brief   Support Printf Function
 *
 * @param   *buf - UART send Data.
 *          size - Data length
 *
 * @return  size: Data length
 */
__attribute__((used))
int _write(int fd, char *buf, int size)
{
    int i = 0;
    
    // 避免未使用参数警告
    (void)fd;

#if (SDI_PRINT == SDI_PR_TRUE)
    int writeSize = size;

    do
    {
        // 等待数据寄存器为空
        while( (*(DEBUG_DATA0_ADDRESS) != 0u))
        {
            // 添加小延迟以避免总线冲突
            Delay_Us(1);
        }

        if(writeSize > 7)
        {
            // 发送8字节数据（前1字节长度，后7字节数据）
            // 确保访问不会越界
            if (i + 6 < size) {
                *(DEBUG_DATA1_ADDRESS) = (*(buf+i+3)) | ((uint32_t)(*(buf+i+4))<<8) | ((uint32_t)(*(buf+i+5))<<16) | ((uint32_t)(*(buf+i+6))<<24);
                *(DEBUG_DATA0_ADDRESS) = (7u) | (*(buf+i)<<8) | (*(buf+i+1)<<16) | (*(buf+i+2)<<24);
            }

            i += 7;
            writeSize -= 7;
        }
        else
        {
            // 发送剩余数据，确保正确处理各种长度
            uint32_t data0_temp = writeSize;
            uint32_t data1_temp = 0;
            
            // 使用临时字节数组避免指针别名导致的越界，确保数据构造安全
            uint8_t tmp[8] = {0};
            
            // 根据实际剩余字节数动态构造发送数据
            for (int k = 0; k < 3 && i + k < size; k++) {
                tmp[1 + k] = buf[i + k];  // tmp[1]~tmp[3]存储buf[i]~buf[i+2]
            }
            
            for (int k = 0; k < 4 && i + 3 + k < size; k++) {
                tmp[4 + k] = buf[i + 3 + k];  // tmp[4]~tmp[7]存储buf[i+3]~buf[i+6]
            }
            
            data0_temp = *(uint32_t*)tmp;
            data1_temp = *(uint32_t*)(tmp + 4);
            
            // 应用长度掩码
            data0_temp = (data0_temp & 0xFFFFFF00) | (writeSize & 0xFF);
            
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
 * @brief   Change the spatial position of data segment.
 *
 * @return  size: Data length
 */
__attribute__((used))
void *_sbrk(ptrdiff_t incr)
{
    extern char _end[];
    extern char _heap_end[];
    static char *curbrk = _end;

    if (incr < 0 || (curbrk + incr < _end) || (curbrk + incr > _heap_end))
        return (void*)(-1);  // 修复指针运算警告

    curbrk += incr;
    return curbrk - incr;
}
