#include "kk_ui_internal.h"
#include "kk_ui_draw.h"

#if KK_UI_ENABLE_MENU

static const KK_UI_MenuPage *kk_ui_menu_page(const KK_UI_PageState *state)
{
    const KK_UI_PageRoute *route = KK_UI_GetRoute(state->page);
    return &kk_ui.app->menu_pages[route->index];
}

KK_UI_ItemState KK_UI_MenuGetItemState(const KK_UI_MenuPage *page,
                                       uint16_t item_index)
{
    uint8_t packed;
    if (page->item_states == NULL) {
        return KK_UI_ITEM_NORMAL;
    }
    packed = page->item_states[item_index >> 2U];
    return (KK_UI_ItemState)((packed >> ((item_index & 3U) * 2U)) & 3U);
}

static uint16_t kk_ui_menu_visible_count(const KK_UI_MenuPage *page)
{
    uint16_t count = 1U; /* Virtual Return. */
    uint16_t i;
    for (i = 0U; i < page->item_count; ++i) {
        if (KK_UI_MenuGetItemState(page, i) != KK_UI_ITEM_HIDDEN) {
            ++count;
        }
    }
    return count;
}

static uint16_t kk_ui_menu_selectable_count(const KK_UI_MenuPage *page)
{
    uint16_t count = 1U;
    uint16_t i;
    for (i = 0U; i < page->item_count; ++i) {
        if (KK_UI_MenuGetItemState(page, i) == KK_UI_ITEM_NORMAL) {
            ++count;
        }
    }
    return count;
}

static uint16_t kk_ui_menu_visible_to_item(const KK_UI_MenuPage *page,
                                           uint16_t visible)
{
    uint16_t i;
    for (i = 0U; i < page->item_count; ++i) {
        if (KK_UI_MenuGetItemState(page, i) == KK_UI_ITEM_HIDDEN) {
            continue;
        }
        if (visible == 0U) {
            return i;
        }
        --visible;
    }
    return page->item_count;
}

static uint16_t kk_ui_menu_item_to_visible(const KK_UI_MenuPage *page,
                                           uint16_t item)
{
    uint16_t visible = 0U;
    uint16_t i;
    for (i = 0U; i < page->item_count; ++i) {
        if (i == item) {
            return visible;
        }
        if (KK_UI_MenuGetItemState(page, i) != KK_UI_ITEM_HIDDEN) {
            ++visible;
        }
    }
    return visible;
}

static bool kk_ui_menu_item_selectable(const KK_UI_MenuPage *page,
                                       uint16_t item)
{
    return item == page->item_count ||
           KK_UI_MenuGetItemState(page, item) == KK_UI_ITEM_NORMAL;
}

void KK_UI_MenuRepairFocus(KK_UI_PageState *state)
{
    const KK_UI_MenuPage *page = kk_ui_menu_page(state);
    uint16_t original;
    uint16_t i;
    uint16_t visible;

    if (state->selected > page->item_count) {
        state->selected = page->item_count;
    }
    if (kk_ui_menu_item_selectable(page, state->selected)) {
        visible = kk_ui_menu_item_to_visible(page, state->selected);
    } else {
        original = state->selected;
        state->selected = page->item_count;
        for (i = (uint16_t)(original + 1U); i < page->item_count; ++i) {
            if (kk_ui_menu_item_selectable(page, i)) {
                state->selected = i;
                break;
            }
        }
        if (state->selected == page->item_count) {
            i = original;
            while (i > 0U) {
                --i;
                if (kk_ui_menu_item_selectable(page, i)) {
                    state->selected = i;
                    break;
                }
            }
        }
        visible = kk_ui_menu_item_to_visible(page, state->selected);
    }
    if (visible < state->top) {
        state->top = visible;
    } else if (visible >= state->top + KK_UI_VISIBLE_ROWS) {
        state->top = (uint16_t)(visible - (KK_UI_VISIBLE_ROWS - 1U));
    }
    {
        uint16_t count = kk_ui_menu_visible_count(page);
        uint16_t max_top = count > KK_UI_VISIBLE_ROWS ?
                           (uint16_t)(count - KK_UI_VISIBLE_ROWS) : 0U;
        if (state->top > max_top) {
            state->top = max_top;
        }
    }
    state->scroll_q8 = (int32_t)state->top * KK_UI_ROW_HEIGHT * 256;
    state->focus_q8 = (int32_t)(visible - state->top) *
                      KK_UI_ROW_HEIGHT * 256;
}

