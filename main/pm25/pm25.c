//
// Created by nebula on 2026/1/16.
//

#include "pm25.h"

#include <stdio.h>
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_log.h"

/**
 * @brief 延时函数封装
 */
static void inline delay_us(uint32_t us)
{
    os_delay_us(us);
}

/**
 * @brief 初始化传感器需要的 GPIO 和 ADC
 */
void pm25_sensor_init()
{
    // 1. 初始化控制 LED 的 GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PM25_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(PM25_LED_PIN, 1); // 默认关闭 (根据电路，通常高电平关或开，请自行测试)

    // 2. 初始化 ADC
    adc_config_t adc_config = {
        .mode = ADC_READ_TOUT_MODE,
        .clk_div = 8
    };
    adc_init(&adc_config);
}

/**
 * @brief 读取一次粉尘浓度
 * 遵循 GP2Y1014AU10 的脉冲驱动时序
 */
uint16_t get_pm25_single_data()
{
    uint16_t adc_raw = 0;
    float voltage = 0;
    float dust_density = 0;

    // 1. 开启红外 LED (拉低引脚, 假设你的电路是低电平触发 LED)
    // 如果你的电路是高电平点亮，请改为 gpio_set_level(..., 1)
    gpio_set_level(PM25_LED_PIN, 0);

    // 2. 关键时序：等待 280us 后采样
    delay_us(280);

    // 3. 读取 ADC 原始值 (0-1023)
    adc_read(&adc_raw);

    // 4. 保持 LED 开启状态直到总长 320us (280 + 40)
    delay_us(40);

    // 5. 关闭红外 LED
    gpio_set_level(PM25_LED_PIN, 1);

    // 6. 周期剩余时间延时 (10ms 周期，还剩约 9680us)
    delay_us(9680);

    // --- 换算逻辑 ---
    // ESP8266 ADC 1.0V 对应 1024 原始值。
    // 如果你用了 NodeMCU 分压电路 (3.3V 对应 1024)，系数如下：
    voltage = (float)adc_raw * (3.3 / 1024.0);

    // 如果你的硬件像 STM32 代码里那样加了 2 倍压电路，此处 * 2
    // voltage = voltage * 2.0;

    // 根据你提供的公式: dustVal = (0.17 * Voltage - 0.1) * 1000
    dust_density = (0.17 * voltage - 0.1) * 1000.0;

    // 限位处理
    if (dust_density < 0) dust_density = 0;
    if (dust_density > 500) dust_density = 500;

    return (uint16_t)dust_density;
}
