//
// Created by nebula on 2026/1/16.
//

#include "vars.h"

volatile huiyishi_data_type huiyishi_data = {
    .baojing_status = 0,
    .kaimen_status = 0,
    .wendu_var = 0.0,
    .shidu_var = 0.0,
    .guangzhao_var = 0.0
};

volatile xianchang_data_type xianchang_data = {
    .baojing_status = 0,
    .tongfeng_status = 0,
    .eryanghuatan_var = 0.0,
    .pm25_var = 0.0,
    .guangzhao_var = 0.0
};
