#include "kk_ui_internal.h"

#include <string.h>

KK_UI_Runtime kk_ui;

void KK_UI_RecordError(KK_UI_Status code, KK_UI_PageId page, uint16_t index)
{
    kk_ui.error.code = code;
    kk_ui.error.page = page;
    kk_ui.error.index = index;
    kk_ui.error_valid = 1U;
}

bool KK_UI_QueueEvent(KK_UI_EventId event)
{
    uint8_t tail;
    if (event == KK_UI_EVENT_NONE) {
        return true;
    }
    if (kk_ui.event_count >= KK_UI_EVENT_QUEUE_LENGTH) {
        return false;
    }
    tail = (uint8_t)((kk_ui.event_head + kk_ui.event_count) %
                     KK_UI_EVENT_QUEUE_LENGTH);
    kk_ui.events[tail] = event;
    ++kk_ui.event_count;
    return true;
}

static KK_UI_Status kk_ui_config_error(KK_UI_PageId page, uint16_t index)
{
    KK_UI_RecordError(KK_UI_CONFIG_ERROR, page, index);
    return KK_UI_CONFIG_ERROR;
}

static bool kk_ui_valid_texts(const KK_UI_App *app)
{
    return app->fonts.home_font != NULL && app->fonts.title_font != NULL &&
           app->fonts.body_font != NULL && app->texts.return_text != NULL &&
           app->texts.cancel_text != NULL && app->texts.confirm_text != NULL &&
           app->texts.on_text != NULL && app->texts.off_text != NULL &&
           app->texts.message_title != NULL;
}

static KK_UI_Status kk_ui_validate_route(const KK_UI_App *app,
                                         KK_UI_PageId page)
{
    const KK_UI_PageRoute *route = &app->routes[page - 1U];
    switch (route->type) {
    case KK_UI_PAGE_HOME:
#if KK_UI_ENABLE_HOME
        if (route->index >= app->home_page_count) {
            return kk_ui_config_error(page, route->index);
        }
        break;
#else
        return kk_ui_config_error(page, route->index);
#endif
    case KK_UI_PAGE_MENU:
#if KK_UI_ENABLE_MENU
        if (route->index >= app->menu_page_count) {
            return kk_ui_config_error(page, route->index);
        }
        break;
#else
        return kk_ui_config_error(page, route->index);
#endif
    case KK_UI_PAGE_INFO:
#if KK_UI_ENABLE_INFO
        if (route->index >= app->info_page_count) {
            return kk_ui_config_error(page, route->index);
        }
        break;
#else
        return kk_ui_config_error(page, route->index);
#endif
    case KK_UI_PAGE_CUSTOM:
#if KK_UI_ENABLE_CUSTOM
        if (route->index >= app->custom_page_count) {
            return kk_ui_config_error(page, route->index);
        }
        break;
#else
        return kk_ui_config_error(page, route->index);
#endif
    default:
        return kk_ui_config_error(page, route->index);
    }
    return KK_UI_OK;
}

