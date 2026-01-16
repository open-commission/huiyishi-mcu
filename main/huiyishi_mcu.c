#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coap_domain.h"
#include "uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// #include "driver/adc.h"
// #include "driver/gpio.h"
// #include "esp_log.h"
//
// static const char* TAG = "pm2.5 sensor";
//
// // 定义LED控制GPIO引脚
// #define LED_GPIO_PIN 4  // 请根据实际连接修改GPIO引脚
//
// static void pm25_adc_task()
// {
//     uint16_t adc_data[1];
//
//     // 配置GPIO为输出模式
//     gpio_config_t io_conf;
//     io_conf.intr_type = GPIO_INTR_DISABLE;
//     io_conf.mode = GPIO_MODE_OUTPUT;
//     io_conf.pin_bit_mask = (1ULL << LED_GPIO_PIN);
//     io_conf.pull_down_en = 0;
//     io_conf.pull_up_en = 0;
//     gpio_config(&io_conf);
//
//     while (1)
//     {
//         // 设置GPIO为高电平
//         gpio_set_level(LED_GPIO_PIN, 1);
//
//         // 延时280微秒
//         ets_delay_us(280);
//
//         // 读取ADC值
//         if (ESP_OK == adc_read(&adc_data[0]))
//         {
//             printf("read: %d\n", adc_data[0]);
//         }
//
//         // 延时19毫秒
//         ets_delay_us(19);
//
//         // 设置GPIO为低电平
//         gpio_set_level(LED_GPIO_PIN, 0);
//
//         // 延时9600微秒
//         ets_delay_us(9600);
//
//         vTaskDelay(1000 / portTICK_PERIOD_MS);
//     }
// }
#include "task_manager.h"

void app_main()
{
#ifdef XIANCHANG_MOD
    // 现场模式
    create_xianchang_tasks();
#else
    // 会议室模式
    create_huiyishi_tasks();
#endif
}
