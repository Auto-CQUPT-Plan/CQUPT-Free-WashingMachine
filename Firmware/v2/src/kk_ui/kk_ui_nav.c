#include "kk_ui_internal.h"

const KK_UI_PageRoute *KK_UI_GetRoute(KK_UI_PageId page)
{
    if (kk_ui.app == NULL || page == KK_UI_PAGE_NONE ||
        page > kk_ui.app->route_count) {
        return NULL;
    }
    return &kk_ui.app->routes[page - 1U];
}

KK_UI_PageType KK_UI_CurrentPageType(void)
{
    const KK_UI_PageRoute *route = KK_UI_GetRoute(kk_ui.current.page);
    return route != NULL ? route->type : KK_UI_PAGE_HOME;
}

void KK_UI_ResetPageState(KK_UI_PageState *state, KK_UI_PageId page)
{
    const KK_UI_PageRoute *route;

    state->page = page;
    state->selected = 0U;
    state->top = 0U;
    state->scroll_q8 = 0;
    state->focus_q8 = 0;
    route = KK_UI_GetRoute(page);
    if (route != NULL && route->type == KK_UI_PAGE_MENU) {
        KK_UI_MenuRepairFocus(state);
    }
}

static void kk_ui_start_transition(const KK_UI_PageState *outgoing,
                                   bool reverse, uint32_t now)
{
    kk_ui.transition.outgoing = *outgoing;
    kk_ui.transition.started = now;
    kk_ui.transition.active = 1U;
    kk_ui.transition.reverse = reverse ? 1U : 0U;
    kk_ui.transition.leave_custom = 0U;
    kk_ui.blocked_keys |= (uint8_t)((kk_ui.keys[0].stable ? KK_UI_KEY_UP : 0U) |
                           (kk_ui.keys[1].stable ? KK_UI_KEY_DOWN : 0U) |
                           (kk_ui.keys[2].stable ? KK_UI_KEY_OK : 0U));
#if KK_UI_ENABLE_CUSTOM
    {
        const KK_UI_PageRoute *route = KK_UI_GetRoute(outgoing->page);
        if (route != NULL && route->type == KK_UI_PAGE_CUSTOM) {
            kk_ui.transition.leave_custom = 1U;
        }
    }
#endif
    kk_ui.dirty = 1U;
}

KK_UI_Status KK_UI_NavigateTo(KK_UI_PageId page, uint32_t now)
{
    const KK_UI_PageRoute *route = KK_UI_GetRoute(page);
    KK_UI_PageState outgoing;

    if (route == NULL) {
        return KK_UI_INVALID_ARGUMENT;
    }
    if (kk_ui.transition.active != 0U) {
        return KK_UI_BUSY;
    }
    if (kk_ui.stack_count >= KK_UI_NAV_DEPTH) {
        KK_UI_RecordError(KK_UI_NAVIGATION_FULL, kk_ui.current.page, page);
        kk_ui.reject_flash = 2U;
        kk_ui.dirty = 1U;
        return KK_UI_NAVIGATION_FULL;
    }
#if KK_UI_ENABLE_CUSTOM
    if (KK_UI_CurrentPageType() == KK_UI_PAGE_CUSTOM &&
        route->type == KK_UI_PAGE_CUSTOM) {
        return KK_UI_NOT_ALLOWED;
    }
#endif
    outgoing = kk_ui.current;
    kk_ui.stack[kk_ui.stack_count++] = outgoing;
    KK_UI_ResetPageState(&kk_ui.current, page);
    kk_ui.home_anim.active = 0U;
    kk_ui.list_anim.active = 0U;
    kk_ui_start_transition(&outgoing, false, now);
#if KK_UI_ENABLE_CUSTOM
    if (route->type == KK_UI_PAGE_CUSTOM) {
        kk_ui.in_callback = 1U;
        KK_UI_CustomOnEnter(page);
        kk_ui.in_callback = 0U;
    }
#endif
    return KK_UI_OK;
}

KK_UI_Status KK_UI_NavigateBack(uint32_t now)
{
    KK_UI_PageState outgoing;
    const KK_UI_PageRoute *route;

    if (kk_ui.transition.active != 0U) {
        return KK_UI_BUSY;
    }
    if (kk_ui.stack_count == 0U) {
        return KK_UI_NOT_ALLOWED;
    }
    outgoing = kk_ui.current;
    kk_ui.current = kk_ui.stack[--kk_ui.stack_count];
    if (KK_UI_CurrentPageType() == KK_UI_PAGE_MENU) {
        KK_UI_MenuRepairFocus(&kk_ui.current);
    }
    kk_ui.home_anim.active = 0U;
    kk_ui.list_anim.active = 0U;
    kk_ui_start_transition(&outgoing, true, now);
#if KK_UI_ENABLE_CUSTOM
    route = KK_UI_GetRoute(kk_ui.current.page);
    if (route != NULL && route->type == KK_UI_PAGE_CUSTOM) {
        kk_ui.in_callback = 1U;
        KK_UI_CustomOnEnter(kk_ui.current.page);
        kk_ui.in_callback = 0U;
    }
#else
    (void)route;
#endif
    return KK_UI_OK;
}

