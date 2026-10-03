#include "kk_ui_internal.h"
#include "kk_ui_draw.h"

#if KK_UI_ENABLE_INFO

static const KK_UI_InfoPage *kk_ui_info_page(const KK_UI_PageState *state)
{
    const KK_UI_PageRoute *route = KK_UI_GetRoute(state->page);
    return &kk_ui.app->info_pages[route->index];
}

static uint16_t kk_ui_info_header_height(const KK_UI_InfoPage *page)
{
    return page->title != NULL && page->title[0] != '\0' ?
           KK_UI_INFO_HEADER_HEIGHT : 0U;
}

static int32_t kk_ui_info_max_scroll_q8(const KK_UI_InfoPage *page)
{
    uint16_t header = kk_ui_info_header_height(page);
    int32_t content = (int32_t)page->row_count * KK_UI_ROW_HEIGHT;
    int32_t viewport = KK_UI_SCREEN_HEIGHT - header;
    return content > viewport ? (content - viewport) * 256 : 0;
}

static int32_t kk_ui_info_scroll(uint32_t now)
{
    if (kk_ui.list_anim.active != 0U) {
        uint16_t p = KK_UI_EaseQ12(now - kk_ui.list_anim.started,
                                   kk_ui.list_anim.duration);
        return KK_UI_LerpQ12(kk_ui.list_anim.scroll_from_q8,
                            kk_ui.current.scroll_q8, p);
    }
    return kk_ui.current.scroll_q8;
}

void KK_UI_InfoAnimate(uint32_t now)
{
    if (KK_UI_CurrentPageType() != KK_UI_PAGE_INFO ||
        kk_ui.list_anim.active == 0U) {
        return;
    }
    kk_ui.dirty = 1U;
    if (now - kk_ui.list_anim.started >= kk_ui.list_anim.duration) {
        kk_ui.list_anim.active = 0U;
    }
}

static void kk_ui_info_move(int16_t direction, uint16_t steps, uint32_t now)
{
    const KK_UI_InfoPage *page = kk_ui_info_page(&kk_ui.current);
    int32_t current = kk_ui_info_scroll(now);
    int32_t target = kk_ui.current.scroll_q8;
    int32_t maximum = kk_ui_info_max_scroll_q8(page);
    int32_t amount;

    if (steps == 0U || maximum == 0) {
        return;
    }
    amount = (int32_t)steps * KK_UI_ROW_HEIGHT * 256;
    if (direction < 0) {
        target = target > amount ? target - amount : 0;
    } else {
        target = maximum - target > amount ? target + amount : maximum;
    }
    if (target == kk_ui.current.scroll_q8) {
        return;
    }
    kk_ui.current.scroll_q8 = target;
    kk_ui.current.top = (uint16_t)(target / (KK_UI_ROW_HEIGHT * 256));
    kk_ui.list_anim.scroll_from_q8 = current;
    kk_ui.list_anim.focus_from_q8 = 0;
    kk_ui.list_anim.started = now;
    kk_ui.list_anim.duration = KK_UI_INFO_SCROLL_MS;
    kk_ui.list_anim.active = 1U;
    kk_ui.dirty = 1U;
}

void KK_UI_InfoInput(KK_UI_InputEvent event, uint32_t now)
{
    if (event.action == KK_UI_INPUT_OK) {
        (void)KK_UI_NavigateBack(now);
    } else if (event.action == KK_UI_INPUT_UP) {
        kk_ui_info_move(-1, event.steps, now);
    } else {
        kk_ui_info_move(1, event.steps, now);
    }
}

static void kk_ui_info_close_icon(int16_t x_offset)
{
    OLED_DrawRBox((int16_t)(114 + x_offset), 1, 12U, 12U, 2U);
    OLED_SetDrawMode(OLED_DRAW_CLEAR);
    OLED_DrawLine((int16_t)(117 + x_offset), 4,
                  (int16_t)(122 + x_offset), 9);
    OLED_DrawLine((int16_t)(122 + x_offset), 4,
                  (int16_t)(117 + x_offset), 9);
    OLED_SetDrawMode(OLED_DRAW_SET);
}

