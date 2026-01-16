//
// Created by nebula on 2026/1/16.
//

#include "event.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "GPIO_EVENT";

// 创建一个队列用于中断与任务间的通信
static xQueueHandle gpio_evt_queue = NULL;

/**
 * @brief 中断服务程序 (ISR)
 * 此函数运行在中断上下文，必须极其精简，且带有 IRAM_ATTR 属性
 */
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t) arg;
    // 将触发中断的引脚编号发往队列
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

/**
 * @brief 用户自定义的回调业务逻辑
 */
void my_gpio_callback(uint32_t pin, int level) {
    ESP_LOGI(TAG, "回调触发! 引脚: %d, 当前电平: %d", pin, level);
    // 在这里写你的业务逻辑，比如控制 LED、发送数据等
}

/**
 * @brief GPIO 事件处理任务
 */
static void gpio_event_task(void* arg) {
    uint32_t io_num;
    while (1) {
        // 等待队列信号（无限等待）
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            int level = gpio_get_level(io_num);
            // 执行回调
            my_gpio_callback(io_num, level);
        }
    }
}
