/********************************** (C) COPYRIGHT *******************************
 * File Name          : ch32v20x_it.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2023/12/29
 * Description        : Main Interrupt Service Routines.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#include "ch32v20x_it.h"
#include "config.h"

#if ENABLE_ETHERNET
#include "eth_driver.h"
#endif

void NMI_Handler(void) __attribute__((interrupt()));
void HardFault_Handler(void) __attribute__((interrupt()));

#if ENABLE_ETHERNET
void TIM2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void ETH_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
#endif

/*********************************************************************
 * @fn      NMI_Handler
 *
 * @brief   This function handles NMI exception.
 *
 * @return  none
 */
void NMI_Handler(void)
{
    while(1)
    {

    }
}

/*******************************************************************************
 * @fn        HardFault_Handler
 *
 * @brief     This function handles Hard Fault exception.
 *
 * @return    None
 */
void HardFault_Handler(void)
{
    // 输出硬件错误信息
    // 注意：这里不能使用printf，因为可能会导致更多问题
    // 直接通过LED闪烁指示错误

    // 快速闪烁PB0和PB1表示硬件错误
    volatile uint32_t i;
    while(1)
    {
        GPIOB->OUTDR ^= GPIO_Pin_0 | GPIO_Pin_1;
        for(i = 0; i < 500000; i++);
    }
}

#if ENABLE_ETHERNET
// 调试用：中断计数器
volatile uint32_t tim2_isr_count = 0;

/*********************************************************************
 * @fn      TIM2_IRQHandler
 *
 * @brief   定时器2中断处理函数（用于网络协议栈定时）
 *          按照WCH官方示例的方式实现
 *
 * @return  none
 */
void TIM2_IRQHandler(void)
{
    tim2_isr_count++;
    WCHNET_TimeIsr(WCHNETTIMERPERIOD);
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
}

/*********************************************************************
 * @fn      ETH_IRQHandler
 *
 * @brief   以太网中断处理函数
 *
 * @return  none
 */
void ETH_IRQHandler(void)
{
    WCHNET_ETHIsr();
}
#endif /* ENABLE_ETHERNET */


