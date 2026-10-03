#include "kk_ui_draw.h"
#include "kk_ui_config.h"

#include "kk_oled.h"

#include <stddef.h>

#if KK_UI_ENABLE_DRAW_HELPERS || KK_UI_ENABLE_BOOL_EDITOR

static void kk_ui_center_text(int16_t x, int16_t y, uint16_t width,
                              uint16_t height, const char *label)
{
    int16_t font_height;
    int16_t text_x;
    int16_t text_y;

    if (label == NULL) {
        return;
    }
    font_height = (int16_t)(OLED_GetFontAscent() - OLED_GetFontDescent());
    text_x = (int16_t)(x + ((int32_t)width - OLED_GetUTF8Width(label)) / 2);
    text_y = (int16_t)(y + ((int32_t)height - font_height) / 2);
    OLED_DrawUTF8(text_x, text_y, label);
}

void KK_UI_DrawFocus(int16_t x, int16_t y, uint16_t width, uint16_t height,
                     uint16_t radius)
{
    OLED_SetDrawMode(OLED_DRAW_XOR);
    OLED_DrawRBox(x, y, width, height, radius);
    OLED_SetDrawMode(OLED_DRAW_SET);
}

void KK_UI_DrawButton(int16_t x, int16_t y, uint16_t width, uint16_t height,
                      const char *label, bool focused)
{
    OLED_DrawRFrame(x, y, width, height, 3U);
    kk_ui_center_text(x, y, width, height, label);
    if (focused) {
        KK_UI_DrawFocus(x, y, width, height, 3U);
    }
}

void KK_UI_DrawProgressBar(int16_t x, int16_t y, uint16_t width,
                           uint16_t height, uint16_t value,
                           uint16_t maximum)
{
    uint16_t inner_width;
    uint16_t filled;

    if (width < 5U || height < 5U) {
        return;
    }
    OLED_DrawRFrame(x, y, width, height, height > 5U ? 2U : 1U);
    if (maximum == 0U || value == 0U) {
        return;
    }
    if (value > maximum) {
        value = maximum;
    }
    inner_width = (uint16_t)(width - 4U);
    filled = (uint16_t)(((uint32_t)inner_width * value + maximum / 2U) /
                        maximum);
    if (filled != 0U) {
        OLED_DrawRBox((int16_t)(x + 2), (int16_t)(y + 2), filled,
                      (uint16_t)(height - 4U), height > 7U ? 2U : 1U);
    }
}

void KK_UI_DrawScrollbar(int16_t x, int16_t y, uint16_t height,
                         uint16_t visible, uint16_t total,
                         uint16_t first_visible)
{
    uint16_t thumb_height;
    uint16_t thumb_y;
    uint16_t travel;
    uint16_t maximum_first;

    if (height == 0U || total <= visible || visible == 0U) {
        return;
    }
    thumb_height = (uint16_t)(((uint32_t)height * visible) / total);
    if (thumb_height < 8U) {
        thumb_height = 8U;
    }
    if (thumb_height > height) {
        thumb_height = height;
    }
    maximum_first = (uint16_t)(total - visible);
    if (first_visible > maximum_first) {
        first_visible = maximum_first;
    }
    travel = (uint16_t)(height - thumb_height);
    thumb_y = (uint16_t)(y + ((uint32_t)travel * first_visible) /
                         maximum_first);
    OLED_DrawBox(x, (int16_t)thumb_y, 2U, thumb_height);
}

void KK_UI_DrawSwitch(int16_t x, int16_t y, uint16_t width, uint16_t height,
                      bool on, uint16_t progress_q12)
{
    int16_t left;
    int16_t right;
    int16_t from;
    int16_t to;
    int16_t center_y;
    int16_t knob_x;
    uint16_t radius;

    if (width < 7U || height < 7U || width < height) {
        return;
    }
    if (progress_q12 > 4096U) {
        progress_q12 = 4096U;
    }
    radius = (uint16_t)(height / 2U);
    center_y = (int16_t)(y + (int16_t)(height / 2U));
    left = (int16_t)(x + (int16_t)(height / 2U));
    right = (int16_t)(x + (int16_t)width - (int16_t)(height / 2U));
    from = on ? left : right;
    to = on ? right : left;
    knob_x = (int16_t)(from + ((int32_t)(to - from) * progress_q12) / 4096);
    OLED_DrawRBox(x, y, width, height, radius);
    OLED_SetDrawMode(OLED_DRAW_CLEAR);
    OLED_DrawDisc(knob_x, center_y, radius > 2U ? radius - 2U : 1U);
    OLED_SetDrawMode(OLED_DRAW_SET);
}

#else

void KK_UI_DrawFocus(int16_t x, int16_t y, uint16_t width, uint16_t height,
                     uint16_t radius)
{ (void)x; (void)y; (void)width; (void)height; (void)radius; }
void KK_UI_DrawButton(int16_t x, int16_t y, uint16_t width, uint16_t height,
                      const char *label, bool focused)
{ (void)x; (void)y; (void)width; (void)height; (void)label; (void)focused; }
void KK_UI_DrawProgressBar(int16_t x, int16_t y, uint16_t width,
                           uint16_t height, uint16_t value, uint16_t maximum)
{ (void)x; (void)y; (void)width; (void)height; (void)value; (void)maximum; }
void KK_UI_DrawScrollbar(int16_t x, int16_t y, uint16_t height,
                         uint16_t visible, uint16_t total,
                         uint16_t first_visible)
{ (void)x; (void)y; (void)height; (void)visible; (void)total; (void)first_visible; }
void KK_UI_DrawSwitch(int16_t x, int16_t y, uint16_t width, uint16_t height,
                      bool on, uint16_t progress_q12)
{ (void)x; (void)y; (void)width; (void)height; (void)on; (void)progress_q12; }

#endif
