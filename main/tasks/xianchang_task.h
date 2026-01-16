//
// Created by nebula on 2026/1/16.
//

#ifndef XIANCHANG_TASK_H
#define XIANCHANG_TASK_H

void event_task(void* p);
void control_task(void* p);
void jw01_task(void* pvParameters);
void dht_task(void* p);
void pm25_task(void* pvParameters);

#endif //XIANCHANG_TASK_H