void KK_UI_AnimateNavigation(uint32_t now)
{
    if (kk_ui.transition.active != 0U) {
        uint32_t elapsed = now - kk_ui.transition.started;
        kk_ui.dirty = 1U;
        if (elapsed >= KK_UI_PAGE_MS) {
            kk_ui.transition.active = 0U;
#if KK_UI_ENABLE_CUSTOM
            if (kk_ui.transition.leave_custom != 0U) {
                kk_ui.in_callback = 1U;
                KK_UI_CustomOnLeave(kk_ui.transition.outgoing.page);
                kk_ui.in_callback = 0U;
            }
#endif
        }
    }
}

void KK_UI_DrawPage(const KK_UI_PageState *state, int16_t x_offset,
                    int16_t clip_x, uint16_t clip_width, uint32_t now)
{
    const KK_UI_PageRoute *route = KK_UI_GetRoute(state->page);
    (void)now;

    if (route == NULL || clip_width == 0U) {
        return;
    }
    switch (route->type) {
#if KK_UI_ENABLE_HOME
    case KK_UI_PAGE_HOME:
        KK_UI_HomeDraw(state, x_offset, clip_x, clip_width);
        break;
#endif
#if KK_UI_ENABLE_MENU
    case KK_UI_PAGE_MENU:
        KK_UI_MenuDraw(state, x_offset, clip_x, clip_width);
        break;
#endif
#if KK_UI_ENABLE_INFO
    case KK_UI_PAGE_INFO:
        KK_UI_InfoDraw(state, x_offset, clip_x, clip_width);
        break;
#endif
#if KK_UI_ENABLE_CUSTOM
    case KK_UI_PAGE_CUSTOM:
        kk_ui.in_callback = 1U;
        KK_UI_CustomOnDraw(state->page, x_offset, clip_x, clip_width);
        kk_ui.in_callback = 0U;
        break;
#endif
    default:
        break;
    }
}

void KK_UI_DrawScene(uint32_t now)
{
    OLED_Clear();
    OLED_ResetClipWindow();
    OLED_SetDrawMode(OLED_DRAW_SET);
    OLED_SetBackgroundMode(OLED_BG_TRANSPARENT);
    OLED_SetFontDirection(OLED_ROTATION_0);
    OLED_SetFontPosition(OLED_FONT_POS_TOP);
    OLED_SetFontRefHeight(OLED_FONT_REF_ALL);

    if (kk_ui.transition.active != 0U) {
        uint16_t progress = KK_UI_EaseQ12(now - kk_ui.transition.started,
                                         KK_UI_PAGE_MS);
        int16_t boundary;
        int16_t outgoing_x;
        int16_t current_x;

        if (kk_ui.transition.reverse == 0U) {
            outgoing_x = (int16_t)(-((int32_t)KK_UI_PAGE_PARALLAX * progress /
                                     KK_UI_Q12_ONE));
            current_x = (int16_t)((int32_t)KK_UI_SCREEN_WIDTH *
                          (KK_UI_Q12_ONE - progress) / KK_UI_Q12_ONE);
            boundary = current_x;
            if (boundary > 0) {
                KK_UI_DrawPage(&kk_ui.transition.outgoing, outgoing_x, 0,
                               (uint16_t)boundary, now);
            }
            if (boundary < KK_UI_SCREEN_WIDTH) {
                KK_UI_DrawPage(&kk_ui.current, current_x, boundary,
                               (uint16_t)(KK_UI_SCREEN_WIDTH - boundary), now);
            }
            if (boundary >= 0 && boundary < KK_UI_SCREEN_WIDTH) {
                OLED_ResetClipWindow();
                OLED_DrawVLine(boundary, 0, KK_UI_SCREEN_HEIGHT);
            }
        } else {
            outgoing_x = (int16_t)(((int32_t)KK_UI_PAGE_PARALLAX * progress) /
                                   KK_UI_Q12_ONE);
            boundary = (int16_t)(((int32_t)KK_UI_SCREEN_WIDTH * progress) /
                                 KK_UI_Q12_ONE);
            current_x = (int16_t)(boundary - KK_UI_SCREEN_WIDTH);
            if (boundary > 0) {
                KK_UI_DrawPage(&kk_ui.current, current_x, 0,
                               (uint16_t)boundary, now);
            }
            if (boundary < KK_UI_SCREEN_WIDTH) {
                KK_UI_DrawPage(&kk_ui.transition.outgoing, outgoing_x,
                               boundary,
                               (uint16_t)(KK_UI_SCREEN_WIDTH - boundary), now);
            }
            if (boundary >= 0 && boundary < KK_UI_SCREEN_WIDTH) {
                OLED_ResetClipWindow();
                OLED_DrawVLine(boundary, 0, KK_UI_SCREEN_HEIGHT);
            }
        }
    } else {
        KK_UI_DrawPage(&kk_ui.current, 0, 0, KK_UI_SCREEN_WIDTH, now);
    }

    if (kk_ui.reject_flash > 1U) {
        OLED_SetDrawMode(OLED_DRAW_XOR);
        OLED_DrawRFrame(1, 1, 126U, 62U, 3U);
        OLED_SetDrawMode(OLED_DRAW_SET);
        kk_ui.reject_flash = 1U;
        kk_ui.dirty = 1U;
    } else if (kk_ui.reject_flash != 0U) {
        kk_ui.reject_flash = 0U;
    }
    KK_UI_DialogDraw();
    KK_UI_ToastDraw();
    OLED_ResetClipWindow();
    OLED_SetDrawMode(OLED_DRAW_SET);
}