static void kk_ui_menu_animated_values(uint32_t now, int32_t *scroll,
                                       int32_t *focus)
{
    if (kk_ui.list_anim.active != 0U) {
        uint16_t p = KK_UI_EaseQ12(now - kk_ui.list_anim.started,
                                   kk_ui.list_anim.duration);
        *scroll = KK_UI_LerpQ12(kk_ui.list_anim.scroll_from_q8,
                               kk_ui.current.scroll_q8, p);
        *focus = KK_UI_LerpQ12(kk_ui.list_anim.focus_from_q8,
                              kk_ui.current.focus_q8, p);
    } else {
        *scroll = kk_ui.current.scroll_q8;
        *focus = kk_ui.current.focus_q8;
    }
}

void KK_UI_MenuAnimate(uint32_t now)
{
    if (KK_UI_CurrentPageType() != KK_UI_PAGE_MENU ||
        kk_ui.list_anim.active == 0U) {
        return;
    }
    kk_ui.dirty = 1U;
    if (now - kk_ui.list_anim.started >= kk_ui.list_anim.duration) {
        kk_ui.list_anim.active = 0U;
    }
}

static void kk_ui_menu_move(int16_t direction, uint16_t steps, uint32_t now)
{
    const KK_UI_MenuPage *page = kk_ui_menu_page(&kk_ui.current);
    uint16_t count = kk_ui_menu_visible_count(page);
    uint16_t selectable = kk_ui_menu_selectable_count(page);
    uint16_t visible = kk_ui_menu_item_to_visible(page, kk_ui.current.selected);
    uint16_t remaining;
    int32_t old_scroll;
    int32_t old_focus;

    if (steps == 0U || selectable == 0U) {
        return;
    }
    kk_ui_menu_animated_values(now, &old_scroll, &old_focus);
    remaining = (uint16_t)(steps % selectable);
    if (remaining == 0U) {
        return;
    }
    while (remaining != 0U) {
        uint16_t item;
        do {
            if (direction < 0) {
                visible = visible == 0U ? (uint16_t)(count - 1U) :
                          (uint16_t)(visible - 1U);
            } else {
                visible = (uint16_t)((visible + 1U) % count);
            }
            item = kk_ui_menu_visible_to_item(page, visible);
        } while (!kk_ui_menu_item_selectable(page, item));
        kk_ui.current.selected = item;
        --remaining;
    }
    if (visible < kk_ui.current.top) {
        kk_ui.current.top = visible;
    } else if (visible >= kk_ui.current.top + KK_UI_VISIBLE_ROWS) {
        kk_ui.current.top = (uint16_t)(visible - (KK_UI_VISIBLE_ROWS - 1U));
    }
    {
        uint16_t max_top = count > KK_UI_VISIBLE_ROWS ?
                           (uint16_t)(count - KK_UI_VISIBLE_ROWS) : 0U;
        if (kk_ui.current.top > max_top) {
            kk_ui.current.top = max_top;
        }
    }
    kk_ui.current.scroll_q8 = (int32_t)kk_ui.current.top * 4096;
    kk_ui.current.focus_q8 = (int32_t)(visible - kk_ui.current.top) * 4096;
    kk_ui.list_anim.scroll_from_q8 = old_scroll;
    kk_ui.list_anim.focus_from_q8 = old_focus;
    kk_ui.list_anim.started = now;
    kk_ui.list_anim.duration = KK_UI_MENU_MS;
    kk_ui.list_anim.active = 1U;
    kk_ui.dirty = 1U;
}

static void kk_ui_menu_reject(KK_UI_Status status, uint16_t index)
{
    KK_UI_RecordError(status, kk_ui.current.page, index);
    kk_ui.reject_flash = 2U;
    kk_ui.dirty = 1U;
}

static void kk_ui_menu_activate(uint32_t now)
{
    const KK_UI_MenuPage *page = kk_ui_menu_page(&kk_ui.current);
    const KK_UI_MenuItem *item;
    KK_UI_Status status = KK_UI_OK;

    if (kk_ui.current.selected == page->item_count) {
        (void)KK_UI_NavigateBack(now);
        return;
    }
    item = &page->items[kk_ui.current.selected];
    switch (item->type) {
    case KK_UI_MENU_PAGE:
        status = KK_UI_NavigateTo((KK_UI_PageId)item->ref, now);
        break;
    case KK_UI_MENU_ACTION:
        if (!KK_UI_QueueEvent((KK_UI_EventId)item->ref)) {
            status = KK_UI_QUEUE_FULL;
        }
        break;
    case KK_UI_MENU_INT:
        status = KK_UI_DialogOpenInt(item->ref, now);
        break;
    case KK_UI_MENU_BOOL:
        status = KK_UI_DialogOpenBool(item->ref, now);
        break;
    case KK_UI_MENU_CONFIRM:
        status = KK_UI_DialogOpenConfirm(item->ref, now);
        break;
    default:
        status = KK_UI_CONFIG_ERROR;
        break;
    }
    if (status != KK_UI_OK && status != KK_UI_BUSY) {
        kk_ui_menu_reject(status, kk_ui.current.selected);
    }
}

