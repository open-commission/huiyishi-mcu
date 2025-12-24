#include "esp_log.h"
#include "pn532/532.h"


void app_main()
{
    bool init_flag = init_PN532_I2C(14, 2, 16, 13, I2C_NUM_0);
    ESP_LOGI("app_main", "init_flag = %d", init_flag);
    uint32_t firmware_version = getPN532FirmwareVersion();
    ESP_LOGI("app_main", "firmware_version = %d", firmware_version);
}
