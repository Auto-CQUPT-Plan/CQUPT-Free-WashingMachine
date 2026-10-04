#pragma once

#include <stdbool.h>
#include <stdint.h>

namespace rotary {

/** 配置编码器 A/B 相中断与按键引脚（均为 INPUT_PULLUP）。 */
void begin();

/**
 * 取出自上次调用以来编码器转过的档位数：顺时针（向下）为正。
 * 内部按四状态正交解码，一个机械档位计 1。
 */
int16_t take_steps();

/** 按键当前是否按住（低电平有效）；消抖由 KK_UI 处理。 */
bool ok_held();

}  // namespace rotary
