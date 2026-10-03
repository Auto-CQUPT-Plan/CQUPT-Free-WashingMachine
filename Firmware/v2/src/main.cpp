#include <Arduino.h>

#include "config.h"
#include "generated/kk_app_font.h"
#include "kk_oled.h"
#include "rotary.h"
#include "washer.h"
#include "washer_app.h"

#include "kk_ui.h"

namespace {

/** 上电画面：店铺名 + 版本，居中绘制后交还 KK_UI 接管刷新。 */
void draw_splash()
{
    const char *title = "樱花洗衣券铺";
    const char *subtitle = "CQUPT v2.0.0";

    OLED_Clear();
    OLED_SetFont(kk_font_app_title);
    OLED_DrawUTF8((int16_t)((OLED_GetWidth() - OLED_GetUTF8Width(title)) / 2),
                  18, title);
    OLED_SetFont(kk_font_app_body);
    OLED_DrawUTF8((int16_t)((OLED_GetWidth() - OLED_GetUTF8Width(subtitle)) / 2),
                  44, subtitle);
    OLED_Update();
}

/** OLED 初始化失败（未接线/地址不对）时慢闪板载 LED 提示。 */
[[noreturn]] void blink_error()
{
    pinMode(LED_BUILTIN, OUTPUT);
    while (true) {
        digitalWrite(LED_BUILTIN, LOW);
        delay(120);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(380);
    }
}

}  // namespace

void setup()
{
    washer_begin(WASHER_BAUD_RATE);
    rotary::begin();

    if (OLED_Init() != OLED_OK) {
        blink_error();
    }
    draw_splash();
    delay(900);

    KK_UI_AppReset(millis());
    KK_UI_Init(KK_UI_AppGet());
}

void loop()
{
    uint32_t now = millis();
    KK_UI_Input input = {};

    input.encoder_delta = rotary::take_steps();
    if (rotary::ok_held()) {
        input.keys = KK_UI_KEY_OK;
    }

    KK_UI_AppUpdate(now);
    KK_UI_Update(now, input);
    KK_UI_AppProcessEvents();
}
