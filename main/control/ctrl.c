#include "ctrl.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "vars.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief 通用 GPIO 初始化函数
 * @param gpio_num 引脚号
 * @param mode 预设的常用模式
 */
esp_err_t gpio_quick_init(gpio_num_t gpio_num, gpio_util_mode_t mode)
{
    gpio_config_t io_conf = {0};
    io_conf.pin_bit_mask = (1ULL << gpio_num);

    switch (mode)
    {
    case GPIO_MODE_LED:
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = 0;
        io_conf.pull_down_en = 0;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        break;
    case GPIO_MODE_RELAY:
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = 0;
        io_conf.pull_down_en = 1; // 下拉确保默认关闭
        break;
    case GPIO_MODE_KEY_INT:
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pull_up_en = 0;
        io_conf.pull_down_en = 1;
        io_conf.intr_type = GPIO_INTR_POSEDGE;
        break;
    case GPIO_MODE_INPUT_FLOAT:
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pull_up_en = 0;
        io_conf.pull_down_en = 0;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        break;
    }
    return gpio_config(&io_conf);
}

/**
 * @brief 翻转 GPIO 电平 (用于闪烁灯)
 */
void gpio_toggle(gpio_num_t gpio_num)
{
    static bool state[GPIO_NUM_MAX] = {0};
    state[gpio_num] = !state[gpio_num];
    gpio_set_level(gpio_num, state[gpio_num]);
}

/**
 * @brief 简单的软件去抖读取
 * @return 稳定的电平状态
 */
int gpio_get_level_stable(gpio_num_t gpio_num)
{
    int last_level = gpio_get_level(gpio_num);
    os_delay_us(20000); // 20ms 消抖
    if (gpio_get_level(gpio_num) == last_level)
    {
        return last_level;
    }
    return -1; // 不稳定状态
}

int gpio_set_level_stable(gpio_num_t gpio_num, int level)
{
    gpio_set_level(gpio_num, level);
    os_delay_us(20000); // 20ms 消抖
    if (gpio_get_level(gpio_num) == level)
    {
        return level;
    }
    return -1; // 不稳定状态
}

void control_task_huiyishi(void* p)
{
    gpio_quick_init(12, GPIO_MODE_KEY_INT);

    ESP_LOGI("APP", "GPIO 系统初始化完成");

    while (1)
    {
        ESP_LOGI("APP", "%d",gpio_get_level_stable(12));

        // 2. 使用封装好的稳定读取
        if (gpio_get_level_stable(12) == 0)
        {
            ESP_LOGI("APP", "检测到按键按下!");

            huiyishi_data_type tmp;
            memcpy(&tmp, (const void*)&huiyishi_data, sizeof(tmp));

            tmp.baojing_status = 1;

            memcpy((void*)&huiyishi_data, &tmp, sizeof(tmp));

            while (gpio_get_level(12) == 0)
            {
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }
        }

        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}

void put_task_huiyishi(void* p)
{
    // 1. 一键初始化
    gpio_quick_init(15, GPIO_MODE_RELAY);

    ESP_LOGI("APP", "GPIO 系统初始化完成");

    while (1)
    {
        // 2. 使用封装好的稳定读取
        if (huiyishi_data.kaimen_status == 1)
        {
            gpio_set_level_stable(15, 1);
        }else
        {
            gpio_set_level_stable(15, 0);
        }

        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}


void control_task_xianchang(void* p)
{
    gpio_quick_init(12, GPIO_MODE_KEY_INT);

    ESP_LOGI("APP", "GPIO 系统初始化完成");

    while (1)
    {
        ESP_LOGI("APP", "%d",gpio_get_level_stable(12));

        // 2. 使用封装好的稳定读取
        if (gpio_get_level_stable(12) == 0)
        {
            ESP_LOGI("APP", "检测到按键按下!");

            huiyishi_data_type tmp;
            memcpy(&tmp, (const void*)&huiyishi_data, sizeof(tmp));

            tmp.baojing_status = 1;

            memcpy((void*)&huiyishi_data, &tmp, sizeof(tmp));

            while (gpio_get_level(12) == 0)
            {
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }
        }

        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}