void KK_UI_MenuInput(KK_UI_InputEvent event, uint32_t now)
{
    if (event.action == KK_UI_INPUT_OK) {
        kk_ui_menu_activate(now);
    } else if (event.action == KK_UI_INPUT_UP) {
        kk_ui_menu_move(-1, event.steps, now);
    } else {
        kk_ui_menu_move(1, event.steps, now);
    }
}

static void kk_ui_draw_enter_arrow(int16_t x, int16_t y, bool back)
{
    int16_t point = (int16_t)(x + (back ? -3 : 3));
    OLED_DrawLine(x, (int16_t)(y + 5), point, (int16_t)(y + 8));
    OLED_DrawLine(point, (int16_t)(y + 8), x, (int16_t)(y + 11));
}

static void kk_ui_draw_disabled(int16_t x, int16_t y)
{
    OLED_DrawCircle(x, y, 4U);
    OLED_DrawLine((int16_t)(x - 3), (int16_t)(y + 3),
                  (int16_t)(x + 3), (int16_t)(y - 3));
}

static void kk_ui_menu_value(const KK_UI_MenuItem *item, char *buffer,
                             size_t capacity)
{
    buffer[0] = '\0';
    if (item->type == KK_UI_MENU_INT) {
        const KK_UI_IntBinding *binding = &kk_ui.app->int_bindings[item->ref];
        KK_UI_FormatInt(*binding->value, binding->unit, buffer, capacity);
    } else if (item->type == KK_UI_MENU_BOOL) {
        const KK_UI_BoolBinding *binding = &kk_ui.app->bool_bindings[item->ref];
        const char *text = *binding->value ? kk_ui.app->texts.on_text :
                                             kk_ui.app->texts.off_text;
        size_t i = 0U;
        while (i + 1U < capacity && text[i] != '\0') {
            buffer[i] = text[i];
            ++i;
        }
        buffer[i] = '\0';
    }
}

static void kk_ui_menu_content_clip(int16_t left, int16_t right,
                                    int16_t clip_x, int16_t clip_right)
{
    if (left < clip_x) left = clip_x;
    if (right > clip_right) right = clip_right;
    if (right < left) right = left;
    OLED_SetClipWindow(left, KK_UI_MENU_HEADER_HEIGHT,
                       (uint16_t)(right - left),
                       KK_UI_VISIBLE_ROWS * KK_UI_ROW_HEIGHT);
}

