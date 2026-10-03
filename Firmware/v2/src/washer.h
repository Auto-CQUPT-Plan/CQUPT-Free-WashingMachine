#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 预置洗衣模式数量。 */
#define WASHER_MODE_COUNT 5U

/**
 * 按波特率初始化与洗衣机控制板相连的 UART0。
 */
bool washer_begin(unsigned long baud_rate);

/**
 * 发送第 index 个预置洗衣模式的串口指令帧，阻塞直到发送完成。
 * 6 元大件洗会间隔 FRAME_GAP_MS 连发两段指令。
 */
bool washer_send_mode(uint8_t index);

#ifdef __cplusplus
}
#endif