static KK_UI_Status kk_ui_validate_pages(const KK_UI_App *app)
{
    uint16_t i;
    for (i = 0U; i < app->home_page_count; ++i) {
        const KK_UI_HomePage *page = &app->home_pages[i];
        uint16_t j;
        if (page->items == NULL || page->item_count == 0U) {
            return kk_ui_config_error(0U, i);
        }
        for (j = 0U; j < page->item_count; ++j) {
            if (page->items[j].label == NULL ||
                page->items[j].icon_xbm_32x32 == NULL ||
                page->items[j].target_page == KK_UI_PAGE_NONE ||
                page->items[j].target_page > app->route_count) {
                return kk_ui_config_error(0U, j);
            }
        }
    }
    for (i = 0U; i < app->menu_page_count; ++i) {
        const KK_UI_MenuPage *page = &app->menu_pages[i];
        uint16_t j;
        if (page->title == NULL || page->item_count == UINT16_MAX ||
            (page->item_count != 0U && page->items == NULL)) {
            return kk_ui_config_error(0U, i);
        }
        for (j = 0U; j < page->item_count; ++j) {
            const KK_UI_MenuItem *item = &page->items[j];
            if (item->label == NULL) {
                return kk_ui_config_error(0U, j);
            }
            switch (item->type) {
            case KK_UI_MENU_PAGE:
                if (item->ref == KK_UI_PAGE_NONE ||
                    item->ref > app->route_count) {
                    return kk_ui_config_error(0U, j);
                }
                break;
            case KK_UI_MENU_ACTION:
                if (item->ref == KK_UI_EVENT_NONE) {
                    return kk_ui_config_error(0U, j);
                }
                break;
            case KK_UI_MENU_INT:
#if KK_UI_ENABLE_INT_EDITOR
                if (item->ref >= app->int_binding_count) {
                    return kk_ui_config_error(0U, j);
                }
                break;
#else
                return kk_ui_config_error(0U, j);
#endif
            case KK_UI_MENU_BOOL:
#if KK_UI_ENABLE_BOOL_EDITOR
                if (item->ref >= app->bool_binding_count) {
                    return kk_ui_config_error(0U, j);
                }
                break;
#else
                return kk_ui_config_error(0U, j);
#endif
            case KK_UI_MENU_CONFIRM:
#if KK_UI_ENABLE_CONFIRM
                if (item->ref >= app->confirm_desc_count) {
                    return kk_ui_config_error(0U, j);
                }
                break;
#else
                return kk_ui_config_error(0U, j);
#endif
            default:
                return kk_ui_config_error(0U, j);
            }
        }
    }
    for (i = 0U; i < app->info_page_count; ++i) {
        const KK_UI_InfoPage *page = &app->info_pages[i];
        uint16_t j;
        if (page->row_count != 0U && page->rows == NULL) {
            return kk_ui_config_error(0U, i);
        }
        for (j = 0U; j < page->row_count; ++j) {
            if (page->rows[j].name == NULL) {
                return kk_ui_config_error(0U, j);
            }
        }
    }
    return KK_UI_OK;
}

static KK_UI_Status kk_ui_validate_bindings(const KK_UI_App *app)
{
    uint16_t i;
    for (i = 0U; i < app->int_binding_count; ++i) {
        const KK_UI_IntBinding *binding = &app->int_bindings[i];
        if (binding->title == NULL || binding->value == NULL ||
            binding->minimum > binding->maximum || binding->step == 0U ||
            *binding->value < binding->minimum ||
            *binding->value > binding->maximum) {
            return kk_ui_config_error(0U, i);
        }
    }
    for (i = 0U; i < app->bool_binding_count; ++i) {
        if (app->bool_bindings[i].title == NULL ||
            app->bool_bindings[i].value == NULL) {
            return kk_ui_config_error(0U, i);
        }
    }
    for (i = 0U; i < app->confirm_desc_count; ++i) {
        if (app->confirm_descs[i].text == NULL) {
            return kk_ui_config_error(0U, i);
        }
    }
    return KK_UI_OK;
}

static KK_UI_Status kk_ui_validate(const KK_UI_App *app)
{
    uint16_t i;
    KK_UI_Status status;
    if (app == NULL || app->routes == NULL || app->route_count == 0U ||
        app->root_page == KK_UI_PAGE_NONE ||
        app->root_page > app->route_count || !kk_ui_valid_texts(app)) {
        return kk_ui_config_error(0U, 0U);
    }
    if ((app->home_page_count != 0U && app->home_pages == NULL) ||
        (app->menu_page_count != 0U && app->menu_pages == NULL) ||
        (app->info_page_count != 0U && app->info_pages == NULL) ||
        (app->int_binding_count != 0U && app->int_bindings == NULL) ||
        (app->bool_binding_count != 0U && app->bool_bindings == NULL) ||
        (app->confirm_desc_count != 0U && app->confirm_descs == NULL)) {
        return kk_ui_config_error(0U, 0U);
    }
    for (i = 0U; i < app->route_count; ++i) {
        status = kk_ui_validate_route(app, (KK_UI_PageId)(i + 1U));
        if (status != KK_UI_OK) {
            return status;
        }
    }
    status = kk_ui_validate_pages(app);
    if (status != KK_UI_OK) {
        return status;
    }
    return kk_ui_validate_bindings(app);
}

