//
// Created by nebula on 2026/1/16.
//

#ifndef HUIYISHI_MCU_VARS_H
#define HUIYISHI_MCU_VARS_H

//
// Created by nebula on 2026/1/15.
//

#ifndef HEALTHY_MCU_VARS_H
#define HEALTHY_MCU_VARS_H
#include <stdint.h>

typedef struct
{
    int baojing_status;
    int kaimen_status;
    float wendu_var;
    float shidu_var;
    float guangzhao_var;
    char rfid_card[64];
} huiyishi_data_type;

extern volatile huiyishi_data_type huiyishi_data;

typedef struct
{
    int baojing_status;
    int tongfeng_status;
    float eryanghuatan_var;
    float pm25_var;
    float wendu_var;
    float shidu_var;
    float guangzhao_var;
} xianchang_data_type;

extern volatile xianchang_data_type xianchang_data;

#endif //HEALTHY_MCU_VARS_H


#endif //HUIYISHI_MCU_VARS_H
