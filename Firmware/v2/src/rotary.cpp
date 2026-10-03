#include "rotary.h"

#include <Arduino.h>

#include "config.h"

namespace rotary {
namespace {

/*
 * EC11 类编码器每个机械档位经历一个完整的四状态正交循环。
 * 以 (A<<1)|B 为状态编码，查 16 项跳变表：合法的单比特跳变按方向记 ±1，
 * 双比特跳变（毛刺）记 0，一个档位四个跳变的符号一致，累加恰好 ±4。
 */
const int8_t STEP_TABLE[16] = {
    0, -1, 1, 0,
    1, 0, 0, -1,
    -1, 0, 0, 1,
    0, 1, -1, 0,
};

volatile int32_t raw_delta = 0; /**< ISR 累计的原始跳变值，4 的倍数为整档。 */

void IRAM_ATTR encoder_isr()
{
    static uint8_t previous = 0;
    uint8_t current = (uint8_t)((digitalRead(PIN_ENCODER_A) << 1) |
                                digitalRead(PIN_ENCODER_B));
    raw_delta += STEP_TABLE[((previous << 2) | current)];
    previous = current;
}

}  // namespace

void begin()
{
    pinMode(PIN_ENCODER_A, INPUT_PULLUP);
    pinMode(PIN_ENCODER_B, INPUT_PULLUP);
    pinMode(PIN_ENCODER_BUTTON, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A), encoder_isr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_B), encoder_isr, CHANGE);
}

int16_t take_steps()
{
    int32_t snapshot;
    noInterrupts();
    snapshot = raw_delta;
    raw_delta = 0;
    interrupts();
    return (int16_t)(snapshot / 4); /* 不足一档的残留抖动直接丢弃。 */
}

bool ok_held()
{
    return digitalRead(PIN_ENCODER_BUTTON) == LOW;
}

}  // namespace rotary
