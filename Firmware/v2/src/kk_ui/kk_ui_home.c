#include "kk_ui_internal.h"

#if KK_UI_ENABLE_HOME

static const KK_UI_HomePage *kk_ui_home_page(const KK_UI_PageState *state)
{
    const KK_UI_PageRoute *route = KK_UI_GetRoute(state->page);
    return &kk_ui.app->home_pages[route->index];
}

static int16_t kk_ui_home_delta(uint16_t index, uint16_t selected,
                                uint16_t count)
{
    int32_t delta = (int32_t)index - selected;
    int32_t half = count / 2U;
    if (delta > half) {
        delta -= count;
    } else if (delta < -half) {
        delta += count;
    }
    return (int16_t)delta;
}

static int32_t kk_ui_home_label_width_q8(const KK_UI_HomePage *page,
                                         uint16_t selected)
{
    int32_t width;
    OLED_SetFont(kk_ui.app->fonts.home_font);
    width = (int32_t)OLED_GetUTF8Width(page->items[selected].label) + 12;
    if (width > 120) {
        width = 120;
    }
    width = (width + 1) & ~1;
    return width * 256;
}

static int16_t kk_ui_home_text_spacing(const KK_UI_HomePage *page)
{
    uint16_t maximum = 0U;
    uint16_t i;

    OLED_SetFont(kk_ui.app->fonts.home_font);
    for (i = 0U; i < page->item_count; ++i) {
        uint16_t width = (uint16_t)(OLED_GetUTF8Width(page->items[i].label) + 12U);
        if (width > maximum) {
            maximum = width;
        }
    }
    if (maximum > 120U) {
        maximum = 120U;
    }
    return (int16_t)(maximum + 4U);
}

static void kk_ui_home_values(uint32_t now, int32_t *offset_q8,
                              int32_t *label_q8)
{
    const KK_UI_HomePage *page = kk_ui_home_page(&kk_ui.current);
    if (kk_ui.home_anim.active != 0U) {
        uint16_t p = KK_UI_EaseQ12(now - kk_ui.home_anim.started, KK_UI_HOME_MS);
        *offset_q8 = KK_UI_LerpQ12(kk_ui.home_anim.offset_from_q8, 0, p);
        *label_q8 = KK_UI_LerpQ12(kk_ui.home_anim.label_from_q8,
                                 kk_ui.home_anim.label_to_q8, p);
    } else {
        *offset_q8 = 0;
        *label_q8 = kk_ui_home_label_width_q8(page, kk_ui.current.selected);
    }
}

void KK_UI_HomeAnimate(uint32_t now)
{
    if (KK_UI_CurrentPageType() != KK_UI_PAGE_HOME ||
        kk_ui.home_anim.active == 0U) {
        return;
    }
    kk_ui.dirty = 1U;
    if (now - kk_ui.home_anim.started >= KK_UI_HOME_MS) {
        kk_ui.home_anim.active = 0U;
    }
}

static void kk_ui_home_move(int16_t direction, uint16_t steps, uint32_t now)
{
    const KK_UI_HomePage *page = kk_ui_home_page(&kk_ui.current);
    int32_t current_offset;
    int32_t current_label;
    int32_t move;

    if (page->item_count == 0U || steps == 0U) {
        return;
    }
    kk_ui_home_values(now, &current_offset, &current_label);
    move = (int32_t)(steps % page->item_count);
    if (move == 0) {
        return;
    }
    if (direction < 0) {
        move = -move;
    }
    kk_ui.current.selected = (uint16_t)(((int32_t)kk_ui.current.selected + move +
                                (int32_t)page->item_count * 2) %
                               page->item_count);
    kk_ui.home_anim.offset_from_q8 = current_offset + direction * 64 * 256;
    kk_ui.home_anim.label_from_q8 = current_label;
    kk_ui.home_anim.label_to_q8 =
        kk_ui_home_label_width_q8(page, kk_ui.current.selected);
    kk_ui.home_anim.started = now;
    kk_ui.home_anim.active = 1U;
    kk_ui.dirty = 1U;
}

