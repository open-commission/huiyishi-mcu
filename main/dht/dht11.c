//
// Created by nebula on 2026/1/16.
//

#include "dht11.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "DHT11_EXAMPLE";

// 定义连接 DHT11 的 GPIO 引脚（例如 GPIO4 / D2）
#define DHT_GPIO_PIN 4

/**
 * @brief 精确微秒级延时
 * ESP8266 RTOS SDK 建议使用 os_delay_us
 */
static void inline delay_us(uint32_t us) {
    os_delay_us(us);
}

/**
 * @brief 从 DHT11 读取原始数据
 */
static esp_err_t read_dht_raw(uint8_t data[5]) {
    uint8_t last_state = 1;
    uint16_t counter = 0;
    uint8_t j = 0, i = 0;

    data[0] = data[1] = data[2] = data[3] = data[4] = 0;

    // 1. 发送开始信号
    gpio_set_direction(DHT_GPIO_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT_GPIO_PIN, 0);
    vTaskDelay(20 / portTICK_PERIOD_MS); // 至少拉低 18ms
    gpio_set_level(DHT_GPIO_PIN, 1);
    delay_us(30); // 拉高 20-40us

    // 2. 切换为输入模式等待响应
    gpio_set_direction(DHT_GPIO_PIN, GPIO_MODE_INPUT);

    // 3. 读取 40 bits 数据
    for (i = 0; i < 85; i++) {
        counter = 0;
        while (gpio_get_level(DHT_GPIO_PIN) == last_state) {
            counter++;
            delay_us(1);
            if (counter == 1000) break;
        }
        last_state = gpio_get_level(DHT_GPIO_PIN);
        if (counter == 1000) break;

        // 忽略前 3 个状态转换（响应信号）
        if ((i >= 4) && (i % 2 == 0)) {
            data[j / 8] <<= 1;
            if (counter > 30) { // 高电平持续时间判断：26-28us 为 0，70us 为 1
                data[j / 8] |= 1;
            }
            j++;
        }
    }

    // 4. 校验数据
    if ((j >= 40) && (data[4] == ((data[0] + data[1] + data[2] + data[3]) & 0xFF))) {
        return ESP_OK;
    }
    return ESP_FAIL;
}
