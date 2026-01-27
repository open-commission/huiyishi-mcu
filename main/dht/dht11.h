//
// Created by nebula on 2026/1/16.
//

#ifndef HUIYISHI_MCU_DHT11_H
#define HUIYISHI_MCU_DHT11_H
#include <esp_err.h>

#define uchar unsigned char
#define uint8 unsigned char
#define uint16 unsigned short

extern uchar shidu, wendu;

void dht11_setup(void);

void DHT11(void);

#endif //HUIYISHI_MCU_DHT11_H
