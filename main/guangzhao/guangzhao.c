//
// Created by nebula on 2026/1/16.
//

#include "guangzhao.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "driver/adc.h"

/**
 * @brief ADC 初始化配置
 */
void adc_init_config()
{
    // ADC 配置结构体
    adc_config_t adc_config;

    // 对于 ESP8266，ADC 只有 1 个通道（TOUT）
    // 下面配置采样模式和时钟预分频
    adc_config.mode = ADC_READ_TOUT_MODE;
    adc_config.clk_div = 8; // 推荐 8

    // 初始化 ADC
    ESP_ERROR_CHECK(adc_init(&adc_config));
}

