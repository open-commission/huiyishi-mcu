#include "esp_log.h"
#include "pn532/532.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main()
{
    bool init_flag = init_PN532_I2C(4, 5, 16, 13, I2C_NUM_0);
    ESP_LOGI("app_main", "init_flag = %d", init_flag);

    SAMConfig();

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    uint32_t firmware_version = getPN532FirmwareVersion();
    ESP_LOGI("app_main", "firmware_version = %d", firmware_version);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    uint8_t uid[4];
    uint8_t uidLength = 0;

    // 初始化PN532 I2C等（假设已调用 init_PN532_I2C() 等）

    // 尝试读取卡
    bool success = readPassiveTargetID(0x00, uid, &uidLength, 1000); // 1秒超时

    if (success && uidLength > 0)
    {
        ESP_LOGI("app_main", "找到卡，UID长度=%d, UID=", uidLength);
        for (int i = 0; i < uidLength; i++)
        {
            ESP_LOGI("app_main", "%02X ", uid[i]);
        }
    }
    else
    {
        ESP_LOGI("app_main", "未检测到卡或读取失败\n");
    }
}
