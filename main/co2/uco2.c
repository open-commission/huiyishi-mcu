//
// Created by nebula on 2026/1/16.
//

#include "uco2.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "driver/uart.h"
#include "esp_log.h"


// 使用 UART0，读取时建议避开系统日志冲突
#define JW01_UART_PORT      UART_NUM_0
#define BUF_SIZE            (1024)

/**
 * @brief 初始化 UART
 */
void jw01_uart_init()
{
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    // 安装驱动
    uart_driver_install(JW01_UART_PORT, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(JW01_UART_PORT, &uart_config);
}