//
// Created by nebula on 2026/1/16.
//

#ifndef HUIYISHI_MCU_TASK_MANAGER_H
#define HUIYISHI_MCU_TASK_MANAGER_H

// 会议室模式任务
void create_huiyishi_tasks(void);
// 现场模式任务
void create_xianchang_tasks(void);

// 特定任务函数声明
void pn532_task(void* pvParameters);
void jw01_task(void* pvParameters);
void dht_task(void* p);
void guangzhao_task(void* p);
void pm25_task(void* pvParameters);

#endif //HUIYISHI_MCU_TASK_MANAGER_H