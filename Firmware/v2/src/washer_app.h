#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "kk_ui.h"

/** 应用页面描述（洗衣菜单 + 关于页），供 KK_UI_Init 使用。 */
const KK_UI_App *KK_UI_AppGet(void);

/** 复位应用状态（亮度等），now_ms 为上电毫秒时基。 */
void KK_UI_AppReset(uint32_t now_ms);

/** 周期性应用状态刷新（当前无动态数据，保留接口与 Demo 对齐）。 */
void KK_UI_AppUpdate(uint32_t now_ms);

/** 取空 KK_UI 事件队列：发送串口指令、应用对比度、弹 Toast。 */
void KK_UI_AppProcessEvents(void);

#ifdef __cplusplus
}
#endif
