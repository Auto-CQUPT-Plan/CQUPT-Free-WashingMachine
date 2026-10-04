#include "washer.h"

#include <Arduino.h>

#include "config.h"

/*
 * 抓包自洗衣机原装投币控制器的启动帧：0xAA 帧头 + 长度 + 模式 + 校验。
 * 与 v1 固件（../v1/src/main.cpp）保持一致。
 */
namespace {

const uint8_t CMD_1YUAN[] = {0xAA, 0x06, 0x01, 0x98, 0x04, 0x00, 0x75, 0x03};
const uint8_t CMD_3YUAN[] = {0xAA, 0x06, 0x01, 0x98, 0x03, 0x01, 0xD0, 0xAC};
const uint8_t CMD_4YUAN[] = {0xAA, 0x06, 0x01, 0x98, 0x02, 0x02, 0xB2, 0x0D};
const uint8_t CMD_4YUAN_PLUS[] = {0xAA, 0x06, 0x01, 0x98, 0x02, 0x02, 0xB2, 0x0A};
const uint8_t CMD_6YUAN_1[] = {0xAA, 0x06, 0x01, 0x98, 0x01, 0x03, 0x51, 0xC6};
const uint8_t CMD_6YUAN_2[] = {0xAA, 0x06, 0x00, 0x9F, 0x01, 0x03, 0x09};

struct WasherMode {
    const uint8_t *frames[2]; /**< 依次发送的指令帧。 */
    uint8_t frame_count;      /**< 指令帧数量（1 或 2）。 */
};

const WasherMode MODES[WASHER_MODE_COUNT] = {
    {{CMD_1YUAN, nullptr}, 1},
    {{CMD_3YUAN, nullptr}, 1},
    {{CMD_4YUAN, nullptr}, 1},
    {{CMD_4YUAN_PLUS, nullptr}, 1},
    {{CMD_6YUAN_1, CMD_6YUAN_2}, 2},
};

}  // namespace

bool washer_begin(unsigned long baud_rate)
{
    Serial.begin(baud_rate);
    delay(50);
    return true;
}

bool washer_send_mode(uint8_t index)
{
    if (index >= WASHER_MODE_COUNT) {
        return false;
    }
    const WasherMode &mode = MODES[index];
    for (uint8_t i = 0; i < mode.frame_count; ++i) {
        Serial.write(mode.frames[i], 8U);
        Serial.flush();
        if (i + 1U < mode.frame_count) {
            delay(WASHER_FRAME_GAP_MS);
        }
    }
    return true;
}