KK_UI_Status KK_UI_Init(const KK_UI_App *app)
{
    KK_UI_Status status;
    memset(&kk_ui, 0, sizeof(kk_ui));
    kk_ui.app = app;
    status = kk_ui_validate(app);
    if (status != KK_UI_OK) {
        return status;
    }
    if (OLED_GetWidth() != KK_UI_SCREEN_WIDTH ||
        OLED_GetHeight() != KK_UI_SCREEN_HEIGHT) {
        return kk_ui_config_error(0U, 0U);
    }
    KK_UI_ResetPageState(&kk_ui.current, app->root_page);
    kk_ui.initialized = 1U;
    kk_ui.dirty = 1U;
#if KK_UI_ENABLE_CUSTOM
    if (KK_UI_CurrentPageType() == KK_UI_PAGE_CUSTOM) {
        kk_ui.in_callback = 1U;
        KK_UI_CustomOnEnter(kk_ui.current.page);
        kk_ui.in_callback = 0U;
    }
#endif
    return KK_UI_OK;
}

void KK_UI_Invalidate(void)
{
    if (kk_ui.initialized != 0U) {
        kk_ui.dirty = 1U;
    }
}

bool KK_UI_PollEvent(KK_UI_EventId *out_event)
{
    if (out_event == NULL || kk_ui.event_count == 0U) {
        return false;
    }
    *out_event = kk_ui.events[kk_ui.event_head];
    kk_ui.event_head = (uint8_t)((kk_ui.event_head + 1U) %
                                 KK_UI_EVENT_QUEUE_LENGTH);
    --kk_ui.event_count;
    return true;
}

bool KK_UI_PollError(KK_UI_ErrorInfo *out_error)
{
    if (out_error == NULL || kk_ui.error_valid == 0U) {
        return false;
    }
    *out_error = kk_ui.error;
    kk_ui.error_valid = 0U;
    return true;
}

KK_UI_Status KK_UI_SetMenuItemState(KK_UI_PageId page_id,
                                    uint16_t item_index,
                                    KK_UI_ItemState state)
{
#if KK_UI_ENABLE_MENU
    const KK_UI_PageRoute *route;
    const KK_UI_MenuPage *page;
    uint8_t shift;
    uint8_t mask;
    if (kk_ui.initialized == 0U) {
        return KK_UI_NOT_INITIALIZED;
    }
    if (state > KK_UI_ITEM_RESERVED) {
        return KK_UI_INVALID_ARGUMENT;
    }
    route = KK_UI_GetRoute(page_id);
    if (route == NULL || route->type != KK_UI_PAGE_MENU) {
        return KK_UI_INVALID_ARGUMENT;
    }
    page = &kk_ui.app->menu_pages[route->index];
    if (item_index >= page->item_count) {
        return KK_UI_INVALID_ARGUMENT;
    }
    if (page->item_states == NULL) {
        return KK_UI_UNSUPPORTED;
    }
    shift = (uint8_t)((item_index & 3U) * 2U);
    mask = (uint8_t)(3U << shift);
    page->item_states[item_index >> 2U] =
        (uint8_t)((page->item_states[item_index >> 2U] & (uint8_t)~mask) |
                  ((uint8_t)state << shift));
    if (kk_ui.current.page == page_id) {
        kk_ui.list_anim.active = 0U;
        KK_UI_MenuRepairFocus(&kk_ui.current);
    }
    kk_ui.dirty = 1U;
    return KK_UI_OK;
#else
    (void)page_id; (void)item_index; (void)state;
    return KK_UI_UNSUPPORTED;
#endif
}

KK_UI_Status KK_UI_Defer(KK_UI_DeferredType type, uint16_t ref,
                         const char *text, KK_UI_EventId event,
                         uint32_t duration)
{
    if (kk_ui.deferred.type != KK_UI_DEFER_NONE) {
        return KK_UI_BUSY;
    }
    kk_ui.deferred.type = type;
    kk_ui.deferred.ref = ref;
    kk_ui.deferred.text = text;
    kk_ui.deferred.event = event;
    kk_ui.deferred.duration = duration;
    return KK_UI_OK;
}

KK_UI_Status KK_UI_OpenIntEditor(uint16_t binding_index)
{
    if (kk_ui.initialized == 0U) return KK_UI_NOT_INITIALIZED;
#if KK_UI_ENABLE_INT_EDITOR
    if (binding_index >= kk_ui.app->int_binding_count)
        return KK_UI_INVALID_ARGUMENT;
    if (KK_UI_DialogActive()) return KK_UI_BUSY;
    if (kk_ui.in_callback != 0U)
        return KK_UI_Defer(KK_UI_DEFER_INT, binding_index, NULL, 0U, 0U);
    return KK_UI_DialogOpenInt(binding_index, kk_ui.last_update);
#else
    (void)binding_index;
    return KK_UI_UNSUPPORTED;
#endif
}

