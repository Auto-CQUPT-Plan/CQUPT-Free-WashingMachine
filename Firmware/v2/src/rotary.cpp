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

volatile int32_t raw_delta = 0; /**< ISR 累计的原始跳变值，整档为 4 的倍数。 */
uint8_t previous_state = 0;     /**< 上次采样的 (A<<1)|B 状态，begin() 里对齐初值。 */

void IRAM_ATTR encoder_isr()
{
    uint8_t current = (uint8_t)((digitalRead(PIN_ENCODER_A) << 1) |
                                digitalRead(PIN_ENCODER_B));
    raw_delta += STEP_TABLE[((previous_state << 2) | current)];
    previous_state = current;
}

}  // namespace

void begin()
{
    pinMode(PIN_ENCODER_A, INPUT_PULLUP);
    pinMode(PIN_ENCODER_B, INPUT_PULLUP);
    pinMode(PIN_ENCODER_BUTTON, INPUT_PULLUP);
    /* 先对齐停位状态再开中断，避免上电第一个档位计入虚假跳变。 */
    previous_state = (uint8_t)((digitalRead(PIN_ENCODER_A) << 1) |
                               digitalRead(PIN_ENCODER_B));
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A), encoder_isr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_B), encoder_isr, CHANGE);
}

int16_t take_steps()
{
    int32_t consumed;
    noInterrupts();
    /*
     * 主循环全速空转，两次调用之间往往只到账 1~3 个跳变；
     * 只消费整档（4 的倍数），残量留给下次，否则每个跳变都会被截断丢掉。
     */
    consumed = raw_delta - raw_delta % 4;
    raw_delta -= consumed;
    interrupts();
    return (int16_t)(consumed / 4);
}

bool ok_held()
{
    return digitalRead(PIN_ENCODER_BUTTON) == LOW;
}

}  // namespace rotary
