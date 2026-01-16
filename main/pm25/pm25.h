//
// Created by nebula on 2026/1/16.
//

#ifndef HUIYISHI_MCU_PM25_H
#define HUIYISHI_MCU_PM25_H
#include <stdint.h>

void pm25_sensor_init();

// --- 引脚定义 ---
#define PM25_LED_PIN    4   // 连接传感器的 LED 控制引脚 (D2)
#define PM25_READ_TIMES 20  // 平均值采样次数

uint16_t get_pm25_single_data();

#endif //HUIYISHI_MCU_PM25_H