KK_UI_Status KK_UI_OpenBoolEditor(uint16_t binding_index)
{
    if (kk_ui.initialized == 0U) return KK_UI_NOT_INITIALIZED;
#if KK_UI_ENABLE_BOOL_EDITOR
    if (binding_index >= kk_ui.app->bool_binding_count)
        return KK_UI_INVALID_ARGUMENT;
    if (KK_UI_DialogActive()) return KK_UI_BUSY;
    if (kk_ui.in_callback != 0U)
        return KK_UI_Defer(KK_UI_DEFER_BOOL, binding_index, NULL, 0U, 0U);
    return KK_UI_DialogOpenBool(binding_index, kk_ui.last_update);
#else
    (void)binding_index;
    return KK_UI_UNSUPPORTED;
#endif
}

KK_UI_Status KK_UI_OpenConfirm(uint16_t confirm_index)
{
    if (kk_ui.initialized == 0U) return KK_UI_NOT_INITIALIZED;
#if KK_UI_ENABLE_CONFIRM
    if (confirm_index >= kk_ui.app->confirm_desc_count)
        return KK_UI_INVALID_ARGUMENT;
    if (KK_UI_DialogActive()) return KK_UI_BUSY;
    if (kk_ui.in_callback != 0U)
        return KK_UI_Defer(KK_UI_DEFER_CONFIRM, confirm_index, NULL, 0U, 0U);
    return KK_UI_DialogOpenConfirm(confirm_index, kk_ui.last_update);
#else
    (void)confirm_index;
    return KK_UI_UNSUPPORTED;
#endif
}

KK_UI_Status KK_UI_ShowMessage(const char *text, KK_UI_EventId event)
{
    if (kk_ui.initialized == 0U) return KK_UI_NOT_INITIALIZED;
    if (text == NULL) return KK_UI_INVALID_ARGUMENT;
#if KK_UI_ENABLE_MESSAGE
    if (KK_UI_DialogActive()) return KK_UI_BUSY;
    if (kk_ui.in_callback != 0U)
        return KK_UI_Defer(KK_UI_DEFER_MESSAGE, 0U, text, event, 0U);
    return KK_UI_DialogOpenMessage(text, event, kk_ui.last_update);
#else
    (void)event;
    return KK_UI_UNSUPPORTED;
#endif
}

KK_UI_Status KK_UI_ShowToast(const char *text, uint32_t duration_ms)
{
    if (kk_ui.initialized == 0U) return KK_UI_NOT_INITIALIZED;
    if (text == NULL) return KK_UI_INVALID_ARGUMENT;
#if KK_UI_ENABLE_TOAST
    if (KK_UI_DialogActive()) return KK_UI_BUSY;
    if (kk_ui.in_callback != 0U)
        return KK_UI_Defer(KK_UI_DEFER_TOAST, 0U, text, 0U, duration_ms);
    return KK_UI_ToastOpen(text, duration_ms, kk_ui.last_update);
#else
    (void)duration_ms;
    return KK_UI_UNSUPPORTED;
#endif
}

KK_UI_Status KK_UI_CustomRequestClose(void)
{
    if (kk_ui.initialized == 0U) return KK_UI_NOT_INITIALIZED;
    if (KK_UI_CurrentPageType() != KK_UI_PAGE_CUSTOM) return KK_UI_NOT_ALLOWED;
    if (kk_ui.transition.active != 0U || KK_UI_DialogActive()) return KK_UI_BUSY;
    if (kk_ui.stack_count == 0U) return KK_UI_NOT_ALLOWED;
    if (kk_ui.in_callback != 0U)
        return KK_UI_Defer(KK_UI_DEFER_CLOSE, 0U, NULL, 0U, 0U);
    return KK_UI_NavigateBack(kk_ui.last_update);
}

KK_UI_Status KK_UI_CustomFinish(KK_UI_EventId event)
{
    if (kk_ui.initialized == 0U) return KK_UI_NOT_INITIALIZED;
    if (event == KK_UI_EVENT_NONE) return KK_UI_INVALID_ARGUMENT;
    if (KK_UI_CurrentPageType() != KK_UI_PAGE_CUSTOM) return KK_UI_NOT_ALLOWED;
    if (kk_ui.transition.active != 0U || KK_UI_DialogActive()) return KK_UI_BUSY;
    if (kk_ui.stack_count == 0U) return KK_UI_NOT_ALLOWED;
    if (kk_ui.in_callback != 0U)
        return KK_UI_Defer(KK_UI_DEFER_FINISH, 0U, NULL, event, 0U);
    if (!KK_UI_QueueEvent(event)) return KK_UI_QUEUE_FULL;
    return KK_UI_NavigateBack(kk_ui.last_update);
}

