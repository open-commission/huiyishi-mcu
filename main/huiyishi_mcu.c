// #include "task_manager.h"
//
// void app_main()
// {
// #ifdef XIANCHANG_MOD
//     // 现场模式
//     create_xianchang_tasks();
// #else
//     // 会议室模式
//     create_huiyishi_tasks();
// #endif
// }


//测试专用

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"

// --- 硬件与串口配置 ---
#define UART_NUM            UART_NUM_0
#define BUF_SIZE            (1024)

// --- 数据结构定义 ---
typedef struct
{
    char name[16];
    char trigger_key;
    float current_val;
    float base_val;
    float target_val;
    float step;
    bool is_active;
} sensor_attr_t;

typedef struct
{
    char name[16];
    char trigger_key;
    int gpio_pin;
    bool state;
} status_attr_t;

// --- 可配置参数区 ---
sensor_attr_t sensors[] = {
    // 名称        触发键   初始值   目标值   步长(每秒)
    {"Temp", '1', 25.5, 38.2, 0.4, false},
    {"Humi", '2', 50.0, 85.0, 1.2, false},
    {"Lux", '3', 150.0, 800.0, 15.0, false}
};

status_attr_t status_devs[] = {
    // 名称        触发键   GPIO引脚   初始状态
    {"Relay", '5', GPIO_NUM_2, false}
};

#define SENSOR_COUNT (sizeof(sensors)/sizeof(sensor_attr_t))
#define STATUS_COUNT (sizeof(status_devs)/sizeof(status_attr_t))

// --- 硬件初始化 ---
void init_hw()
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM, &uart_config);
    uart_driver_install(UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);

    for (int i = 0; i < STATUS_COUNT; i++)
    {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << status_devs[i].gpio_pin)
        };
        gpio_config(&io_conf);
        gpio_set_level(status_devs[i].gpio_pin, status_devs[i].state);
    }
}

// --- 核心逻辑任务 ---
void system_monitor_task(void* pvParameters)
{
    while (1)
    {
        // 1. 处理数值模拟（步进变化）
        for (int i = 0; i < SENSOR_COUNT; i++)
        {
            float goal = sensors[i].is_active ? sensors[i].target_val : sensors[i].base_val;

            if (sensors[i].current_val < goal)
            {
                sensors[i].current_val += sensors[i].step;
                if (sensors[i].current_val > goal) sensors[i].current_val = goal;
            }
            else if (sensors[i].current_val > goal)
            {
                sensors[i].current_val -= sensors[i].step;
                if (sensors[i].current_val < goal) sensors[i].current_val = goal;
            }
        }

        // 2. 统一串口输出（固定格式，每秒一次）
        // 格式: [Temp: 26.30] [Humi: 51.20] [Lux: 150.00] | [Relay: OFF]
        printf("\r>> "); // \r 使光标回到行首，实现原地刷新（取决于串口助手支持情况）
        for (int i = 0; i < SENSOR_COUNT; i++)
        {
            printf("%s: %.2f | ", sensors[i].name, sensors[i].current_val);
        }
        for (int i = 0; i < STATUS_COUNT; i++)
        {
            printf("%s: %s ", status_devs[i].name, status_devs[i].state ? "ON " : "OFF");
        }
        printf("    "); // 清除行尾多余字符
        fflush(stdout);
        if (UART_NUM == UART_NUM_0) printf("\n"); // 如果不支持\r刷新，则换行

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

// --- 静默串口监听 ---
void uart_silent_rx_task(void* pvParameters)
{
    uint8_t data;
    while (1)
    {
        // 阻塞式读取单个字节
        if (uart_read_bytes(UART_NUM, &data, 1, portMAX_DELAY) > 0)
        {
            // 匹配传感器切换
            for (int i = 0; i < SENSOR_COUNT; i++)
            {
                if (data == sensors[i].trigger_key)
                {
                    sensors[i].is_active = !sensors[i].is_active;
                }
            }
            // 匹配状态设备切换
            for (int i = 0; i < STATUS_COUNT; i++)
            {
                if (data == status_devs[i].trigger_key)
                {
                    status_devs[i].state = !status_devs[i].state;
                    gpio_set_level(status_devs[i].gpio_pin, status_devs[i].state);
                }
            }
        }
    }
}

void app_main()
{
    init_hw();

    // 监控任务优先级稍低
    xTaskCreate(system_monitor_task, "monitor", 2048, NULL, 5, NULL);
    // 串口接收任务优先级高，确保响应及时
    xTaskCreate(uart_silent_rx_task, "uart_rx", 2048, NULL, 10, NULL);
}