void KK_UI_MenuDraw(const KK_UI_PageState *state, int16_t x_offset,
                    int16_t clip_x, uint16_t clip_width)
{
    const KK_UI_MenuPage *page = kk_ui_menu_page(state);
    int32_t scroll = state->scroll_q8;
    int32_t focus = state->focus_q8;
    uint16_t visible_count = kk_ui_menu_visible_count(page);
    uint16_t visible;
    int16_t clip_right = (int16_t)(clip_x + clip_width);

    if (state == &kk_ui.current && kk_ui.list_anim.active != 0U) {
        kk_ui_menu_animated_values(kk_ui.last_update, &scroll, &focus);
    }
    OLED_SetFont(kk_ui.app->fonts.title_font);
    OLED_SetClipWindow(clip_x, 0, clip_width, KK_UI_MENU_HEADER_HEIGHT);
    KK_UI_DrawCentered(x_offset, 1, KK_UI_SCREEN_WIDTH, page->title);

    OLED_SetFont(kk_ui.app->fonts.body_font);
    OLED_SetClipWindow(clip_x, KK_UI_MENU_HEADER_HEIGHT, clip_width,
                       KK_UI_VISIBLE_ROWS * KK_UI_ROW_HEIGHT);
    for (visible = 0U; visible < visible_count; ++visible) {
        uint16_t item_index = kk_ui_menu_visible_to_item(page, visible);
        int16_t y = (int16_t)(KK_UI_MENU_HEADER_HEIGHT + visible *
                              KK_UI_ROW_HEIGHT - scroll / 256);
        char value[20];

        if (y >= KK_UI_MENU_HEADER_HEIGHT + KK_UI_VISIBLE_ROWS *
                 KK_UI_ROW_HEIGHT || y + KK_UI_ROW_HEIGHT <=
            KK_UI_MENU_HEADER_HEIGHT) {
            continue;
        }
        if (item_index == page->item_count) {
            kk_ui_menu_content_clip((int16_t)(8 + x_offset),
                                    (int16_t)(106 + x_offset),
                                    clip_x, clip_right);
            OLED_DrawUTF8((int16_t)(8 + x_offset), y,
                          kk_ui.app->texts.return_text);
            OLED_SetClipWindow(clip_x, KK_UI_MENU_HEADER_HEIGHT, clip_width,
                               KK_UI_VISIBLE_ROWS * KK_UI_ROW_HEIGHT);
            kk_ui_draw_enter_arrow((int16_t)(110 + x_offset), y, true);
            continue;
        }
        value[0] = '\0';
        kk_ui_menu_value(&page->items[item_index], value, sizeof(value));
        if (value[0] != '\0') {
            int16_t right = KK_UI_MenuGetItemState(page, item_index) ==
                            KK_UI_ITEM_NORMAL ? 112 : 106;
            int16_t value_x = (int16_t)(right - OLED_GetUTF8Width(value) +
                                        x_offset);
            kk_ui_menu_content_clip((int16_t)(8 + x_offset),
                                    (int16_t)(value_x - 2),
                                    clip_x, clip_right);
            OLED_DrawUTF8((int16_t)(8 + x_offset), y,
                          page->items[item_index].label);
            OLED_SetClipWindow(clip_x, KK_UI_MENU_HEADER_HEIGHT, clip_width,
                               KK_UI_VISIBLE_ROWS * KK_UI_ROW_HEIGHT);
            OLED_DrawUTF8(value_x, y, value);
        } else if (page->items[item_index].type == KK_UI_MENU_PAGE) {
            kk_ui_menu_content_clip((int16_t)(8 + x_offset),
                                    (int16_t)(106 + x_offset),
                                    clip_x, clip_right);
            OLED_DrawUTF8((int16_t)(8 + x_offset), y,
                          page->items[item_index].label);
            OLED_SetClipWindow(clip_x, KK_UI_MENU_HEADER_HEIGHT, clip_width,
                               KK_UI_VISIBLE_ROWS * KK_UI_ROW_HEIGHT);
            kk_ui_draw_enter_arrow((int16_t)(110 + x_offset), y, false);
        } else {
            int16_t right = KK_UI_MenuGetItemState(page, item_index) ==
                            KK_UI_ITEM_NORMAL ? 121 : 110;
            kk_ui_menu_content_clip((int16_t)(8 + x_offset),
                                    (int16_t)(right + x_offset),
                                    clip_x, clip_right);
            OLED_DrawUTF8((int16_t)(8 + x_offset), y,
                          page->items[item_index].label);
            OLED_SetClipWindow(clip_x, KK_UI_MENU_HEADER_HEIGHT, clip_width,
                               KK_UI_VISIBLE_ROWS * KK_UI_ROW_HEIGHT);
        }
        if (KK_UI_MenuGetItemState(page, item_index) != KK_UI_ITEM_NORMAL) {
            kk_ui_draw_disabled((int16_t)(117 + x_offset),
                                (int16_t)(y + 8));
        }
    }
    OLED_SetDrawMode(OLED_DRAW_XOR);
    OLED_DrawRBox((int16_t)(3 + x_offset),
                  (int16_t)(KK_UI_MENU_HEADER_HEIGHT + focus / 256),
                  118U, KK_UI_ROW_HEIGHT, 3U);
    OLED_SetDrawMode(OLED_DRAW_SET);
    if (visible_count > KK_UI_VISIBLE_ROWS &&
        x_offset + 124 < clip_right && x_offset + 127 >= clip_x) {
        uint16_t first = (uint16_t)((scroll / 256) /
                                    KK_UI_ROW_HEIGHT);
        uint16_t thumb_height = (uint16_t)(48U * KK_UI_VISIBLE_ROWS /
                                           visible_count);
        uint16_t maximum_first = (uint16_t)(visible_count -
                                            KK_UI_VISIBLE_ROWS);
        uint16_t thumb_y = (uint16_t)(KK_UI_MENU_HEADER_HEIGHT +
            (48U - thumb_height) * first / maximum_first);
        OLED_DrawVLine((int16_t)(125 + x_offset), KK_UI_MENU_HEADER_HEIGHT,
                       48U);
        OLED_DrawBox((int16_t)(124 + x_offset), (int16_t)thumb_y, 3U,
                     thumb_height);
    }
    OLED_ResetClipWindow();
}

#else
KK_UI_ItemState KK_UI_MenuGetItemState(const KK_UI_MenuPage *page,
                                       uint16_t item_index)
{ (void)page; (void)item_index; return KK_UI_ITEM_NORMAL; }
void KK_UI_MenuRepairFocus(KK_UI_PageState *state) { (void)state; }
void KK_UI_MenuAnimate(uint32_t now) { (void)now; }
void KK_UI_MenuInput(KK_UI_InputEvent event, uint32_t now)
{ (void)event; (void)now; }
void KK_UI_MenuDraw(const KK_UI_PageState *state, int16_t x_offset,
                    int16_t clip_x, uint16_t clip_width)
{ (void)state; (void)x_offset; (void)clip_x; (void)clip_width; }
#endif