void KK_UI_ApplyDeferred(uint32_t now)
{
    KK_UI_Deferred request = kk_ui.deferred;
    kk_ui.deferred.type = KK_UI_DEFER_NONE;
    switch (request.type) {
    case KK_UI_DEFER_CLOSE:
        if (KK_UI_CurrentPageType() == KK_UI_PAGE_CUSTOM &&
            kk_ui.stack_count != 0U && kk_ui.transition.active == 0U &&
            !KK_UI_DialogActive()) {
            (void)KK_UI_NavigateBack(now);
        }
        break;
    case KK_UI_DEFER_FINISH:
        if (KK_UI_CurrentPageType() != KK_UI_PAGE_CUSTOM ||
            kk_ui.stack_count == 0U || kk_ui.transition.active != 0U ||
            KK_UI_DialogActive()) {
            KK_UI_RecordError(KK_UI_NOT_ALLOWED, kk_ui.current.page,
                              request.event);
        } else if (KK_UI_QueueEvent(request.event)) {
            (void)KK_UI_NavigateBack(now);
        } else {
            KK_UI_RecordError(KK_UI_QUEUE_FULL, kk_ui.current.page,
                              request.event);
            kk_ui.reject_flash = 2U;
            kk_ui.dirty = 1U;
        }
        break;
    case KK_UI_DEFER_INT:
        (void)KK_UI_DialogOpenInt(request.ref, now);
        break;
    case KK_UI_DEFER_BOOL:
        (void)KK_UI_DialogOpenBool(request.ref, now);
        break;
    case KK_UI_DEFER_CONFIRM:
        (void)KK_UI_DialogOpenConfirm(request.ref, now);
        break;
    case KK_UI_DEFER_MESSAGE:
        (void)KK_UI_DialogOpenMessage(request.text, request.event, now);
        break;
    case KK_UI_DEFER_TOAST:
        (void)KK_UI_ToastOpen(request.text, request.duration, now);
        break;
    default:
        break;
    }
}

void KK_UI_DispatchInput(KK_UI_InputEvent event, uint32_t now)
{
    if (KK_UI_DialogActive()) {
        KK_UI_DialogInput(event, now);
        return;
    }
    switch (KK_UI_CurrentPageType()) {
    case KK_UI_PAGE_HOME:
        KK_UI_HomeInput(event, now);
        break;
    case KK_UI_PAGE_MENU:
        KK_UI_MenuInput(event, now);
        break;
    case KK_UI_PAGE_INFO:
        KK_UI_InfoInput(event, now);
        break;
    case KK_UI_PAGE_CUSTOM:
        KK_UI_CustomInput(event);
        break;
    default:
        break;
    }
}

static uint32_t kk_ui_repeat_interval(uint32_t held)
{
    if (held >= 2000U) return 100U;
    if (held >= 1000U) return 160U;
    return 240U;
}

