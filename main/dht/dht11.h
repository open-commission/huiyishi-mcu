//
// Created by nebula on 2026/1/16.
//

#ifndef HUIYISHI_MCU_DHT11_H
#define HUIYISHI_MCU_DHT11_H
#include <esp_err.h>

extern const char *TAG;

esp_err_t read_dht_raw(uint8_t data[5]);

#endif //HUIYISHI_MCU_DHT11_H
