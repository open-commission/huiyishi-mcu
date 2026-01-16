//
// Created by nebula on 2026/1/16.
//

#ifndef HUIYISHI_MCU_EVENT_H
#define HUIYISHI_MCU_EVENT_H
#include <FreeRTOS.h>
#include <queue.h>

extern xQueueHandle gpio_evt_queue;

void gpio_event_task(void* arg);

void IRAM_ATTR gpio_isr_handler(void* arg);

#endif //HUIYISHI_MCU_EVENT_H
