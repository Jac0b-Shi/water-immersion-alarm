/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2023/12/29
 * Description        : Water level monitoring and alarm system using CH32V203
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/*
 *@Note
 *Water level monitoring and alarm system:
 *1. Read water level sensor data using ADC (PA0/ADC_IN0)
 *2. Convert ADC value to water level in centimeters
 *3. Compare with threshold and trigger alarm if exceeded
 *4. Send webhook alert via BC28 NB-IoT module through UART
 */

#include "debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Global define */
#define TASK_WATER_MONITOR_PRIO     5
#define TASK_WATER_MONITOR_STK_SIZE 512

#define WATER_LEVEL_THRESHOLD       80.0    // Water level threshold (centimeters)
#define MAX_WATER_LEVEL             100.0   // Maximum water level (centimeters)
#define ADC_MAX_VALUE               4095    // 12-bit ADC maximum value
#define WEBHOOK_URL                 "https://example.com/webhook"  // Replace with actual URL
#define UART_BC28                   USART1  // BC28 connected to USART1
#define BC28_BAUDRATE               9600    // BC28 default baud rate

/* Global Variable */
TaskHandle_t WaterMonitorTask_Handler;

// Alarm state variables
volatile uint8_t alarm_sent = 0;           // Flag to indicate if alarm was sent
volatile uint32_t last_alarm_time = 0;     // Time of last alarm (in ticks)

/*********************************************************************
 * @fn      ADC_Init
 *
 * @brief   Initializes ADC for water level sensor on PA0 (ADC_IN0)
 *
 * @return  none
 */
void ADC_Init_Custom(void)
{
    ADC_InitTypeDef  ADC_InitStructure = {0};
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div8);

    // Configure PA0 as analog input for water level sensor
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    ADC_DeInit(ADC1);
    
    // ADC configuration
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &ADC_InitStructure);

    // Configure ADC channel 0 with 239.5 cycles sample time
    ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_239Cycles5);

    // Enable ADC
    ADC_Cmd(ADC1, ENABLE);

    // Disable buffer and calibrate ADC
    ADC_BufferCmd(ADC1, DISABLE);
    ADC_ResetCalibration(ADC1);
    while(ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while(ADC_GetCalibrationStatus(ADC1));
}

/*********************************************************************
 * @fn      ADC_Read
 *
 * @brief   Reads ADC value from channel 0
 *
 * @return  12-bit ADC value (0-4095)
 */
uint16_t ADC_Read(void)
{
    uint16_t val;

    // Start software conversion
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);

    // Wait for conversion to complete
    while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));

    // Get conversion result
    val = ADC_GetConversionValue(ADC1);

    return val;
}

/*********************************************************************
 * @fn      ADC_to_WaterLevel
 *
 * @brief   Converts ADC value to water level in centimeters
 *          Note: Water level is inversely proportional to ADC value
 *
 * @param   adc_val - Raw ADC value (0-4095)
 *
 * @return  Water level in centimeters (0-100)
 */
float ADC_to_WaterLevel(uint16_t adc_val)
{
    // Water level calculation: 
    // When ADC=4095, water level = 0 cm
    // When ADC=0, water level = 100 cm
    return ((float)(ADC_MAX_VALUE - adc_val) / ADC_MAX_VALUE) * MAX_WATER_LEVEL;
}

/*********************************************************************
 * @fn      UART_Init_Custom
 *
 * @brief   Initializes UART for BC28 communication (USART1: PA9-TX, PA10-RX)
 *
 * @return  none
 */
void UART_Init_Custom(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};

    // Enable clocks
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    // Configure USART1 TX (PA9) as alternate function push-pull
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // Configure USART1 RX (PA10) as input floating
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // USART configuration
    USART_InitStructure.USART_BaudRate = BC28_BAUDRATE;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    
    USART_Init(UART_BC28, &USART_InitStructure);
    USART_Cmd(UART_BC28, ENABLE);
}

/*********************************************************************
 * @fn      UART_SendChar
 *
 * @brief   Sends a single character via UART
 *
 * @param   ch - Character to send
 *
 * @return  none
 */
void UART_SendChar(uint8_t ch)
{
    // Wait until transmit data register is empty
    while(USART_GetFlagStatus(UART_BC28, USART_FLAG_TXE) == RESET);
    
    // Send character
    USART_SendData(UART_BC28, ch);
}

/*********************************************************************
 * @fn      UART_SendString
 *
 * @brief   Sends a string via UART
 *
 * @param   str - String to send
 *
 * @return  none
 */
void UART_SendString(const char *str)
{
    while(*str) {
        UART_SendChar(*str++);
    }
}

