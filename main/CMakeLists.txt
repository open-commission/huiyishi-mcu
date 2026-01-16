idf_component_register(
        SRCS "huiyishi_mcu.c" "pn532/532.c" "co2/uco2.c" "control/ctrl.c" "dht/dht11.c"
        "global/vars.c" "guangzhao/guangzhao.c" "pm25/pm25.c" "tasks/task_manager.c"
        INCLUDE_DIRS "" "pn532" "co2" "control" "dht" "global" "guangzhao" "pm25" "tasks"
        "coap" "uart"
)