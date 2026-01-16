//
// Created by nebula on 2026/1/16.
//

#include "xianchang_task.h"

#include <adc.h>

#include "532.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ctrl.h"

#include <uart.h>

#include "dht11.h"
#include "event.h"
#include "guangzhao.h"
#include "pm25.h"
#include "uco2.h"

void event_task(void* p)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << 0),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE, // 任何电平变化都会触发（上升沿+下降沿）
    };

    gpio_config(&io_conf);

    // 2. 创建队列 (容量10)
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    // 3. 创建处理任务
    xTaskCreate(gpio_event_task, "gpio_event_task", 2048, NULL, 10, NULL);

    // 4. 安装 ISR 服务并添加处理程序
    gpio_install_isr_service(0);
    gpio_isr_handler_add(0, gpio_isr_handler, 0);

    ESP_LOGI("event", "GPIO 中断回调任务已启动...");
}

void control_task(void* p)
{
    // 1. 一键初始化
    gpio_quick_init(2, GPIO_MODE_LED);
    gpio_quick_init(0, GPIO_MODE_KEY_INT);

    ESP_LOGI("APP", "GPIO 系统初始化完成");

    while (1)
    {
        // 2. 使用封装好的稳定读取
        if (gpio_get_level_stable(2) == 0)
        {
            ESP_LOGI("APP", "检测到按键按下!");

            // 3. 翻转电平
            gpio_toggle(0);

            // 等待按键释放，防止重复触发
            while (gpio_get_level(2) == 0)
            {
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void jw01_task(void* pvParameters)
{
    // 1. 初始化串口
    jw01_uart_init();

    uint8_t data[1024];

    while (1)
    {
        // 读取串口数据
        int len = uart_read_bytes(UART_NUM_0, data, 1024, 100 / portTICK_RATE_MS);

        if (len >= 4)
        {
            for (int i = 0; i <= len - 4; i++)
            {
                // 查找起始符 0xA5
                if (data[i] == 0xA5)
                {
                    uint8_t h_byte = data[i + 1];
                    uint8_t l_byte = data[i + 2];
                    uint8_t checksum = data[i + 3];

                    // 简单校验：前三字节之和取低8位 (部分JW01协议如此，请查阅您的说明书)
                    if (((data[i] + h_byte + l_byte) & 0xFF) == checksum)
                    {
                        int pm25_val = (h_byte << 8) | l_byte;
                        ESP_LOGI("JW01", "检测到 PM2.5 浓度: %d ug/m3", pm25_val);
                    }
                }
            }
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}


void dht_task(void* p)
{
    esp_log_level_set(TAG, ESP_LOG_INFO);

    uint8_t dht_data[5];

    while (1)
    {
        if (read_dht_raw(dht_data) == ESP_OK)
        {
            ESP_LOGI("dht11", "湿度: %d.%d %% 湿度, 温度: %d.%d 度",
                     dht_data[0], dht_data[1], dht_data[2], dht_data[3]);
        }
        else
        {
            ESP_LOGE("DHT11", "无法从 DHT11 读取数据，请检查接线");
        }
        // DHT11 读取频率不要超过 1Hz (1秒一次)
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief 平均值滤波任务
 */
void pm25_task(void* pvParameters)
{
    pm25_sensor_init();
    while (1)
    {
        uint32_t sum = 0;
        for (int i = 0; i < PM25_READ_TIMES; i++)
        {
            sum += get_pm25_single_data();
            vTaskDelay(10 / portTICK_PERIOD_MS); // 采样间隔
        }

        uint16_t avg_pm25 = sum / PM25_READ_TIMES;
        ESP_LOGI("PM25", "当前粉尘浓度: %d ug/m3", avg_pm25);

        vTaskDelay(1000 / portTICK_PERIOD_MS); // 每秒更新一次
    }
}