void KK_UI_HomeInput(KK_UI_InputEvent event, uint32_t now)
{
    const KK_UI_HomePage *page = kk_ui_home_page(&kk_ui.current);

    if (event.action == KK_UI_INPUT_OK) {
        KK_UI_PageId target = page->items[kk_ui.current.selected].target_page;
        kk_ui.home_anim.active = 0U;
        (void)KK_UI_NavigateTo(target, now);
    } else if (event.action == KK_UI_INPUT_UP) {
        kk_ui_home_move(-1, event.steps, now);
    } else {
        kk_ui_home_move(1, event.steps, now);
    }
}

void KK_UI_HomeDraw(const KK_UI_PageState *state, int16_t x_offset,
                    int16_t clip_x, uint16_t clip_width)
{
    const KK_UI_HomePage *page = kk_ui_home_page(state);
    int32_t icon_offset_q8 = 0;
    int32_t label_width_q8;
    int16_t icon_offset;
    int16_t label_width;
    int16_t label_left;
    int16_t text_spacing;
    int16_t text_offset;
    uint16_t i;
    int16_t clip_right = (int16_t)(clip_x + clip_width);

    OLED_SetClipWindow(clip_x, 0, clip_width, KK_UI_SCREEN_HEIGHT);
    OLED_SetFont(kk_ui.app->fonts.home_font);
    if (state == &kk_ui.current && kk_ui.home_anim.active != 0U) {
        uint16_t p = KK_UI_EaseQ12(kk_ui.last_update - kk_ui.home_anim.started,
                                   KK_UI_HOME_MS);
        icon_offset_q8 = KK_UI_LerpQ12(kk_ui.home_anim.offset_from_q8, 0, p);
        label_width_q8 = KK_UI_LerpQ12(kk_ui.home_anim.label_from_q8,
                                      kk_ui.home_anim.label_to_q8, p);
    } else {
        label_width_q8 = kk_ui_home_label_width_q8(page, state->selected);
    }
    icon_offset = KK_UI_RoundQ8(icon_offset_q8);
    label_width = KK_UI_RoundQ8(label_width_q8);
    text_spacing = kk_ui_home_text_spacing(page);
    text_offset = (int16_t)((int32_t)icon_offset * text_spacing / 64);

    for (i = 0U; i < page->item_count; ++i) {
        int16_t delta = kk_ui_home_delta(i, state->selected, page->item_count);
        int16_t x = (int16_t)(48 + delta * 64 + icon_offset + x_offset);
        if (x < clip_right && x + 32 > clip_x) {
            OLED_DrawXBM(x, 8, 32U, 32U, page->items[i].icon_xbm_32x32);
        }
    }

    label_left = (int16_t)(64 - label_width / 2 + x_offset);
    {
        int16_t left = label_left > clip_x ? label_left : clip_x;
        int16_t right = (int16_t)(label_left + label_width);
        if (right > clip_right) {
            right = clip_right;
        }
        if (right > left) {
            OLED_SetClipWindow(left, 44, (uint16_t)(right - left), 20U);
            for (i = 0U; i < page->item_count; ++i) {
                int16_t delta = kk_ui_home_delta(i, state->selected,
                                                 page->item_count);
                int16_t text_x = (int16_t)((KK_UI_SCREEN_WIDTH -
                    (int16_t)OLED_GetUTF8Width(page->items[i].label)) / 2 +
                    delta * text_spacing + text_offset + x_offset);
                OLED_DrawUTF8(text_x, 46, page->items[i].label);
            }
            OLED_SetDrawMode(OLED_DRAW_XOR);
            OLED_DrawRBox(label_left, 44, (uint16_t)label_width, 20U, 3U);
            OLED_SetDrawMode(OLED_DRAW_SET);
        }
    }
    OLED_ResetClipWindow();
}

#else
void KK_UI_HomeAnimate(uint32_t now) { (void)now; }
void KK_UI_HomeInput(KK_UI_InputEvent event, uint32_t now)
{ (void)event; (void)now; }
void KK_UI_HomeDraw(const KK_UI_PageState *state, int16_t x_offset,
                    int16_t clip_x, uint16_t clip_width)
{ (void)state; (void)x_offset; (void)clip_x; (void)clip_width; }
#endif
