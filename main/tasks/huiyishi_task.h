//
// Created by nebula on 2026/1/16.
//

#ifndef HUIYISHI_MCU_HUIYISHI_TASK_H
#define HUIYISHI_MCU_HUIYISHI_TASK_H

void pn532_task(void* pvParameters);
void event_task(void* p);
void control_task(void* p);
void guangzhao_task(void* p);
void dht_task(void* p);

#endif //HUIYISHI_MCU_HUIYISHI_TASK_H
