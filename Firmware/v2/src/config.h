#pragma once

#include <stdint.h>

/*
 * v2 硬件接线与通信参数的唯一事实来源，与 README 接线表保持一致。
 */

/** NodeMCU 引脚：D1/GPIO5 接 OLED SCL（ESP8266 Wire 默认 I2C 引脚）。 */
#define PIN_OLED_SCL 5U
/** NodeMCU 引脚：D2/GPIO4 接 OLED SDA。 */
#define PIN_OLED_SDA 4U

/** NodeMCU 引脚：D5/GPIO14 接编码器 A 相（CLK）。 */
#define PIN_ENCODER_A 14U
/** NodeMCU 引脚：D6/GPIO12 接编码器 B 相（DT）。 */
#define PIN_ENCODER_B 12U
/** NodeMCU 引脚：D7/GPIO13 接编码器按键（SW），按下为低电平。 */
#define PIN_ENCODER_BUTTON 13U

/** 与洗衣机控制板通信的串口波特率（UART0：TX=GPIO1，RX=GPIO0）。 */
#define WASHER_BAUD_RATE 2400UL

/** 6 元加强洗两段指令之间的间隔。 */
#define WASHER_FRAME_GAP_MS 100U
