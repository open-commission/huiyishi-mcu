//
// Created by nebula on 2026/1/16.
//

#ifndef HUIYISHI_MCU_CTRL_H
#define HUIYISHI_MCU_CTRL_H
#include <esp_err.h>
#include <gpio.h>

/**
 * @brief GPIO 快速配置模式
 */
typedef enum
{
    GPIO_MODE_LED, // 常用 LED 模式（输出，无上下拉）
    GPIO_MODE_RELAY, // 继电器模式（输出，下拉防止启动抖动）
    GPIO_MODE_KEY_INT, // 按键中断模式（输入，上拉，下降沿触发）
    GPIO_MODE_INPUT_FLOAT, // 浮空输入（如外接有源传感器）
} gpio_util_mode_t;

esp_err_t gpio_quick_init(gpio_num_t gpio_num, gpio_util_mode_t mode);
int gpio_get_level_stable(gpio_num_t gpio_num);
void gpio_toggle(gpio_num_t gpio_num);

#endif //HUIYISHI_MCU_CTRL_H
