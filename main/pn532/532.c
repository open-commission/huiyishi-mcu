/**************************************************************************/
/*!
 @file     PN532.c
 @author   Luca Faccin
 @license  BSD (参见 license.txt)

 这是 Adafruit PN532 驱动程序的移植版本，仅用于 ESP32 的 I2C 总线
 NXP PN532 NFC/13.56MHz RFID 收发器驱动

 @section  HISTORY
 v 1.0		基于 Adafruit PN532 驱动 v 2.1 的基本移植
 */
/**************************************************************************/

#include <532.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include <stdlib.h>

#include "queue.h"

#define TAG "PN532"

uint8_t pn532ack[] =
    {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
uint8_t pn532response_firmwarevers[] =
    {0x00, 0xFF, 0x06, 0xFA, 0xD5, 0x03};
uint8_t SDA_PIN, SCL_PIN, RESET_PIN, IRQ_PIN;
i2c_port_t PN532_I2C_PORT;
uint8_t _uid[7]; // ISO14443A uid
uint8_t _uidLen; // uid 长度
uint8_t _key[6]; // Mifare Classic 密钥
uint8_t _inListedTag; // 已列入列表的标签的 Tg 号.

//IRQ 事件处理器
#define ESP_INTR_FLAG_DEFAULT 0
static xQueueHandle IRQQueue = NULL;
// 取消注释这些行以启用 PN532(SPI) 和/或 MIFARE 相关代码的调试输出

//#define CONFIG_PN532DEBUG CONFIG_PN532DEBUG
// #define CONFIG_MIFAREDEBUG
// #define CONFIG_IRQDEBUG

#define PN532_PACKBUFFSIZ 64
uint8_t pn532_packetbuffer[PN532_PACKBUFFSIZ];
uint8_t ACK_PACKET[] =
    {0x0, 0x0, 0xFF, 0x0, 0xFF, 0x0};
uint8_t NACK_PACKET[] =
    {0x0, 0x0, 0xFF, 0xFF, 0x0, 0x0};

#ifndef _BV
#define _BV(bit) (1<<(bit))
#endif

// 仅定义
bool SAMConfig(void);

/**
 * 向 PN532 发送复位信号
 */
static void resetPN532()
{
    gpio_set_level(RESET_PIN, 1);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    gpio_set_level(RESET_PIN, 0);
    vTaskDelay(400 / portTICK_PERIOD_MS);
    gpio_set_level(RESET_PIN, 1);
    vTaskDelay(15 / portTICK_PERIOD_MS); // 复位后执行其他操作前需要的小延迟
    //	 请参见数据手册第 209 页的时序图，第 12.23 节。
}

/**************************************************************************/
/*!
 @brief  向 PN532 写入命令，自动插入
 前导码和所需的帧详细信息（校验和，长度等）

 @param  cmd       指向命令缓冲区的指针
 @param  cmdlen    命令长度（以字节为单位）
 */
/**************************************************************************/
void writecommand(uint8_t* cmd, uint8_t cmdlen)
{
    // I2C 命令写入.
    uint8_t checksum;

    //创建命令
    uint8_t* command = malloc(cmdlen + 9);
    bzero(command, cmdlen + 9);

    vTaskDelay(10 / portTICK_PERIOD_MS);
    checksum = PN532_PREAMBLE + PN532_PREAMBLE + PN532_STARTCODE2;

    command[0] = PN532_I2C_ADDRESS;
    command[1] = PN532_PREAMBLE;
    command[2] = PN532_PREAMBLE;
    command[3] = PN532_STARTCODE2;
    command[4] = (cmdlen + 1);
    command[5] = ~(cmdlen + 1) + 1;
    command[6] = PN532_HOSTTOPN532;
    checksum += PN532_HOSTTOPN532;

    uint8_t i = 0;
    for (i = 0; i < cmdlen; i++)
    {
        command[i + 7] = cmd[i];
        checksum += cmd[i];
    }
    command[(cmdlen - 1) + 8] = ~checksum;
    command[(cmdlen - 1) + 9] = PN532_POSTAMBLE;

    //通过 I2C 发送数据
    i2c_cmd_handle_t i2ccmd = i2c_cmd_link_create();
    i2c_master_start(i2ccmd);
    i2c_master_write_byte(i2ccmd, command[0], true);
    for (i = 1; i < cmdlen + 9; i++)
        i2c_master_write_byte(i2ccmd, command[i], true);
    i2c_master_stop(i2ccmd);

#ifdef CONFIG_PN532DEBUG
    ESP_LOGD(TAG, "%s 发送: ", __func__);
    esp_log_buffer_hex(TAG, command, cmdlen + 9);
#endif

    esp_err_t result = ESP_OK;
    result = i2c_master_cmd_begin(PN532_I2C_PORT, i2ccmd, I2C_WRITE_TIMEOUT / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "发送命令：原数据开始");
    for (int j = 0; j < cmdlen + 9; j++)
    {
        ESP_LOGI(TAG, "0x%02X", command[j]);
    }
    ESP_LOGI(TAG, "发送命令：原数据结束");

    if (result != ESP_OK)
    {
        char* resultText = NULL;
        switch (result)
        {
        case ESP_ERR_INVALID_ARG:
            resultText = "参数错误";
            break;
        case ESP_FAIL:
            resultText = "发送命令错误，从设备未应答传输.";
            break;
        case ESP_ERR_INVALID_STATE:
            resultText = "I2C 驱动未安装或不在主模式.";
            break;
        case ESP_ERR_TIMEOUT:
            resultText = "操作超时，因为总线忙.";
            break;
        }
        ESP_LOGE(TAG, "%s I2C 写入失败: %s", __func__, resultText);
    }

    i2c_cmd_link_delete(i2ccmd);

    free(command);
}

/**************************************************************************/
/*!
 @brief  接收来自 IRQ 引脚的中断

 @param  ARG      						中断参数

 */
/**************************************************************************/
static void IRAM_ATTR IRQHandler(void* arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(IRQQueue, &gpio_num, NULL);
}

/**************************************************************************/
/*!
 @brief  设置硬件和 I2C 总线

 @param  sda      						SDA 信号的 GPIO 引脚
 @param  scl      						SCL 信号的 GPIO 引脚
 @param  reset     						复位信号的 GPIO 引脚
 @param  irq      						IRQ 信号的 GPIO 引脚
 @param  i2c_port_number      I2C 端口号

 @return 如果硬件设置正常则返回 true，否则返回 false
 */
/**************************************************************************/
bool init_PN532_I2C(uint8_t sda, uint8_t scl, uint8_t reset, uint8_t irq, i2c_port_t i2c_port_number)
{
    SCL_PIN = scl;
    SDA_PIN = sda;
    RESET_PIN = reset;
    IRQ_PIN = irq;
    PN532_I2C_PORT = i2c_port_number;

    uint64_t pintBitMask = ((1ULL) << RESET_PIN);

    //初始化引脚
    //让我们配置 GPIO 引脚用于复位
    gpio_config_t io_conf;
    //禁用中断
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //设置为输出模式
    io_conf.mode = GPIO_MODE_OUTPUT;
    //要设置的引脚位掩码，例如.GPIO18/19
    io_conf.pin_bit_mask = pintBitMask;
    //禁用下拉模式
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    //启用上拉模式
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    //使用给定设置配置 GPIO
    if (gpio_config(&io_conf) != ESP_OK) return false;

    pintBitMask = ((1ULL) << IRQ_PIN);
    //让我们配置 GPIO 引脚用于 IRQ
    //禁用中断
#ifdef CONFIG_ENABLE_IRQ_ISR

    io_conf.intr_type = GPIO_PIN_INTR_NEGEDGE;
#else
    io_conf.intr_type = GPIO_INTR_DISABLE;
#endif

    //设置为输出模式
    io_conf.mode = GPIO_MODE_INPUT;
    //要设置的引脚位掩码，例如.GPIO18/19
    io_conf.pin_bit_mask = pintBitMask;
    //禁用下拉模式
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    //启用上拉模式
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    //使用给定设置配置 GPIO
    if (gpio_config(&io_conf) != ESP_OK) return false;

    // 复位 PN532
    resetPN532();

#ifdef CONFIG_ENABLE_IRQ_ISR
    if (IRQQueue != NULL) vQueueDelete(IRQQueue);
    //创建一个队列来处理来自 isr 的 gpio 事件
    IRQQueue = xQueueCreate(1, sizeof(uint32_t));

    //启动 IRQ 服务
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    //为特定 gpio 引脚挂接 isr 处理程序
    gpio_isr_handler_add(IRQ_PIN, IRQHandler, (void*)IRQ_PIN);
#endif
    i2c_config_t conf;
    //打开 I2C 总线
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = SDA_PIN;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = SCL_PIN;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.clk_stretch_tick = 300;
    if (i2c_driver_install(PN532_I2C_PORT, conf.mode) != ESP_OK) return false;
    if (i2c_param_config(PN532_I2C_PORT, &conf) != ESP_OK) return false;
    return true;
}

/**************************************************************************/
/*!
 @brief  通过 SPI 或 I2C 从 PN532 读取 n 个字节的数据.

 @param  buff      指向将写入数据的缓冲区
 @param  n         要读取的字节数
 @return 如果读取成功则返回 true，否则返回 false
 */
/**************************************************************************/
bool readdata(uint8_t* buff, uint8_t n)
{
    i2c_cmd_handle_t i2ccmd;
    uint8_t* buffer = malloc(n + 3);

    vTaskDelay(10 / portTICK_PERIOD_MS);
    bzero(buffer, n + 3);
    bzero(buff, n);

    i2ccmd = i2c_cmd_link_create();
    i2c_master_start(i2ccmd);
    i2c_master_write_byte(i2ccmd, PN532_I2C_READ_ADDRESS, true);
    for (uint8_t i = 0; i < (n + 2); i++)
        i2c_master_read_byte(i2ccmd, &buffer[i], I2C_MASTER_ACK);
    i2c_master_read_byte(i2ccmd, &buffer[n + 2], I2C_MASTER_LAST_NACK);
    i2c_master_stop(i2ccmd);

    if (i2c_master_cmd_begin(PN532_I2C_PORT, i2ccmd, I2C_READ_TIMEOUT / portTICK_RATE_MS) != ESP_OK)
    {
        //重置 i2c 总线
        i2c_cmd_link_delete(i2ccmd);
        free(buffer);
        return false;
    };

    i2c_cmd_link_delete(i2ccmd);

    memcpy(buff, buffer + 1, n);
    // 开始读取 (n+1 以考虑 I2C 的前导 0x01)
#ifdef CONFIG_PN532DEBUG
    ESP_LOGD(TAG, "读取: ");
    esp_log_buffer_hex(TAG, buffer, n + 3);
#endif
    free(buffer);

    return true;
}

/************** 高级通信函数 (处理 I2C 和 SPI) */

/**************************************************************************/
/*!
 @brief  尝试读取 SPI 或 I2C 的 ACK 信号
 @return 如果接收到 ACK，则返回 true，否则返回 false
 */
/**************************************************************************/
bool readack()
{
    uint8_t ackbuff[6];

    readdata(ackbuff, 6);

    return (0 == strncmp((char*)ackbuff, (char*)pn532ack, 6));
}

/**************************************************************************/
/*!
 @brief  如果 PN532 已准备好响应，则返回 true。
 @return 如果 IRQ 信号为低电平，则返回 true
 */
/**************************************************************************/
bool isready()
{
    // I2C 通过 IRQ 线被拉低来检查状态是否就绪。
    uint8_t x = gpio_get_level(IRQ_PIN);
#ifdef CONFIG_IRQDEBUG
    ESP_LOGI(TAG, "IRQ: %d", x);
#endif
    return (x == 0);
}

/**************************************************************************/
/*!
 @brief  等待直到 PN532 就绪。

 @param  timeout   在放弃之前的超时时间（以毫秒为单位）。如果超时为 0 将无限等待。
 @return 如果 PN532 在超时前就绪则返回 true，否则返回 false
 */
/**************************************************************************/
bool waitready(uint16_t timeout)
{
#ifdef CONFIG_ENABLE_IRQ_ISR

    uint32_t io_num = 0;
    TickType_t delay = 0;
    if (timeout == 0) delay = portMAX_DELAY;
    else delay = timeout / portTICK_PERIOD_MS;

    xQueueReceive(IRQQueue, &io_num, delay);

    return (io_num == IRQ_PIN);
#else
    uint16_t timer = 0;
    while (!isready())
    {
        if (timeout != 0)
        {
            timer += 10;
            if (timer > timeout)
            {
#ifdef CONFIG_PN532DEBUG
                ESP_LOGE(TAG, "等待就绪超时 %d ms!", timeout);
#endif
                return false;
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    return true;
#endif
}

/**************************************************************************/
/*!
 @brief  发送命令并等待指定时间的 ACK

 @param  cmd       指向命令缓冲区的指针
 @param  cmdlen    命令的大小（以字节为单位）
 @param  timeout   放弃之前的超时时间

 @returns  如果一切正常则返回 true，如果在接收到
 ACK 之前超时则返回 0
 */
/**************************************************************************/
// 默认超时时间为一秒
bool sendCommandCheckAck(uint8_t* cmd, uint8_t cmdlen, uint16_t timeout)
{
    // 写入命令
    writecommand(cmd, cmdlen);

    // 等待芯片说它已就绪！
    if (!waitready(timeout))
    {
#ifdef CONFIG_PN532DEBUG
        ESP_LOGE(TAG, "超时");
#endif
        return false;
    }

    // 读取确认
    if (!readack())
    {
#ifdef CONFIG_PN532DEBUG
        ESP_LOGD(TAG, "未收到 ACK 帧！再试一次");
#endif
        return false;
    }

    return true; // 已确认命令
}

/**************************************************************************/
/*!
 @brief  检查 PN5xx 芯片的固件版本

 @returns  芯片的固件版本和 ID
 */
/**************************************************************************/
uint32_t getPN532FirmwareVersion(void)
{
    uint32_t response;

    pn532_packetbuffer[0] = PN532_COMMAND_GETFIRMWAREVERSION;

    if (!sendCommandCheckAck(pn532_packetbuffer, 1, I2C_WRITE_TIMEOUT))
    {
        return 0;
    }

    // 读取数据包
    readdata(pn532_packetbuffer, 12);

    // 检查一些基本内容
    if (0 != strncmp((char*)pn532_packetbuffer, (char*)pn532response_firmwarevers, 6))
    {
#ifdef CONFIG_PN532DEBUG
        ESP_LOGD(TAG, "固件不匹配！");
#endif
        return 0;
    }

    int offset = 7; // 使用 I2C 时跳过一个响应字节以忽略多余数据。
    response = pn532_packetbuffer[offset++];
    response <<= 8;
    response |= pn532_packetbuffer[offset++];
    response <<= 8;
    response |= pn532_packetbuffer[offset++];
    response <<= 8;
    response |= pn532_packetbuffer[offset++];

    return response;
}

/**************************************************************************/
/*!
 写入一个 8 位值来设置 PN532 的 GPIO 引脚状态

 @warning 此函数仅用于板测试，
 如果修改标记为 "可用作 GPIO" 之外的任何引脚，
 它将抛出错误！所有不能用作 GPIO 的引脚
 应始终保持高电平（值 = 1），否则系统将变得不稳定，
 并且需要硬件复位才能恢复 PN532。

 pinState[0]  = P30     可用作 GPIO
 pinState[1]  = P31     可用作 GPIO
 pinState[2]  = P32     *** 预留 (必须为 1!) ***
 pinState[3]  = P33     可用作 GPIO
 pinState[4]  = P34     *** 预留 (必须为 1!) ***
 pinState[5]  = P35     可用作 GPIO

 @returns 1 如果一切执行正常，0 表示错误
 */
/**************************************************************************/
bool writeGPIO(uint8_t pinstate)
{
    // 确保 pinstate 不尝试切换 P32 或 P34
    pinstate |= (1 << PN532_GPIO_P32) | (1 << PN532_GPIO_P34);

    // 填充命令缓冲区
    pn532_packetbuffer[0] = PN532_COMMAND_WRITEGPIO;
    pn532_packetbuffer[1] = PN532_GPIO_VALIDATIONBIT | pinstate; // P3 引脚
    pn532_packetbuffer[2] = 0x00; // P7 GPIO 引脚 (未使用 ... 被 SPI 占用)

#ifdef CONFIG_PN532DEBUG
    ESP_LOGD(TAG, "写入 P3 GPIO: 0x%.2X", pn532_packetbuffer[1]);
#endif

    // 发送 WRITEGPIO 命令 (0x0E)
    if (!sendCommandCheckAck(pn532_packetbuffer, 3, I2C_WRITE_TIMEOUT)) return 0x0;

    // 读取响应包 (00 FF PLEN PLENCHECKSUM D5 CMD+1(0x0F) DATACHECKSUM 00)
    readdata(pn532_packetbuffer, 8);

#ifdef CONFIG_PN532DEBUG
    ESP_LOGD(TAG, "接收到: 0x%.2X 0x%.2X 0x%.2X 0x%.2X 0x%.2X 0x%.2X 0x%.2X 0x%.2X", pn532_packetbuffer[0],
             pn532_packetbuffer[1], pn532_packetbuffer[2], pn532_packetbuffer[3], pn532_packetbuffer[4],
             pn532_packetbuffer[5], pn532_packetbuffer[6], pn532_packetbuffer[7]);
#endif

    int offset = 6;
    return (pn532_packetbuffer[offset] == 0x0F);
}

/**************************************************************************/
/*!
 读取 PN532 的 GPIO 引脚状态

 @returns 包含引脚状态的 8 位值，其中：

 pinState[0]  = P30
 pinState[1]  = P31
 pinState[2]  = P32
 pinState[3]  = P33
 pinState[4]  = P4
 pinState[5]  = P35
 */
/**************************************************************************/
uint8_t readGPIO(void)
{
    pn532_packetbuffer[0] = PN532_COMMAND_READGPIO;

    // 发送 READGPIO 命令 (0x0C)
    if (!sendCommandCheckAck(pn532_packetbuffer, 1, I2C_WRITE_TIMEOUT)) return 0x0;

    // 读取响应包 (00 FF PLEN PLENCHECKSUM D5 CMD+1(0x0D) P3 P7 IO1 DATACHECKSUM 00)
    readdata(pn532_packetbuffer, 50);

    /* READGPIO 响应应为以下格式：

     字节            描述
     -------------   ------------------------------------------
     b0..5           帧头和前导码 (I2C 中有一个额外的 0x00)
     b6              P3 GPIO 引脚
     b7              P7 GPIO 引脚 (未使用 ... 被 SPI 占用)
     b8              接口模式引脚 (未使用 ... 总线选择引脚)
     b9..10          校验和 */

    int p3offset = 7;

#ifdef CONFIG_PN532DEBUG
    printf("接收到: ");
    esp_log_buffer_hex(TAG, pn532_packetbuffer, 11);
    printf("\n");
    ESP_LOGD(TAG, "P3 GPIO: 0x%.2X", pn532_packetbuffer[p3offset]);
    ESP_LOGD(TAG, "P7 GPIO: 0x%.2X", pn532_packetbuffer[p3offset + 1]);
    ESP_LOGD(TAG, "P10 GPIO: 0x%.2X", pn532_packetbuffer[p3offset + 2]);

    // 注意：您可以使用 IO GPIO 值来检测正在使用的串行总线
    switch (pn532_packetbuffer[p3offset + 2])
    {
    case 0x00: // 使用 UART

        ESP_LOGD(TAG, "使用 UART (IO = 0x00)");
        break;
    case 0x01: // 使用 I2C
        ESP_LOGD(TAG, "使用 I2C (IO = 0x01)");
        break;
    case 0x02: // 使用 SPI
        ESP_LOGD(TAG, "使用 SPI (IO = 0x02)");
        break;
    }
#endif

    return pn532_packetbuffer[p3offset];
}

/**************************************************************************/
/*!
 @brief  配置 SAM (安全访问模块)
 */
/**************************************************************************/
bool SAMConfig(void)
{
    pn532_packetbuffer[0] = PN532_COMMAND_SAMCONFIGURATION;
    pn532_packetbuffer[1] = 0x01; // 正常模式;
    pn532_packetbuffer[2] = 0x14; // 超时 50ms * 20 = 1 秒
    pn532_packetbuffer[3] = 0x01; // 使用 IRQ 引脚!

    if (!sendCommandCheckAck(pn532_packetbuffer, 4, I2C_WRITE_TIMEOUT)) return false;

    // 读取数据包
    readdata(pn532_packetbuffer, 50);

    int offset = 6;
    return (pn532_packetbuffer[offset] == 0x15);
}

/**************************************************************************/
/*!
 设置 RFConfiguration 寄存器的 MxRtyPassiveActivation 字节

 @param  maxRetries    0xFF 表示永远等待，0x00..0xFE 表示在
 mxRetries 次后超时

 @returns 1 如果一切执行正常，0 表示错误
 */
/**************************************************************************/
bool setPassiveActivationRetries(uint8_t maxRetries)
{
    pn532_packetbuffer[0] = PN532_COMMAND_RFCONFIGURATION;
    pn532_packetbuffer[1] = 5; // 配置项 5 (MaxRetries)
    pn532_packetbuffer[2] = 0xFF; // MxRtyATR (默认 = 0xFF)
    pn532_packetbuffer[3] = 0x01; // MxRtyPSL (默认 = 0x01)
    pn532_packetbuffer[4] = maxRetries;

#ifdef CONFIG_MIFAREDEBUG
    ESP_LOGD(TAG, "设置 MxRtyPassiveActivation 为 %d", maxRetries);
#endif

    if (!sendCommandCheckAck(pn532_packetbuffer, 5, I2C_WRITE_TIMEOUT)) return 0x0; // 无确认

    return 1;
}

/***** ISO14443A 命令 ******/

/**************************************************************************/
/*!
 等待 ISO14443A 目标进入场

 @param  cardBaudRate  卡片的波特率
 @param  uid           指向将被填充
 卡片 UID 的数组（最多 7 字节）
 @param  uidLength     指向将保存
 卡片 UID 长度的变量

 @returns 1 如果一切执行正常，0 表示错误
 */
/**************************************************************************/
bool readPassiveTargetID(uint8_t cardbaudrate, uint8_t* uid, uint8_t* uidLength, uint16_t timeout)
{
    pn532_packetbuffer[0] = PN532_COMMAND_INLISTPASSIVETARGET;
    pn532_packetbuffer[1] = 1; // 最多 1 张卡同时 (我们可以稍后将其设置为 2)
    pn532_packetbuffer[2] = cardbaudrate;

    if (!sendCommandCheckAck(pn532_packetbuffer, 3, I2C_WRITE_TIMEOUT))
    {
#ifdef CONFIG_PN532DEBUG
        ESP_LOGD(TAG, "未读取到卡片");
#endif
        return 0x0; // 未读取到卡片
    }

#ifdef CONFIG_PN532DEBUG
    ESP_LOGD(TAG, "等待 IRQ (表示卡片存在)");
#endif
    if (!waitready(timeout))
    {
#ifdef CONFIG_PN532DEBUG
        ESP_LOGD(TAG, "IRQ 超时");
#endif
        return 0x0;
    }

    // 读取数据包
    readdata(pn532_packetbuffer, 20);
    // 检查一些基本内容

    /* ISO14443A 卡片响应应为以下格式：

     字节            描述
     -------------   ------------------------------------------
     b0..6           帧头和前导码
     b7              找到的标签
     b8              标签编号 (在此示例中仅使用一个)
     b9..10          SENS_RES
     b11             SEL_RES
     b12             NFCID 长度
     b13..NFCIDLen   NFCID                                      */

#ifdef CONFIG_MIFAREDEBUG
    ESP_LOGD(TAG, "找到 %d 个标签", pn532_packetbuffer[7]);
#endif
    if (pn532_packetbuffer[7] != 1) return 0;

    uint16_t sens_res = pn532_packetbuffer[9];
    sens_res <<= 8;
    sens_res |= pn532_packetbuffer[10];
#ifdef CONFIG_MIFAREDEBUG
    ESP_LOGD(TAG, "ATQA: 0x%.2X", sens_res);
    ESP_LOGD(TAG, "SAK: 0x%.2X", pn532_packetbuffer[11]);
#endif

    /* 卡片似乎是 Mifare Classic */
    *uidLength = pn532_packetbuffer[12];
#ifdef CONFIG_MIFAREDEBUG
    printf("UID:");
#endif
    for (uint8_t i = 0; i < pn532_packetbuffer[12]; i++)
    {
        uid[i] = pn532_packetbuffer[13 + i];
#ifdef CONFIG_MIFAREDEBUG
        printf(" 0x%.2X", uid[i]);
#endif
    }
#ifdef CONFIG_MIFAREDEBUG
    printf("\n");
#endif

    return 1;
}

/**************************************************************************/
/*!
 @brief  与当前已列入列表的对等方交换 APDU

 @param  send            指向要发送数据的指针
 @param  sendLength      要发送的数据长度
 @param  response        指向响应数据的指针
 @param  responseLength  指向响应数据长度的指针
 */
/**************************************************************************/
bool inDataExchange(uint8_t* send, uint8_t sendLength, uint8_t* response, uint8_t* responseLength)
{
    if (sendLength > PN532_PACKBUFFSIZ - 2)
    {
#ifdef CONFIG_PN532DEBUG
        ESP_LOGD(TAG, "APDU 长度对于数据包缓冲区太长");
#endif
        return false;
    }
    uint8_t i;

    pn532_packetbuffer[0] = 0x40; // PN532_COMMAND_INDATAEXCHANGE;
    pn532_packetbuffer[1] = _inListedTag;
    for (i = 0; i < sendLength; ++i)
    {
        pn532_packetbuffer[i + 2] = send[i];
    }

    if (!sendCommandCheckAck(pn532_packetbuffer, sendLength + 2, I2C_WRITE_TIMEOUT))
    {
#ifdef CONFIG_PN532DEBUG
        ESP_LOGD(TAG, "无法发送 APDU");
#endif
        return false;
    }

    if (!waitready(1000))
    {
#ifdef CONFIG_PN532DEBUG
        ESP_LOGD(TAG, "APDU 从未收到响应...");
#endif
        return false;
    }

    readdata(pn532_packetbuffer, sizeof(pn532_packetbuffer));

    if (pn532_packetbuffer[0] == 0 && pn532_packetbuffer[1] == 0 && pn532_packetbuffer[2] == 0xff)
    {
        uint8_t length = pn532_packetbuffer[3];
        if (pn532_packetbuffer[4] != (uint8_t)(~length + 1))
        {
#ifdef CONFIG_PN532DEBUG
            ESP_LOGD(TAG, "长度检查无效 0x%.2X 0x%.2X", length, (~length) + 1);

#endif
            return false;
        }
        if (pn532_packetbuffer[5] == PN532_PN532TOHOST && pn532_packetbuffer[6] == PN532_RESPONSE_INDATAEXCHANGE)
        {
            if ((pn532_packetbuffer[7] & 0x3f) != 0)
            {
#ifdef CONFIG_PN532DEBUG
                ESP_LOGD(TAG, "状态码表示错误");
#endif
                return false;
            }

            length -= 3;

            if (length > *responseLength)
            {
                length = *responseLength; // 静默截断...
            }

            for (i = 0; i < length; ++i)
            {
                response[i] = pn532_packetbuffer[8 + i];
            }
            *responseLength = length;

            return true;
        }
        else
        {
            ESP_LOGD(TAG, "不知道如何处理此命令: 0x%.2X", pn532_packetbuffer[6]);
            return false;
        }
    }
    else
    {
        ESP_LOGD(TAG, "缺少前导码");
        return false;
    }
}

/**************************************************************************/
/*!
 @brief  '列入' 一个被动目标。PN532 作为读取器/发起者，
 对方作为卡片/响应者。
 */
/**************************************************************************/
bool inListPassiveTarget()
{
    pn532_packetbuffer[0] = PN532_COMMAND_INLISTPASSIVETARGET;
    pn532_packetbuffer[1] = 1;
    pn532_packetbuffer[2] = 0;

#ifdef CONFIG_PN532DEBUG
    ESP_LOGD(TAG, "即将列入被动目标");
#endif

    if (!sendCommandCheckAck(pn532_packetbuffer, 3, I2C_WRITE_TIMEOUT))
    {
#ifdef CONFIG_PN532DEBUG
        ESP_LOGD(TAG, "无法发送列入消息");
#endif
        return false;
    }

    if (!waitready(30000))
    {
        return false;
    }

    readdata(pn532_packetbuffer, sizeof(pn532_packetbuffer));

    if (pn532_packetbuffer[0] == 0 && pn532_packetbuffer[1] == 0 && pn532_packetbuffer[2] == 0xff)
    {
        uint8_t length = pn532_packetbuffer[3];
        if (pn532_packetbuffer[4] != (uint8_t)(~length + 1))
        {
#ifdef CONFIG_PN532DEBUG
            ESP_LOGD(TAG, "长度检查无效 0x%.2X 0x%.2X", length, (~length) + 1);

#endif
            return false;
        }
        if (pn532_packetbuffer[5] == PN532_PN532TOHOST && pn532_packetbuffer[6] == PN532_RESPONSE_INLISTPASSIVETARGET)
        {
            if (pn532_packetbuffer[7] != 1)
            {
#ifdef CONFIG_PN532DEBUG
                ESP_LOGD(TAG, "未处理的已列入目标数量");
#endif
                ESP_LOGI(TAG, "已列入的标签数量: %d", pn532_packetbuffer[7]);
                return false;
            }

            _inListedTag = pn532_packetbuffer[8];
            ESP_LOGI(TAG, "标签编号: %d", _inListedTag);

            return true;
        }
        else
        {
#ifdef CONFIG_PN532DEBUG
            ESP_LOGD(TAG, "对列入被动主机的意外响应");
#endif
            return false;
        }
    }
    else
    {
#ifdef CONFIG_PN532DEBUG
        ESP_LOGD(TAG, "缺少前导码");
#endif
        return false;
    }

    return true;
}

/***** Mifare Classic 函数 ******/

/**************************************************************************/
/*!
 指示指定的块号是否是扇区中的第一个块
 (相对于当前扇区的块 0)
 */
/**************************************************************************/
bool mifareclassic_IsFirstBlock(uint32_t uiBlock)
{
    // 测试我们是否在小扇区或大扇区
    if (uiBlock < 128) return ((uiBlock) % 4 == 0);
    else return ((uiBlock) % 16 == 0);
}

/**************************************************************************/
/*!
 指示指定的块号是否是扇区尾部
 */
/**************************************************************************/
bool mifareclassic_IsTrailerBlock(uint32_t uiBlock)
{
    // 测试我们是否在小扇区或大扇区
    if (uiBlock < 128) return ((uiBlock + 1) % 4 == 0);
    else return ((uiBlock + 1) % 16 == 0);
}

/**************************************************************************/
/*!
 尝试使用 INDATAEXCHANGE 命令对 MIFARE 卡上的内存块进行认证。
 有关发送 MIFARE 和其他命令的更多信息，请参见 PN532 用户手册的第 7.3.8 节。

 @param  uid           指向包含卡片 UID 的字节数组的指针
 @param  uidLen        卡片 UID 的长度（以字节为单位）（对于
 MIFARE Classic 应该是 4）
 @param  blockNumber   要认证的块号。（对于 1KB 卡是 0..63，
 对于 4KB 卡是 0..255）。
 @param  keyNumber     认证期间使用的密钥类型
 (0 = MIFARE_CMD_AUTH_A, 1 = MIFARE_CMD_AUTH_B)
 @param  keyData       指向包含 6 字节
 密钥值的字节数组的指针

 @returns 1 如果一切执行正常，0 表示错误
 */
/**************************************************************************/
uint8_t mifareclassic_AuthenticateBlock(uint8_t* uid, uint8_t uidLen, uint32_t blockNumber, uint8_t keyNumber,
                                        uint8_t* keyData)
{
    uint8_t i;

    // 保存密钥和 UID 数据
    memcpy(_key, keyData, 6);
    memcpy(_uid, uid, uidLen);
    _uidLen = uidLen;

#ifdef CONFIG_MIFAREDEBUG
    ESP_LOGD(TAG, "尝试认证卡片 ");
    esp_log_buffer_hex(TAG, _uid, _uidLen);
    ESP_LOGD(TAG, "使用认证密钥 %c :", keyNumber ? 'B' : 'A');
    esp_log_buffer_hex(TAG, _key, 6);
#endif

    // 准备认证命令 //
    pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE; /* 数据交换头 */
    pn532_packetbuffer[1] = 1; /* 最大卡片数量 */
    pn532_packetbuffer[2] = (keyNumber) ? MIFARE_CMD_AUTH_B : MIFARE_CMD_AUTH_A;
    pn532_packetbuffer[3] = blockNumber; /* 块号 (1K = 0..63, 4K = 0..255 */
    memcpy(pn532_packetbuffer + 4, _key, 6);
    for (i = 0; i < _uidLen; i++)
    {
        pn532_packetbuffer[10 + i] = _uid[i]; /* 4 字节卡片 ID */
    }

    if (!sendCommandCheckAck(pn532_packetbuffer, 10 + _uidLen, I2C_WRITE_TIMEOUT)) return 0;

    // 读取响应包
    readdata(pn532_packetbuffer, 12);

    // 检查响应是否有效以及我们是否已通过认证???
    // 认证成功时应该是字节 5-7: 0xD5 0x41 0x00
    // Mifare 认证错误技术上是字节 7: 0x14，但除了 0x00 之外的任何内容都不好
    if (pn532_packetbuffer[7] != 0x00)
    {
#ifdef CONFIG_PN532DEBUG
        ESP_LOGD(TAG, "认证失败: ");
        esp_log_buffer_hex(TAG, pn532_packetbuffer, 12);
#endif
        return 0;
    }

    return 1;
}

/**************************************************************************/
/*!
 尝试在指定的块地址读取整个 16 字节的数据块。

 @param  blockNumber   要认证的块号。（对于
 1KB 卡是 0..63，对于 4KB 卡是 0..255）。
 @param  data          指向将保存
 检索到的数据（如果有的话）的字节数组

 @returns 1 如果一切执行正常，0 表示错误
 */
/**************************************************************************/
uint8_t mifareclassic_ReadDataBlock(uint8_t blockNumber, uint8_t* data)
{
#ifdef CONFIG_MIFAREDEBUG
    ESP_LOGD(TAG, "尝试从块 %d 读取 16 字节", blockNumber);
#endif

    /* 准备命令 */
    pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
    pn532_packetbuffer[1] = 1; /* 卡片编号 */
    pn532_packetbuffer[2] = MIFARE_CMD_READ; /* Mifare 读取命令 = 0x30 */
    pn532_packetbuffer[3] = blockNumber; /* 块号 (0..63 for 1K, 0..255 for 4K) */

    /* 发送命令 */
    if (!sendCommandCheckAck(pn532_packetbuffer, 4, I2C_WRITE_TIMEOUT))
    {
#ifdef CONFIG_MIFAREDEBUG
        ESP_LOGD(TAG, "读取命令未收到确认");
#endif
        return 0;
    }

    /* 读取响应包 */
    readdata(pn532_packetbuffer, 26);

    /* 如果字节 8 不是 0x00，我们可能有错误 */
    if (pn532_packetbuffer[7] != 0x00)
    {
#ifdef CONFIG_MIFAREDEBUG
        ESP_LOGD(TAG, "意外响应");
        esp_log_buffer_hex(TAG, pn532_packetbuffer, 26);
#endif
        return 0;
    }

    /* 将 16 个数据字节复制到输出缓冲区 */
    /* 块内容从有效响应的字节 9 开始 */
    memcpy(data, pn532_packetbuffer + 8, 16);

    /* 如需要，显示数据进行调试 */
#ifdef CONFIG_MIFAREDEBUG
    ESP_LOGD(TAG, "块 %d", blockNumber);
    esp_log_buffer_hex(TAG, data, 16);
#endif

    return 1;
}

/**************************************************************************/
/*!
 尝试在指定的块地址写入整个 16 字节的数据块。

 @param  blockNumber   要认证的块号。（对于
 1KB 卡是 0..63，对于 4KB 卡是 0..255）。
 @param  data          包含要写入数据的字节数组。

 @returns 1 如果一切执行正常，0 表示错误
 */
/**************************************************************************/
uint8_t mifareclassic_WriteDataBlock(uint8_t blockNumber, uint8_t* data)
{
#ifdef CONFIG_MIFAREDEBUG
    ESP_LOGD(TAG, "尝试向块 %d 写入 16 字节", blockNumber);
#endif

    /* 准备第一个命令 */
    pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
    pn532_packetbuffer[1] = 1; /* 卡片编号 */
    pn532_packetbuffer[2] = MIFARE_CMD_WRITE; /* Mifare 写入命令 = 0xA0 */
    pn532_packetbuffer[3] = blockNumber; /* 块号 (0..63 for 1K, 0..255 for 4K) */
    memcpy(pn532_packetbuffer + 4, data, 16); /* 数据载荷 */

    /* 发送命令 */
    if (!sendCommandCheckAck(pn532_packetbuffer, 20, I2C_WRITE_TIMEOUT))
    {
#ifdef CONFIG_MIFAREDEBUG
        ESP_LOGD(TAG, "写入命令未收到确认");
#endif
        return 0;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);

    /* 读取响应包 */
    readdata(pn532_packetbuffer, 26);

    return 1;
}

/**************************************************************************/
/*!
 格式化 Mifare Classic 卡以存储 NDEF 记录

 @returns 1 如果一切执行正常，0 表示错误
 */
/**************************************************************************/
uint8_t mifareclassic_FormatNDEF(void)
{
    uint8_t sectorbuffer1[16] =
        {0x14, 0x01, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1};
    uint8_t sectorbuffer2[16] =
        {0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1};
    uint8_t sectorbuffer3[16] =
        {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0x78, 0x77, 0x88, 0xC1, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // 注意 0xA0 0xA1 0xA2 0xA3 0xA4 0xA5 必须用于
    // NDEF 记录的 MAD 扇区中的密钥 A (扇区 0)

    // 将块 1 和 2 写入卡片
    if (!(mifareclassic_WriteDataBlock(1, sectorbuffer1))) return 0;
    if (!(mifareclassic_WriteDataBlock(2, sectorbuffer2))) return 0;
    // 写入密钥 A 和访问权限卡片
    if (!(mifareclassic_WriteDataBlock(3, sectorbuffer3))) return 0;

    // 看起来一切都正常 (?!)
    return 1;
}

/**************************************************************************/
/*!
 将 NDEF URI 记录写入指定扇区 (1..15)

 注意，此函数假定 Mifare Classic 卡
 已经格式化为用作 "NFC 论坛标签" 并使用 MAD1
 文件系统。您可以使用 Android 上的 NXP TagWriter 应用
 来正确格式化卡片以用于此目的。

 @param  sectorNumber  URI 记录应写入的扇区
 (对于 1K 卡可以是 1..15)
 @param  uriIdentifier URI 标识符代码 (0 = 无, 0x01 =
 "http://www.", 等)
 @param  url           要写入的 uri 文本 (最多 38 个字符)。

 @returns 1 如果一切执行正常，0 表示错误
 */
/**************************************************************************/
uint8_t mifareclassic_WriteNDEFURI(uint8_t sectorNumber, uint8_t uriIdentifier, const char* url)
{
    // 计算字符串长度
    uint8_t len = strlen(url);

    // 确保我们在 1K 卡的扇区号限制内
    if ((sectorNumber < 1) || (sectorNumber > 15)) return 0;

    // 确保 URI 载荷在 1 到 38 个字符之间
    if ((len < 1) || (len > 38)) return 0;

    // 注意 0xD3 0xF7 0xD3 0xF7 0xD3 0xF7 必须用于
    // NDEF 记录中的密钥 A

    // 设置扇区缓冲区 (w/预格式化的 TLV 包装器和 NDEF 消息)
    uint8_t sectorbuffer1[16] =
        {0x00, 0x00, 0x03, len + 5, 0xD1, 0x01, len + 1, 0x55, uriIdentifier, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t sectorbuffer2[16] =
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t sectorbuffer3[16] =
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t sectorbuffer4[16] =
        {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7, 0x7F, 0x07, 0x88, 0x40, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (len <= 6)
    {
        // 不太可能得到这么短的 URL，但为什么不呢...
        memcpy(sectorbuffer1 + 9, url, len);
        sectorbuffer1[len + 9] = 0xFE;
    }
    else if (len == 7)
    {
        // 0xFE 需要包装到下一个块
        memcpy(sectorbuffer1 + 9, url, len);
        sectorbuffer2[0] = 0xFE;
    }
    else if ((len > 7) && (len <= 22))
    {
        // URL 适合两个块
        memcpy(sectorbuffer1 + 9, url, 7);
        memcpy(sectorbuffer2, url + 7, len - 7);
        sectorbuffer2[len - 7] = 0xFE;
    }
    else if (len == 23)
    {
        // 0xFE 需要包装到最终块
        memcpy(sectorbuffer1 + 9, url, 7);
        memcpy(sectorbuffer2, url + 7, len - 7);
        sectorbuffer3[0] = 0xFE;
    }
    else
    {
        // URL 适合三个块
        memcpy(sectorbuffer1 + 9, url, 7);
        memcpy(sectorbuffer2, url + 7, 16);
        memcpy(sectorbuffer3, url + 23, len - 24);
        sectorbuffer3[len - 22] = 0xFE;
    }

    // 现在将所有三个块写回卡片
    if (!(mifareclassic_WriteDataBlock(sectorNumber * 4, sectorbuffer1))) return 0;
    if (!(mifareclassic_WriteDataBlock((sectorNumber * 4) + 1, sectorbuffer2))) return 0;
    if (!(mifareclassic_WriteDataBlock((sectorNumber * 4) + 2, sectorbuffer3))) return 0;
    if (!(mifareclassic_WriteDataBlock((sectorNumber * 4) + 3, sectorbuffer4))) return 0;

    // 看起来一切都正常 (?!)
    return 1;
}

/***** Mifare Ultralight 函数 ******/

/**************************************************************************/
/*!
 尝试在指定地址读取整个 4 字节页面。

 @param  page        页面号 (大多数情况下为 0..63)
 @param  buffer      指向将保存
 检索到的数据（如果有的话）的字节数组
 */
/**************************************************************************/
uint8_t mifareultralight_ReadPage(uint8_t page, uint8_t* buffer)
{
    if (page >= 64)
    {
#ifdef CONFIG_MIFAREDEBUG
        ESP_LOGD(TAG, "页面值超出范围");
#endif
        return 0;
    }

#ifdef CONFIG_MIFAREDEBUG
    ESP_LOGD(TAG, "读取页面 %d", page);
#endif

    /* 准备命令 */
    pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
    pn532_packetbuffer[1] = 1; /* 卡片编号 */
    pn532_packetbuffer[2] = MIFARE_CMD_READ; /* Mifare 读取命令 = 0x30 */
    pn532_packetbuffer[3] = page; /* 页面号 (大多数情况下为 0..63) */

    /* 发送命令 */
    if (!sendCommandCheckAck(pn532_packetbuffer, 4, I2C_WRITE_TIMEOUT))
    {
#ifdef CONFIG_MIFAREDEBUG
        ESP_LOGD(TAG, "写入命令未收到确认");
#endif
        return 0;
    }

    /* 读取响应包 */
    readdata(pn532_packetbuffer, 26);
#ifdef CONFIG_MIFAREDEBUG
    ESP_LOGD(TAG, "接收到: ");
    esp_log_buffer_hex(TAG, pn532_packetbuffer, 26);
#endif

    /* 如果字节 8 不是 0x00，我们可能有错误 */
    if (pn532_packetbuffer[7] == 0x00)
    {
        /* 将 4 个数据字节复制到输出缓冲区 */
        /* 块内容从有效响应的字节 9 开始 */
        /* 注意，命令实际上读取 16 字节或 4 */
        /* 页一次 ... 我们简单地丢弃最后 12 */
        /* 字节 */
        memcpy(buffer, pn532_packetbuffer + 8, 4);
    }
    else
    {
#ifdef CONFIG_MIFAREDEBUG
        ESP_LOGD(TAG, "读取块时意外响应: ");
        esp_log_buffer_hex(TAG, pn532_packetbuffer, 26);
#endif
        return 0;
    }

    /* 如需要，显示数据进行调试 */
#ifdef CONFIG_MIFAREDEBUG
    ESP_LOGD(TAG, "页面 %d", page);
    esp_log_buffer_hex(TAG, buffer, 4);
#endif

    // 返回 OK 信号
    return 1;
}

/**************************************************************************/
/*!
 尝试在指定块地址写入整个 4 字节页面。

 @param  page          要写入的页面号。(大多数情况下为 0..63)
 @param  data          包含要写入数据的字节数组。
 应该正好是 4 字节长。

 @returns 1 如果一切执行正常，0 表示错误
 */
/**************************************************************************/
uint8_t mifareultralight_WritePage(uint8_t page, uint8_t* data)
{
    if (page >= 64)
    {
#ifdef CONFIG_MIFAREDEBUG
        ESP_LOGD(TAG, "页面值超出范围");
#endif
        // 返回失败信号
        return 0;
    }

#ifdef CONFIG_MIFAREDEBUG
    ESP_LOGD(TAG, "尝试写入 4 字节页面 %d", page);
#endif

    /* 准备第一个命令 */
    pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
    pn532_packetbuffer[1] = 1; /* 卡片编号 */
    pn532_packetbuffer[2] = MIFARE_ULTRALIGHT_CMD_WRITE; /* Mifare Ultralight 写入命令 = 0xA2 */
    pn532_packetbuffer[3] = page; /* 页面号 (大多数情况下为 0..63) */
    memcpy(pn532_packetbuffer + 4, data, 4); /* 数据载荷 */

    /* 发送命令 */
    if (!sendCommandCheckAck(pn532_packetbuffer, 8, I2C_WRITE_TIMEOUT))
    {
#ifdef CONFIG_MIFAREDEBUG
        ESP_LOGD(TAG, "写入命令未收到确认");
#endif

        // 返回失败信号
        return 0;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);

    /* 读取响应包 */
    readdata(pn532_packetbuffer, 26);

    // 返回 OK 信号
    return 1;
}

/***** NTAG2xx 函数 ******/

/**************************************************************************/
/*!
 尝试在指定地址读取整个 4 字节页面。

 @param  page        页面号 (大多数情况下为 0..63)
 @param  buffer      指向将保存
 检索到的数据（如果有的话）的字节数组
 */
/**************************************************************************/
uint8_t ntag2xx_ReadPage(uint8_t page, uint8_t* buffer)
{
    // 标签类型       页数   用户开始    用户结束
    // --------       -----   ----------    ---------
    // NTAG 203       42      4             39
    // NTAG 213       45      4             39
    // NTAG 215       135     4             129
    // NTAG 216       231     4             225

    if (page >= 231)
    {
#ifdef CONFIG_MIFAREDEBUG
        ESP_LOGD(TAG, "页面值超出范围");
#endif
        return 0;
    }

#ifdef CONFIG_MIFAREDEBUG
    ESP_LOGD(TAG, "读取页面 %d", page);
#endif

    /* 准备命令 */
    pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
    pn532_packetbuffer[1] = 1; /* 卡片编号 */
    pn532_packetbuffer[2] = MIFARE_CMD_READ; /* Mifare 读取命令 = 0x30 */
    pn532_packetbuffer[3] = page; /* 页面号 (大多数情况下为 0..63) */

    /* 发送命令 */
    if (!sendCommandCheckAck(pn532_packetbuffer, 4, I2C_WRITE_TIMEOUT))
    {
#ifdef CONFIG_MIFAREDEBUG
        ESP_LOGD(TAG, "写入命令未收到确认");
#endif
        return 0;
    }

    /* 读取响应包 */
    readdata(pn532_packetbuffer, 26);
#ifdef CONFIG_MIFAREDEBUG
    ESP_LOGD(TAG, "接收到: ");
    esp_log_buffer_hex(TAG, pn532_packetbuffer, 26);
#endif

    /* 如果字节 8 不是 0x00，我们可能有错误 */
    if (pn532_packetbuffer[7] == 0x00)
    {
        /* 将 4 个数据字节复制到输出缓冲区 */
        /* 块内容从有效响应的字节 9 开始 */
        /* 注意，命令实际上读取 16 字节或 4 */
        /* 页一次 ... 我们简单地丢弃最后 12 */
        /* 字节 */
        memcpy(buffer, pn532_packetbuffer + 8, 4);
    }
    else
    {
#ifdef CONFIG_MIFAREDEBUG
        ESP_LOGD(TAG, "读取块时意外响应: ");
        esp_log_buffer_hex(TAG, pn532_packetbuffer, 26);
#endif
        return 0;
    }

    /* 如需要，显示数据进行调试 */
#ifdef CONFIG_MIFAREDEBUG
    ESP_LOGD(TAG, "页面 %d", page);
    esp_log_buffer_hex(TAG, buffer, 4);
#endif

    // 返回 OK 信号
    return 1;
}

/**************************************************************************/
/*!
 尝试在指定块地址写入整个 4 字节页面。

 @param  page          要写入的页面号。(大多数情况下为 0..63)
 @param  data          包含要写入数据的字节数组。
 应该正好是 4 字节长。

 @returns 1 如果一切执行正常，0 表示错误
 */
/**************************************************************************/
uint8_t ntag2xx_WritePage(uint8_t page, uint8_t* data)
{
    // 标签类型       页数   用户开始    用户结束
    // --------       -----   ----------    ---------
    // NTAG 203       42      4             39
    // NTAG 213       45      4             39
    // NTAG 215       135     4             129
    // NTAG 216       231     4             225

    if ((page < 4) || (page > 225))
    {
#ifdef CONFIG_MIFAREDEBUG
        ESP_LOGD(TAG, "页面值超出范围");
#endif
        // 返回失败信号
        return 0;
    }

#ifdef CONFIG_MIFAREDEBUG
    ESP_LOGD(TAG, "尝试写入 4 字节页面 %d", page);
#endif

    /* 准备第一个命令 */
    pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
    pn532_packetbuffer[1] = 1; /* 卡片编号 */
    pn532_packetbuffer[2] = MIFARE_ULTRALIGHT_CMD_WRITE; /* Mifare Ultralight 写入命令 = 0xA2 */
    pn532_packetbuffer[3] = page; /* 页面号 (大多数情况下为 0..63) */
    memcpy(pn532_packetbuffer + 4, data, 4); /* 数据载荷 */

    /* 发送命令 */
    if (!sendCommandCheckAck(pn532_packetbuffer, 8, I2C_WRITE_TIMEOUT))
    {
#ifdef CONFIG_MIFAREDEBUG
        ESP_LOGD(TAG, "写入命令未收到确认");
#endif

        // 返回失败信号
        return 0;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);

    /* 读取响应包 */
    readdata(pn532_packetbuffer, 26);

    // 返回 OK 信号
    return 1;
}

/**************************************************************************/
/*!
 从指定页面（4..nn）开始写入 NDEF URI 记录

 注意，此函数假定 NTAG2xx 卡
 已经格式化为用作 "NFC 论坛标签"。

 @param  uriIdentifier URI 标识符代码 (0 = 无, 0x01 =
 "http://www.", 等)
 @param  url           要写入的 uri 文本（空终止字符串）。
 @param  dataLen       数据区域的大小，用于溢出检查。

 @returns 1 如果一切执行正常，0 表示错误
 */
/**************************************************************************/
uint8_t ntag2xx_WriteNDEFURI(uint8_t uriIdentifier, char* url, uint8_t dataLen)
{
    uint8_t pageBuffer[4] =
        {0, 0, 0, 0};

    // 从 URI 数据中移除 NDEF 记录开销（参见下面的 pageHeader）
    uint8_t wrapperSize = 12;

    // 计算字符串长度
    uint8_t len = strlen(url);

    // 确保 URI 载荷可以适应 dataLen（包括 0xFE 尾部）
    if ((len < 1) || (len + 1 > (dataLen - wrapperSize))) return 0;

    // 设置记录头
    // 详情请参见 NFCForum-TS-Type-2-Tag_1.1.pdf
    uint8_t pageHeader[12] =
    {
        /* NDEF 锁定控制 TLV（必须是第一个且始终存在） */
        0x01, /* 标签字段（0x01 = 锁定控制 TLV） */
        0x03, /* 载荷长度（始终为 3） */
        0xA0, /* 标签内锁定字节的位置（高 4 位 = 页地址，低 4 位 = 字节偏移） */
        0x10, /* 锁定区域的位大小 */
        0x44, /* 页面的字节大小和每个锁定位可以锁定的字节数（4 位 + 4 位） */
        /* NDEF 消息 TLV - URI 记录 */
        0x03, /* 标签字段（0x03 = NDEF 消息） */
        len + 5, /* 载荷长度（不包括 0xFE 尾部） */
        0xD1, /* NDEF 记录头（TNF=0x1：知名记录 + SR + ME + MB） */
        0x01, /* 记录类型指示符的类型长度 */
        len + 1, /* 载荷长度 */
        0x55, /* 记录类型指示符（0x55 或 'U' = URI 记录） */
        uriIdentifier /* URI 前缀（例如 0x01 = "http://www."） */
    };

    // 写入 12 字节头（从第 4 页开始的三个数据页）
    memcpy(pageBuffer, pageHeader, 4);
    if (!(ntag2xx_WritePage(4, pageBuffer))) return 0;
    memcpy(pageBuffer, pageHeader + 4, 4);
    if (!(ntag2xx_WritePage(5, pageBuffer))) return 0;
    memcpy(pageBuffer, pageHeader + 8, 4);
    if (!(ntag2xx_WritePage(6, pageBuffer))) return 0;

    // 写入 URI（从第 7 页开始）
    uint8_t currentPage = 7;
    char* urlcopy = url;
    while (len)
    {
        if (len < 4)
        {
            memset(pageBuffer, 0, 4);
            memcpy(pageBuffer, urlcopy, len);
            pageBuffer[len] = 0xFE; // NDEF 记录尾部
            if (!(ntag2xx_WritePage(currentPage, pageBuffer))) return 0;
            // 完成！
            return 1;
        }
        else if (len == 4)
        {
            memcpy(pageBuffer, urlcopy, len);
            if (!(ntag2xx_WritePage(currentPage, pageBuffer))) return 0;
            memset(pageBuffer, 0, 4);
            pageBuffer[0] = 0xFE; // NDEF 记录尾部
            currentPage++;
            if (!(ntag2xx_WritePage(currentPage, pageBuffer))) return 0;
            // 完成！
            return 1;
        }
        else
        {
            // 还有多于一页的数据
            memcpy(pageBuffer, urlcopy, 4);
            if (!(ntag2xx_WritePage(currentPage, pageBuffer))) return 0;
            currentPage++;
            urlcopy += 4;
            len -= 4;
        }
    }

    // 看起来一切都正常 (?!)
    return 1;
}
