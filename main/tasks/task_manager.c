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
    // xTaskCreate(dht_task, "dht_task", 2048, NULL, 10, NULL);
    xTaskCreate(guangzhao_task, "guangzhao_task", 2048, NULL, 10, NULL);
    // xTaskCreate(put_task_huiyishi, "put_task", 2048, NULL, 10, NULL);
    // xTaskCreate(coap_task, "coap_task", 2048, NULL, 10, NULL);
}

void create_xianchang_tasks(void)
{
    // xTaskCreate(control_task_xianchang, "control_task", 2048, NULL, 10, NULL);
    // xTaskCreate(jw01_task, "jw01_task", 2048, NULL, 10, NULL);
    // xTaskCreate(dht_task, "dht_task", 2048, NULL, 10, NULL);
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
    /* ================= UART1：唯一输出 ================= */
    uart_config_t uart1_cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    uart_param_config(UART_NUM_1, &uart1_cfg);
    uart_driver_install(UART_NUM_1, 0, 0, 0, NULL, 0);

    /* ================= UART0：传感器输入 ================= */
    uart_config_t uart0_cfg = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    uart_param_config(UART_NUM_0, &uart0_cfg);
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);

    /* ================= 接收缓冲 & 状态 ================= */
    uint8_t rx_buf[64];
    uint8_t frame_buf[6]; // 调整为 6 字节
    uint8_t frame_len = 0;

    while (1)
    {
        int len = uart_read_bytes(
            UART_NUM_0,
            rx_buf,
            sizeof(rx_buf),
            20 / portTICK_PERIOD_MS
        );

        if (len > 0)
        {
            /* 1. 逐字节处理，寻找 0x2C 帧头 */
            for (int i = 0; i < len; i++)
            {
                frame_buf[frame_len++] = rx_buf[i];

                // 逻辑 A：如果第一个字节不是 0x2C，说明没对齐，丢弃
                if (frame_buf[0] != 0x2C)
                {
                    frame_len = 0;
                    continue;
                }

                // 逻辑 B：凑齐 6 字节开始解析
                if (frame_len == 6)
                {
                    uint16_t sum = 0;
                    for (int k = 0; k < 5; k++)
                        sum += frame_buf[k];

                    uint8_t checksum = (uint8_t)sum;

                    if (checksum == frame_buf[5])
                    {
                        /* ===== 校验通过 ===== */
                        // 改为读取真正变化的位：frame_buf[1] 和 [2]
                        uint16_t raw = (frame_buf[1] << 8) | frame_buf[2];
                        float value = raw / 1000.0f; // 如果该传感器单位是 mg/m3 且有三位小数

                        char msg[100];
                        int n = snprintf(
                            msg, sizeof(msg),
                            "[OK] DataHex:%02X%02X Value:%.3f | Full:%02X %02X %02X %02X %02X %02X\r\n",
                            frame_buf[1], frame_buf[2], value,
                            frame_buf[0], frame_buf[1], frame_buf[2],
                            frame_buf[3], frame_buf[4], frame_buf[5]
                        );
                        uart_write_bytes(UART_NUM_1, msg, n);

                        frame_len = 0;
                    }
                    else
                    {
                        /* ===== 校验失败 ===== */
                        // 只有在 frame_buf[0] 是 2C 的情况下才报校验错
                        char msg[64];
                        int n = snprintf(
                            msg, sizeof(msg),
                            "[ERR] Checksum Mismatch: calc=%02X recv=%02X\r\n",
                            checksum, frame_buf[5]
                        );
                        uart_write_bytes(UART_NUM_1, msg, n);

                        // 校验失败说明这一组数据不对，滑窗找下一个可能的 2C
                        memmove(frame_buf, frame_buf + 1, 5);
                        frame_len = 5;
                    }
                }
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
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

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << GPIO_NUM_14,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    while (1)
    {
        DHT11(); //读取温湿度
        ESP_LOGI("wenshidu", "T=%d,H=%d %%.", wendu, shidu);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
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

            tmp.guangzhao_var = adc_data;

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