void KK_UI_InfoDraw(const KK_UI_PageState *state, int16_t x_offset,
                    int16_t clip_x, uint16_t clip_width)
{
    const KK_UI_InfoPage *page = kk_ui_info_page(state);
    uint16_t header = kk_ui_info_header_height(page);
    int32_t scroll = state->scroll_q8;
    uint16_t i;
    uint16_t visible_rows = (uint16_t)((KK_UI_SCREEN_HEIGHT - header) /
                                      KK_UI_ROW_HEIGHT);
    int16_t clip_right = (int16_t)(clip_x + clip_width);
    int16_t content_left = clip_x;
    int16_t content_right = clip_right;
    bool draw_content = true;

    if (state == &kk_ui.current && kk_ui.list_anim.active != 0U) {
        scroll = kk_ui_info_scroll(kk_ui.last_update);
    }
    if (header != 0U) {
        OLED_SetFont(kk_ui.app->fonts.title_font);
        OLED_SetClipWindow(clip_x, 0, clip_width, header);
        KK_UI_DrawCentered(x_offset, 1, KK_UI_SCREEN_WIDTH, page->title);
        OLED_DrawHLine(x_offset, (int16_t)(header - 1U), 125U);
        kk_ui_info_close_icon(x_offset);
    }
    OLED_SetFont(kk_ui.app->fonts.body_font);
    {
        if (content_right > x_offset + 125) content_right = (int16_t)(x_offset + 125);
        if (content_left < x_offset) content_left = x_offset;
        if (content_right > content_left) {
            OLED_SetClipWindow(content_left, (int16_t)header,
                               (uint16_t)(content_right - content_left),
                               (uint16_t)(KK_UI_SCREEN_HEIGHT - header));
        } else {
            draw_content = false;
        }
    }
    for (i = 0U; i < page->row_count; ++i) {
        int16_t y = (int16_t)(header + i * KK_UI_ROW_HEIGHT -
                              KK_UI_RoundQ8(scroll));
        const char *value = page->rows[i].value;
        if (!draw_content || y >= KK_UI_SCREEN_HEIGHT ||
            y + KK_UI_ROW_HEIGHT <= header) {
            continue;
        }
        if (value != NULL && value[0] != '\0') {
            int16_t value_x = (int16_t)(121 - OLED_GetUTF8Width(value) +
                                        x_offset);
            int16_t name_left = (int16_t)(6 + x_offset);
            int16_t name_right = (int16_t)(value_x - 2);
            if (name_left < content_left) name_left = content_left;
            if (name_right > content_right) name_right = content_right;
            if (name_right > name_left) {
                OLED_SetClipWindow(name_left, (int16_t)header,
                                   (uint16_t)(name_right - name_left),
                                   (uint16_t)(KK_UI_SCREEN_HEIGHT - header));
                OLED_DrawUTF8((int16_t)(6 + x_offset), y,
                              page->rows[i].name);
            }
            OLED_SetClipWindow(content_left, (int16_t)header,
                               (uint16_t)(content_right - content_left),
                               (uint16_t)(KK_UI_SCREEN_HEIGHT - header));
            OLED_DrawUTF8(value_x, y, value);
        } else {
            OLED_DrawUTF8((int16_t)(6 + x_offset), y,
                          page->rows[i].name);
        }
    }
    OLED_SetClipWindow(clip_x, 0, clip_width, KK_UI_SCREEN_HEIGHT);
    if (page->row_count > visible_rows && x_offset + 126 >= clip_x &&
        x_offset + 126 < clip_right) {
        uint16_t viewport = (uint16_t)(KK_UI_SCREEN_HEIGHT - header);
        uint16_t thumb_height = (uint16_t)((uint32_t)viewport * viewport /
                                  (page->row_count * KK_UI_ROW_HEIGHT));
        int32_t maximum = kk_ui_info_max_scroll_q8(page) / 256;
        int32_t scroll_pixels = KK_UI_RoundQ8(scroll);
        uint16_t thumb_y;
        if (thumb_height < 8U) thumb_height = 8U;
        thumb_y = (uint16_t)(header +
            ((int32_t)viewport - thumb_height) * scroll_pixels / maximum);
        OLED_DrawBox((int16_t)(126 + x_offset), (int16_t)thumb_y, 2U,
                     thumb_height);
    }
    OLED_ResetClipWindow();
}

#else
void KK_UI_InfoAnimate(uint32_t now) { (void)now; }
void KK_UI_InfoInput(KK_UI_InputEvent event, uint32_t now)
{ (void)event; (void)now; }
void KK_UI_InfoDraw(const KK_UI_PageState *state, int16_t x_offset,
                    int16_t clip_x, uint16_t clip_width)
{ (void)state; (void)x_offset; (void)clip_x; (void)clip_width; }
#endif

void KK_UI_CustomInput(KK_UI_InputEvent event)
{
#if KK_UI_ENABLE_CUSTOM
    kk_ui.in_callback = 1U;
    KK_UI_CustomOnInput(kk_ui.current.page, event);
    kk_ui.in_callback = 0U;
#else
    (void)event;
#endif
}

void KK_UI_CustomTick(uint32_t now)
{
#if KK_UI_ENABLE_CUSTOM
    if (KK_UI_CurrentPageType() == KK_UI_PAGE_CUSTOM &&
        kk_ui.transition.active == 0U) {
        bool redraw;
        kk_ui.in_callback = 1U;
        redraw = KK_UI_CustomOnTick(kk_ui.current.page, now);
        kk_ui.in_callback = 0U;
        if (redraw) {
            kk_ui.dirty = 1U;
        }
    }
#else
    (void)now;
#endif
}