static void kk_ui_process_input(uint32_t now, KK_UI_Input input)
{
    uint8_t press = 0U;
    uint8_t repeat = 0U;
    uint8_t stable = 0U;
    uint8_t i;
    KK_UI_InputEvent event;

    input.keys &= (uint8_t)(KK_UI_KEY_UP | KK_UI_KEY_DOWN | KK_UI_KEY_OK);
    if (kk_ui.keys_initialized == 0U) {
        for (i = 0U; i < 3U; ++i) {
            uint8_t down = (input.keys & (1U << i)) != 0U;
            kk_ui.keys[i].changed_at = now;
            kk_ui.keys[i].pressed_at = now;
            kk_ui.keys[i].repeat_at = now;
            kk_ui.keys[i].candidate = down;
            kk_ui.keys[i].stable = down;
            kk_ui.keys[i].armed = (uint8_t)!down;
        }
        kk_ui.keys_initialized = 1U;
    } else {
        for (i = 0U; i < 3U; ++i) {
            KK_UI_KeyState *key = &kk_ui.keys[i];
            uint8_t down = (input.keys & (1U << i)) != 0U;
            if (down != key->candidate) {
                key->candidate = down;
                key->changed_at = now;
            }
            if (key->candidate != key->stable &&
                now - key->changed_at >= KK_UI_KEY_DEBOUNCE_MS) {
                key->stable = key->candidate;
                if (key->stable == 0U) {
                    key->armed = 1U;
                } else if (key->armed != 0U) {
                    key->pressed_at = now;
                    key->repeat_at = now + KK_UI_KEY_REPEAT_DELAY_MS;
                    press |= (uint8_t)(1U << i);
                }
            }
            if (i < 2U && key->stable != 0U && key->candidate != 0U &&
                key->armed != 0U &&
                (int32_t)(now - key->repeat_at) >= 0) {
                repeat |= (uint8_t)(1U << i);
                key->repeat_at = now +
                    kk_ui_repeat_interval(now - key->pressed_at);
            }
        }
    }
    for (i = 0U; i < 3U; ++i) {
        if (kk_ui.keys[i].stable != 0U) stable |= (uint8_t)(1U << i);
    }
    kk_ui.blocked_keys &= stable;
    if (kk_ui.transition.active != 0U) {
        kk_ui.blocked_keys |= stable;
        return;
    }
    press &= (uint8_t)~kk_ui.blocked_keys;
    repeat &= (uint8_t)~kk_ui.blocked_keys;

    event.steps = 1U;
    if ((press & KK_UI_KEY_OK) != 0U) {
        event.action = KK_UI_INPUT_OK;
        event.source = KK_UI_INPUT_PRESS;
        KK_UI_DispatchInput(event, now);
        return;
    }
    if (input.encoder_delta != 0) {
        int32_t delta = input.encoder_delta;
        event.action = delta < 0 ? KK_UI_INPUT_UP : KK_UI_INPUT_DOWN;
        event.source = KK_UI_INPUT_ENCODER;
        event.steps = (uint16_t)(delta < 0 ? -delta : delta);
        KK_UI_DispatchInput(event, now);
        return;
    }
    if ((stable & (KK_UI_KEY_UP | KK_UI_KEY_DOWN)) ==
        (KK_UI_KEY_UP | KK_UI_KEY_DOWN)) {
        return;
    }
    if (((press | repeat) & KK_UI_KEY_UP) != 0U) {
        event.action = KK_UI_INPUT_UP;
        event.source = (repeat & KK_UI_KEY_UP) != 0U ?
                       KK_UI_INPUT_REPEAT : KK_UI_INPUT_PRESS;
        KK_UI_DispatchInput(event, now);
    } else if (((press | repeat) & KK_UI_KEY_DOWN) != 0U) {
        event.action = KK_UI_INPUT_DOWN;
        event.source = (repeat & KK_UI_KEY_DOWN) != 0U ?
                       KK_UI_INPUT_REPEAT : KK_UI_INPUT_PRESS;
        KK_UI_DispatchInput(event, now);
    }
}

static KK_UI_Status kk_ui_latch_display(KK_UI_Status status)
{
    kk_ui.display_fault = 1U;
    kk_ui.frame_ready = 0U;
    KK_UI_RecordError(status, kk_ui.current.page, 0U);
    return status;
}

static KK_UI_Status kk_ui_check_transfer(uint32_t now)
{
    if (kk_ui.transfer_active == 0U) {
        return KK_UI_OK;
    }
    if (OLED_IsBusy()) {
#if KK_UI_DISPLAY_TIMEOUT_MS > 0U
        if (now - kk_ui.transfer_started >= KK_UI_DISPLAY_TIMEOUT_MS) {
            return kk_ui_latch_display(KK_UI_DISPLAY_TIMEOUT);
        }
#endif
        return KK_UI_OK;
    }
    kk_ui.transfer_active = 0U;
    if (OLED_GetLastStatus() != OLED_OK) {
        return kk_ui_latch_display(KK_UI_DISPLAY_ERROR);
    }
    return KK_UI_OK;
}