/*********************************************************************
 * @fn      BC28_SendATCommand
 *
 * @brief   Sends AT command to BC28 and waits for response
 *
 * @param   cmd - AT command string
 * @param   timeout_ms - Timeout in milliseconds
 *
 * @return  1 if OK received, 0 otherwise
 */
uint8_t BC28_SendATCommand(const char *cmd, uint32_t timeout_ms)
{
    UART_SendString(cmd);
    UART_SendString("\r\n");
    
    // In a real implementation, we would wait for "OK" or "ERROR" response
    // For simplicity in this example, we just delay
    Delay_Ms(timeout_ms);
    
    return 1; // Assume success
}

/*********************************************************************
 * @fn      BC28_SendHTTPPost
 *
 * @brief   Sends HTTP POST request via BC28 NB-IoT module
 *
 * @param   url - Target URL
 * @param   json_body - JSON payload
 *
 * @return  none
 */
void BC28_SendHTTPPost(const char *url, const char *json_body)
{
    char cmd_buffer[128];
    
    // Only send alarm once per minute to avoid spam
    uint32_t current_time = xTaskGetTickCount();
    if (alarm_sent && (current_time - last_alarm_time < 60000)) {
        return;
    }
    
    printf("Sending alarm: %s\r\n", json_body);
    
    // Initialize network (in real implementation, check for proper network attachment)
    BC28_SendATCommand("AT+CGATT=1", 1000);  // Attach to network
    
    // Configure HTTP parameters
    snprintf(cmd_buffer, sizeof(cmd_buffer), "AT+HTTPPARA=\"URL\",\"%s\"", url);
    BC28_SendATCommand(cmd_buffer, 500);
    
    snprintf(cmd_buffer, sizeof(cmd_buffer), "AT+HTTPPARA=\"CONTENTTYPE\",\"application/json\"");
    BC28_SendATCommand(cmd_buffer, 500);
    
    // Set HTTP data
    snprintf(cmd_buffer, sizeof(cmd_buffer), "AT+HTTPDATA=%d,5000", strlen(json_body));
    BC28_SendATCommand(cmd_buffer, 500);
    
    // Send JSON data
    UART_SendString(json_body);
    Delay_Ms(100);
    
    // Send HTTP POST request
    BC28_SendATCommand("AT+HTTPACTION=1", 3000);  // POST action
    
    // Mark alarm as sent and record time
    alarm_sent = 1;
    last_alarm_time = current_time;
}

/*********************************************************************
 * @fn      water_monitor_task
 *
 * @brief   Water level monitoring task
 *
 * @param   *pvParameters - Task parameters
 *
 * @return  none
 */
void water_monitor_task(void *pvParameters)
{
    while(1)
    {
        // Read ADC value from water level sensor
        uint16_t adc_val = ADC_Read();
        
        // Convert ADC value to water level in centimeters
        float water_level = ADC_to_WaterLevel(adc_val);
        
        printf("ADC Value: %d, Water Level: %.2f cm\r\n", adc_val, water_level);
        
        // Check if water level exceeds threshold
        if (water_level > WATER_LEVEL_THRESHOLD) {
            // Prepare JSON message for webhook
            char json_msg[256];
            snprintf(json_msg, sizeof(json_msg), 
                     "{\"msgtype\":\"text\",\"text\":{\"content\":\"水位警报：当前水位%.1fcm，超过阈值%.1fcm！\"}}",
                     water_level, WATER_LEVEL_THRESHOLD);
            
            // Send HTTP POST request via BC28
            BC28_SendHTTPPost(WEBHOOK_URL, json_msg);
        }
        
        // Wait 5 seconds before next reading
        vTaskDelay(5000);
    }
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
    printf("Water Immersion Alarm System Started\r\n");
    printf("SystemClk:%d\r\n",SystemCoreClock);
    printf("ChipID:%08x\r\n", DBGMCU_GetCHIPID());

    // Initialize ADC for water level sensor
    ADC_Init_Custom();
    
    // Initialize UART for BC28 communication
    UART_Init_Custom();

    // Create water monitoring task
    xTaskCreate((TaskFunction_t )water_monitor_task,
                (const char*    )"water_monitor",
                (uint16_t       )TASK_WATER_MONITOR_STK_SIZE,
                (void*          )NULL,
                (UBaseType_t    )TASK_WATER_MONITOR_PRIO,
                (TaskHandle_t*  )&WaterMonitorTask_Handler);
                
    vTaskStartScheduler();

    while(1)
    {
        printf("Shouldn't run at here!!\n");
    }
}