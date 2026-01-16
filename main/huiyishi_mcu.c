// #define XIANCHANG_MOD //定义了就是现场的，不定义就是会议室的

#ifdef  XIANCHANG_MOD

#include "xianchang_task.h"

#else  //===========================================================

#include "huiyishi_task.h"

#endif//XIANCHANG_MOD

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main()
{
#ifdef  XIANCHANG_MOD
    xTaskCreate(event_task, "event_task", 2048, NULL, 10, NULL);
    xTaskCreate(control_task, "control_task", 2048, NULL, 10, NULL);
    xTaskCreate(jw01_task, "jw01_task", 2048, NULL, 10, NULL);
    xTaskCreate(dht_task, "dht_task", 2048, NULL, 10, NULL);
#else  //===========================================================
    xTaskCreate(event_task, "event_task", 2048, NULL, 10, NULL);
    xTaskCreate(control_task, "control_task", 2048, NULL, 10, NULL);
    xTaskCreate(pn532_task, "jw01_task", 2048, NULL, 10, NULL);
    xTaskCreate(dht_task, "dht_task", 2048, NULL, 10, NULL);
#endif//XIANCHANG_MOD
}