static KK_UI_Status kk_ui_submit(uint32_t now)
{
    OLED_Status oled_status;
#if KK_UI_REFRESH_MODE == KK_UI_REFRESH_BLOCKING
    oled_status = OLED_Update();
#elif KK_UI_REFRESH_MODE == KK_UI_REFRESH_IT
    oled_status = OLED_UpdateIT();
#elif KK_UI_REFRESH_MODE == KK_UI_REFRESH_DMA
    oled_status = OLED_UpdateDMA();
#else
#error "Invalid KK_UI_REFRESH_MODE"
#endif
    if (oled_status == OLED_BUSY) {
        return KK_UI_OK;
    }
    if (oled_status != OLED_OK) {
        return kk_ui_latch_display(KK_UI_DISPLAY_ERROR);
    }
    kk_ui.frame_ready = 0U;
#if KK_UI_REFRESH_MODE != KK_UI_REFRESH_BLOCKING
    kk_ui.transfer_active = 1U;
    kk_ui.transfer_started = now;
#else
    (void)now;
#endif
    return KK_UI_OK;
}

KK_UI_Status KK_UI_Update(uint32_t now, KK_UI_Input input)
{
    KK_UI_Status status;
    if (kk_ui.initialized == 0U) {
        return KK_UI_NOT_INITIALIZED;
    }
    if (kk_ui.keys_initialized == 0U) {
        kk_ui.next_frame = now;
    }
    kk_ui.last_update = now;
    status = kk_ui_check_transfer(now);
    if (status != KK_UI_OK || kk_ui.display_fault != 0U) {
        return status != KK_UI_OK ? status : KK_UI_DISPLAY_ERROR;
    }

    KK_UI_AnimateNavigation(now);
    KK_UI_HomeAnimate(now);
    KK_UI_MenuAnimate(now);
    KK_UI_InfoAnimate(now);
    KK_UI_DialogAnimate(now);
    kk_ui_process_input(now, input);
    KK_UI_ApplyDeferred(now);
    KK_UI_CustomTick(now);
    KK_UI_ApplyDeferred(now);

    if (kk_ui.dirty != 0U && (int32_t)(now - kk_ui.next_frame) >= 0) {
        uint32_t missed = (now - kk_ui.next_frame) / KK_UI_FRAME_INTERVAL_MS;
        kk_ui.next_frame += (missed + 1U) * KK_UI_FRAME_INTERVAL_MS;
        kk_ui.dirty = 0U;
        KK_UI_DrawScene(now);
        kk_ui.frame_ready = 1U;
    }
    if (kk_ui.frame_ready != 0U && !OLED_IsBusy()) {
        status = kk_ui_submit(now);
        if (status != KK_UI_OK) {
            return status;
        }
    }
    return KK_UI_OK;
}

bool KK_UI_GetIntEditorDraft(uint16_t binding_index, int32_t *out_value)
{
#if KK_UI_ENABLE_INT_EDITOR
    if (kk_ui.initialized == 0U || out_value == NULL ||
        kk_ui.overlay.type != KK_UI_OVERLAY_INT ||
        kk_ui.overlay.closing != 0U ||
        kk_ui.overlay.ref != binding_index) {
        return false;
    }
    *out_value = kk_ui.overlay.draft;
    return true;
#else
    (void)binding_index;
    (void)out_value;
    return false;
#endif
}

bool KK_UI_IsDisplayIdle(void)
{
    return kk_ui.initialized != 0U && kk_ui.display_fault == 0U &&
           kk_ui.dirty == 0U && kk_ui.frame_ready == 0U &&
           kk_ui.transfer_active == 0U &&
           !OLED_IsBusy();
}

KK_UI_Status KK_UI_RecoverDisplay(void)
{
    if (kk_ui.initialized == 0U) {
        return KK_UI_NOT_INITIALIZED;
    }
    if (OLED_IsBusy()) {
        return KK_UI_BUSY;
    }
    /*
     * 异步完成失败后 KK_OLED 会保留错误状态，并把下一次正常刷新标记为
     * 强制全屏。恢复入口的职责正是允许这次重画与重发，因此不能要求
     * OLED_GetLastStatus() 已经先变回 OLED_OK，否则错误将无法恢复。
     */
    kk_ui.display_fault = 0U;
    kk_ui.transfer_active = 0U;
    kk_ui.frame_ready = 0U;
    kk_ui.dirty = 1U;
    kk_ui.next_frame = kk_ui.last_update;
    kk_ui.blocked_keys |= (uint8_t)(
        (kk_ui.keys[0].stable ? KK_UI_KEY_UP : 0U) |
        (kk_ui.keys[1].stable ? KK_UI_KEY_DOWN : 0U) |
        (kk_ui.keys[2].stable ? KK_UI_KEY_OK : 0U));
    return KK_UI_OK;
}
