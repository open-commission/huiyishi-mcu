//
// Created by nebula on 2026/1/16.
//

#include "task_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

// 包含各种任务函数声明
#include <adc.h>
#include <string.h>

#include "ctrl.h"
#include "532.h"
#include "dht11.h"
#include "guangzhao.h"
#include "pm25.h"
#include "uco2.h"
#include "vars.h"
#include "driver/uart.h"

void create_huiyishi_tasks(void)
{
    // xTaskCreate(control_task_huiyishi, "control_task", 2048, NULL, 10, NULL);
    // xTaskCreate(pn532_task, "pn532_task", 2048, NULL, 10, NULL);
    xTaskCreate(dht_task, "dht_task", 2048, NULL, 10, NULL);
    // xTaskCreate(guangzhao_task, "guangzhao_task", 2048, NULL, 10, NULL);
    xTaskCreate(put_task_huiyishi, "put_task", 2048, NULL, 10, NULL);
    // xTaskCreate(coap_task, "coap_task", 2048, NULL, 10, NULL);
}

void create_xianchang_tasks(void)
{
    xTaskCreate(control_task_xianchang, "control_task", 2048, NULL, 10, NULL);
    xTaskCreate(jw01_task, "jw01_task", 2048, NULL, 10, NULL);
    xTaskCreate(dht_task, "dht_task", 2048, NULL, 10, NULL);
    xTaskCreate(pm25_task, "pm25_task", 2048, NULL, 10, NULL);
}

// 以下是具体的任务实现，从各自模块移至此处

void pn532_task(void* pvParameters)
{
    bool init_flag = init_PN532_I2C(4, 5, 16, 13, I2C_NUM_0);
    ESP_LOGI("app_main", "init_flag = %d", init_flag);

    while (1)
    {
        SAMConfig();

        vTaskDelay(1000 / portTICK_PERIOD_MS);

        uint8_t uid[4];
        uint8_t uidLength = 0;

        // 尝试读取卡
        bool success = readPassiveTargetID(0x00, uid, &uidLength, 1000); // 1秒超时

        if (success && uidLength > 0)
        {
            ESP_LOGI("app_main", "找到卡，UID长度=%d, UID=", uidLength);
            /* UID 转 HEX 字符串 */
            huiyishi_data_type tmp;
            size_t pos = 0;
            size_t max_len = sizeof(tmp.rfid_card);
            for (int i = 0; i < uidLength; i++)
            {
                ESP_LOGI("app_main", "%02X ", uid[i]);
                if (pos + 2 >= max_len)
                    break;

                pos += snprintf(&tmp.rfid_card[pos],
                                max_len - pos,
                                "%02X",
                                uid[i]);
            }
            /* 确保字符串结束 */
            tmp.rfid_card[max_len - 1] = '\0';

            /* 一次性写回 volatile */
            memcpy((void*)&huiyishi_data, &tmp, sizeof(tmp));
        }
        else
        {
            ESP_LOGI("app_main", "未检测到卡或读取失败\n");
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
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
                        ESP_LOGI("JW01", "检测到 CO2 浓度: %d ppm", pm25_val);
                    }
                }
            }
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

static void huiyishi_update_temp_humi(float temperature, float humidity)
{
    huiyishi_data_type tmp;

    /* 先拷贝当前值，保留其他字段 */
    memcpy(&tmp, (const void*)&huiyishi_data, sizeof(tmp));

    tmp.wendu_var = temperature;
    tmp.shidu_var = humidity;

    /* 一次性写回 volatile */
    memcpy((void*)&huiyishi_data, &tmp, sizeof(tmp));
}


void dht_task(void* p)
{
    esp_log_level_set("DHT11_EXAMPLE", ESP_LOG_INFO);

    while (1)
    {
        DHT11(); //读取温湿度
        ESP_LOGI("wenshidu","T=%d,H=%d %%.", wendu, shidu);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


void guangzhao_task(void* p)
{
    // 1. 初始化 ADC
    adc_init_config();

    uint16_t adc_data;

    while (1)
    {
        if (adc_read(&adc_data) == ESP_OK)
        {
            /* ADC → 光照百分比 */
            float brightness = (adc_data / 1023.0f) * 100.0f;

            ESP_LOGI("LIGHT_SENSOR",
                     "ADC 原始值: %d | 估计亮度: %.2f%%",
                     adc_data, brightness);

            /* 直接更新 huiyishi_data（一次性写回） */
            huiyishi_data_type tmp;
            memcpy(&tmp, (const void*)&huiyishi_data, sizeof(tmp));

            tmp.guangzhao_var = brightness;

            memcpy((void*)&huiyishi_data, &tmp, sizeof(tmp));
        }
        else
        {
            ESP_LOGE("LIGHT_SENSOR", "ADC 读取失败");
        }

        /* 每 1 秒更新一次 */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

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
