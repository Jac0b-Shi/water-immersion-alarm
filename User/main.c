/********************************** (C) COPYRIGHT *******************************
 * File Name          : main_test.c
 * Author             : Your Name
 * Version            : V1.0.0
 * Date               : 2025/11/28
 * Description        : LED test program for CH32V203
 *******************************************************************************/

#include "debug.h"
#include <inttypes.h>

/* Global define */
#define LED_TASK_INTERVAL_MS    500

/* Function prototypes */
void LED_Init(void);
void LED_Handle(void);

//LED共阳极接法，这里低电平点亮
#define     LED1_OFF()       GPIO_SetBits(GPIOB,GPIO_Pin_0)
#define     LED1_ON()        GPIO_ResetBits(GPIOB,GPIO_Pin_0)
#define     LED2_OFF()       GPIO_SetBits(GPIOB,GPIO_Pin_1)
#define     LED2_ON()        GPIO_ResetBits(GPIOB,GPIO_Pin_1)

/*********************************************************************
 * @fn      LED_Init
 *
 * @brief   Initializes LEDs on GPIOB pins 0 and 1
 *
 * @return  none
 */
void LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0|GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    LED1_OFF();
    LED2_OFF();
}

/*********************************************************************
 * @fn      LED_Handle
 *
 * @brief   Toggles LEDs in sequence
 *
 * @return  none
 */
void LED_Handle(void)
{
    LED1_ON();
    LED2_OFF();
    Delay_Ms(500);
    LED1_OFF();
    LED2_ON();
    Delay_Ms(500);
}

/*********************************************************************
 * @fn      main
 *
 * @brief   Main program
 *
 * @return  none
 */
int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);
    
    printf("LED Test Program Started\r\n");
    printf("SystemClk:%" PRIu32 "\r\n", SystemCoreClock);
    printf("ChipID:%08" PRIx32 "\r\n", DBGMCU_GetCHIPID());

    LED_Init();

    while(1)
    {
        LED_Handle();
        printf("LED Toggle\r\n");
    }
}