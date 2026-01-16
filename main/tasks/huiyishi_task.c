//
// Created by nebula on 2026/1/16.
//

#include "huiyishi_task.h"

#include <adc.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ctrl.h"

#include "532.h"
#include "dht11.h"
#include "esp_log.h"
#include "event.h"
#include "guangzhao.h"

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

void pn532_task(void* pvParameters)
{
    bool init_flag = init_PN532_I2C(4, 5, 16, 13, I2C_NUM_0);
    ESP_LOGI("app_main", "init_flag = %d", init_flag);

    SAMConfig();

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    uint32_t firmware_version = getPN532FirmwareVersion();
    ESP_LOGI("app_main", "firmware_version = %d", firmware_version);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    uint8_t uid[4];
    uint8_t uidLength = 0;

    // 初始化PN532 I2C等（假设已调用 init_PN532_I2C() 等）

    // 尝试读取卡
    bool success = readPassiveTargetID(0x00, uid, &uidLength, 1000); // 1秒超时

    if (success && uidLength > 0)
    {
        ESP_LOGI("app_main", "找到卡，UID长度=%d, UID=", uidLength);
        for (int i = 0; i < uidLength; i++)
        {
            ESP_LOGI("app_main", "%02X ", uid[i]);
        }
    }
    else
    {
        ESP_LOGI("app_main", "未检测到卡或读取失败\n");
    }
}

void guangzhao_task(void* p)
{
    // 1. 初始化 ADC
    adc_init_config();

    uint16_t adc_data;

    while (1)
    {
        // 读取 ADC 原始值 (范围: 0 - 1023)
        if (adc_read(&adc_data) == ESP_OK)
        {
            // 计算光照百分比 (假设 1023 是最亮，0 是最暗，实际取决于你的接线)
            float brightness = (adc_data / 1023.0) * 100.0;

            ESP_LOGI("LIGHT_SENSOR", "ADC 原始值: %d | 估计亮度: %.2f%%", adc_data, brightness);
        }
        else
        {
            ESP_LOGE("LIGHT_SENSOR", "ADC 读取失败");
        }

        // 每 500ms 读取一次
        vTaskDelay(500 / portTICK_PERIOD_MS);
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
