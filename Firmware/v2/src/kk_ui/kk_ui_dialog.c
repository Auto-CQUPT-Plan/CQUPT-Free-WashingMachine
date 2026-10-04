#include "kk_ui_internal.h"
#include "kk_ui_draw.h"

#include <limits.h>

#define KK_UI_MODAL_ENABLED (KK_UI_ENABLE_INT_EDITOR || \
                             KK_UI_ENABLE_BOOL_EDITOR || \
                             KK_UI_ENABLE_CONFIRM || \
                             KK_UI_ENABLE_MESSAGE)

void KK_UI_DrawCentered(int16_t x, int16_t y, uint16_t width,
                        const char *text)
{
    int16_t text_x;
    if (text == NULL) {
        return;
    }
    text_x = (int16_t)(x + ((int32_t)width - OLED_GetUTF8Width(text)) / 2);
    OLED_DrawUTF8(text_x, y, text);
}

void KK_UI_FormatInt(int32_t value, const char *unit, char *buffer,
                     size_t capacity)
{
    char reverse[11];
    uint32_t magnitude;
    size_t digits = 0U;
    size_t out = 0U;

    if (buffer == NULL || capacity == 0U) {
        return;
    }
    if (value < 0) {
        magnitude = (uint32_t)(-(value + 1)) + 1U;
        if (out + 1U < capacity) {
            buffer[out++] = '-';
        }
    } else {
        magnitude = (uint32_t)value;
    }
    do {
        reverse[digits++] = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0U && digits < sizeof(reverse));
    while (digits != 0U && out + 1U < capacity) {
        buffer[out++] = reverse[--digits];
    }
    if (unit != NULL) {
        size_t i = 0U;
        while (unit[i] != '\0' && out + 1U < capacity) {
            buffer[out++] = unit[i++];
        }
    }
    buffer[out] = '\0';
}

bool KK_UI_DialogActive(void)
{
#if KK_UI_MODAL_ENABLED
    return kk_ui.overlay.type != KK_UI_OVERLAY_NONE;
#else
    return false;
#endif
}

#if KK_UI_MODAL_ENABLED
static int16_t kk_ui_dialog_origin(void)
{
    if (KK_UI_CurrentPageType() == KK_UI_PAGE_MENU) {
        return (int16_t)(KK_UI_MENU_HEADER_HEIGHT +
                         KK_UI_RoundQ8(kk_ui.current.focus_q8));
    }
    return 24;
}
#endif

#if KK_UI_MODAL_ENABLED
static void kk_ui_dialog_focus_rect(uint8_t focus, int16_t *x, int16_t *y,
                                    int16_t *width, int16_t *height)
{
    if (kk_ui.overlay.type == KK_UI_OVERLAY_MESSAGE) {
        *x = 46; *y = 41; *width = 36; *height = 16;
    } else if (focus == KK_UI_FOCUS_VALUE) {
        *x = kk_ui.overlay.type == KK_UI_OVERLAY_BOOL ? 29 : 39;
        *y = 21;
        *width = kk_ui.overlay.type == KK_UI_OVERLAY_BOOL ? 70 : 46;
        *height = 16;
    } else if (focus == KK_UI_FOCUS_CONFIRM) {
        *x = 77; *y = 41; *width = 36; *height = 16;
    } else {
        *x = 15; *y = 41; *width = 36; *height = 16;
    }
}

static void kk_ui_dialog_current_focus(uint32_t now, int32_t *x, int32_t *y,
                                       int32_t *width, int32_t *height)
{
    if (kk_ui.overlay.focus_moving != 0U) {
        uint16_t p = KK_UI_EaseQ12(now - kk_ui.overlay.focus_started,
                                   KK_UI_FOCUS_MS);
        *x = KK_UI_LerpQ12(kk_ui.overlay.focus_from_x_q8,
                          kk_ui.overlay.focus_x_q8, p);
        *y = KK_UI_LerpQ12(kk_ui.overlay.focus_from_y_q8,
                          kk_ui.overlay.focus_y_q8, p);
        *width = KK_UI_LerpQ12(kk_ui.overlay.focus_from_w_q8,
                              kk_ui.overlay.focus_w_q8, p);
        *height = KK_UI_LerpQ12(kk_ui.overlay.focus_from_h_q8,
                               kk_ui.overlay.focus_h_q8, p);
    } else {
        *x = kk_ui.overlay.focus_x_q8;
        *y = kk_ui.overlay.focus_y_q8;
        *width = kk_ui.overlay.focus_w_q8;
        *height = kk_ui.overlay.focus_h_q8;
    }
}

static void kk_ui_dialog_set_focus(uint8_t focus, uint32_t now)
{
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;

    kk_ui_dialog_current_focus(now, &kk_ui.overlay.focus_from_x_q8,
                               &kk_ui.overlay.focus_from_y_q8,
                               &kk_ui.overlay.focus_from_w_q8,
                               &kk_ui.overlay.focus_from_h_q8);
    kk_ui.overlay.focus = focus;
    kk_ui_dialog_focus_rect(focus, &x, &y, &width, &height);
    kk_ui.overlay.focus_x_q8 = (int32_t)x * 256;
    kk_ui.overlay.focus_y_q8 = (int32_t)y * 256;
    kk_ui.overlay.focus_w_q8 = (int32_t)width * 256;
    kk_ui.overlay.focus_h_q8 = (int32_t)height * 256;
    kk_ui.overlay.focus_started = now;
    kk_ui.overlay.focus_moving = 1U;
    kk_ui.dirty = 1U;
}

static KK_UI_Status kk_ui_dialog_begin(KK_UI_OverlayType type, uint16_t ref,
                                       const char *text,
                                       KK_UI_EventId event, uint32_t now)
{
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;

    if (KK_UI_DialogActive()) {
        return KK_UI_BUSY;
    }
    kk_ui.toast.visible = 0U;
    kk_ui.overlay.type = type;
    kk_ui.overlay.ref = ref;
    kk_ui.overlay.text = text;
    kk_ui.overlay.event = event;
    kk_ui.overlay.origin_y = kk_ui_dialog_origin();
    kk_ui.overlay.phase_started = now;
    kk_ui.overlay.phase_q12 = 0U;
    kk_ui.overlay.phase_from_q12 = 0U;
    kk_ui.overlay.value_q12 = KK_UI_Q12_ONE;
    kk_ui.overlay.closing = 0U;
    kk_ui.overlay.focus_moving = 0U;
    kk_ui.overlay.value_moving = 0U;
    if (type == KK_UI_OVERLAY_CONFIRM) {
        kk_ui.overlay.focus = KK_UI_FOCUS_CANCEL;
        kk_ui.overlay.editing = 0U;
    } else if (type == KK_UI_OVERLAY_MESSAGE) {
        kk_ui.overlay.focus = KK_UI_FOCUS_CONFIRM;
        kk_ui.overlay.editing = 0U;
    } else {
        kk_ui.overlay.focus = KK_UI_FOCUS_VALUE;
        kk_ui.overlay.editing = 1U;
    }
    kk_ui_dialog_focus_rect(kk_ui.overlay.focus, &x, &y, &width, &height);
    kk_ui.overlay.focus_x_q8 = kk_ui.overlay.focus_from_x_q8 =
        (int32_t)x * 256;
    kk_ui.overlay.focus_y_q8 = kk_ui.overlay.focus_from_y_q8 =
        (int32_t)y * 256;
    kk_ui.overlay.focus_w_q8 = kk_ui.overlay.focus_from_w_q8 =
        (int32_t)width * 256;
    kk_ui.overlay.focus_h_q8 = kk_ui.overlay.focus_from_h_q8 =
        (int32_t)height * 256;
    kk_ui.dirty = 1U;
    return KK_UI_OK;
}
#endif

KK_UI_Status KK_UI_DialogOpenInt(uint16_t index, uint32_t now)
{
#if KK_UI_ENABLE_INT_EDITOR
    KK_UI_Status status;
    if (index >= kk_ui.app->int_binding_count) {
        return KK_UI_INVALID_ARGUMENT;
    }
    status = kk_ui_dialog_begin(KK_UI_OVERLAY_INT, index, NULL,
                                KK_UI_EVENT_NONE, now);
    if (status == KK_UI_OK) {
        kk_ui.overlay.draft = *kk_ui.app->int_bindings[index].value;
        kk_ui.overlay.original = kk_ui.overlay.draft;
        kk_ui.overlay.previous = kk_ui.overlay.draft;
    }
    return status;
#else
    (void)index; (void)now;
    return KK_UI_UNSUPPORTED;
#endif
}

KK_UI_Status KK_UI_DialogOpenBool(uint16_t index, uint32_t now)
{
#if KK_UI_ENABLE_BOOL_EDITOR
    KK_UI_Status status;
    if (index >= kk_ui.app->bool_binding_count) {
        return KK_UI_INVALID_ARGUMENT;
    }
    status = kk_ui_dialog_begin(KK_UI_OVERLAY_BOOL, index, NULL,
                                KK_UI_EVENT_NONE, now);
    if (status == KK_UI_OK) {
        kk_ui.overlay.draft = *kk_ui.app->bool_bindings[index].value ? 1 : 0;
        kk_ui.overlay.original = kk_ui.overlay.draft;
        kk_ui.overlay.previous = kk_ui.overlay.draft;
    }
    return status;
#else
    (void)index; (void)now;
    return KK_UI_UNSUPPORTED;
#endif
}

KK_UI_Status KK_UI_DialogOpenConfirm(uint16_t index, uint32_t now)
{
#if KK_UI_ENABLE_CONFIRM
    if (index >= kk_ui.app->confirm_desc_count) {
        return KK_UI_INVALID_ARGUMENT;
    }
    return kk_ui_dialog_begin(KK_UI_OVERLAY_CONFIRM, index,
                              kk_ui.app->confirm_descs[index].text,
                              KK_UI_EVENT_NONE, now);
#else
    (void)index; (void)now;
    return KK_UI_UNSUPPORTED;
#endif
}

KK_UI_Status KK_UI_DialogOpenMessage(const char *text, KK_UI_EventId event,
                                     uint32_t now)
{
#if KK_UI_ENABLE_MESSAGE
    if (text == NULL) {
        return KK_UI_INVALID_ARGUMENT;
    }
    return kk_ui_dialog_begin(KK_UI_OVERLAY_MESSAGE, 0U, text, event, now);
#else
    (void)text; (void)event; (void)now;
    return KK_UI_UNSUPPORTED;
#endif
}

#if KK_UI_MODAL_ENABLED
static void kk_ui_dialog_close(uint32_t now)
{
    kk_ui.overlay.phase_from_q12 = kk_ui.overlay.phase_q12;
    kk_ui.overlay.phase_started = now;
    kk_ui.overlay.closing = 1U;
    kk_ui.overlay.editing = 0U;
    kk_ui.dirty = 1U;
}

static bool kk_ui_dialog_event_or_reject(KK_UI_EventId event)
{
    if (event == KK_UI_EVENT_NONE || KK_UI_QueueEvent(event)) {
        return true;
    }
    KK_UI_RecordError(KK_UI_QUEUE_FULL, kk_ui.current.page, event);
    kk_ui.reject_flash = 2U;
    kk_ui.dirty = 1U;
    return false;
}

static int32_t kk_ui_int_adjust(const KK_UI_IntBinding *binding,
                                int32_t value, bool increase,
                                uint16_t steps)
{
    int64_t next = value;
    int64_t amount = (int64_t)binding->step * steps;
    if (increase) {
        next += amount;
        return next > binding->maximum ? binding->maximum : (int32_t)next;
    }
    next -= amount;
    return next < binding->minimum ? binding->minimum : (int32_t)next;
}

static void kk_ui_dialog_change_value(KK_UI_InputEvent event, uint32_t now)
{
    int32_t next = kk_ui.overlay.draft;
    bool increase = event.action == KK_UI_INPUT_UP;

    if (kk_ui.overlay.type == KK_UI_OVERLAY_INT) {
        const KK_UI_IntBinding *binding =
            &kk_ui.app->int_bindings[kk_ui.overlay.ref];
        next = kk_ui_int_adjust(binding, next, increase, event.steps);
    } else if (kk_ui.overlay.type == KK_UI_OVERLAY_BOOL) {
        /* A switch has no directional meaning: either direction toggles it.
         * Ignore key auto-repeat so one long press cannot oscillate the value.
         * Batched encoder detents are equivalent to that many toggles. */
        if (event.source == KK_UI_INPUT_REPEAT || (event.steps & 1U) == 0U) {
            return;
        }
        next = kk_ui.overlay.draft == 0 ? 1 : 0;
        increase = next != 0;
    }
    if (next != kk_ui.overlay.draft) {
        kk_ui.overlay.previous = kk_ui.overlay.draft;
        kk_ui.overlay.draft = next;
        kk_ui.overlay.value_direction = increase ? 1 : -1;
        kk_ui.overlay.value_q12 = 0U;
        kk_ui.overlay.value_started = now;
        kk_ui.overlay.value_moving = 1U;
        kk_ui.dirty = 1U;
    }
}

static void kk_ui_dialog_commit(uint32_t now)
{
    KK_UI_EventId event = KK_UI_EVENT_NONE;
    if (kk_ui.overlay.type == KK_UI_OVERLAY_INT) {
        const KK_UI_IntBinding *binding =
            &kk_ui.app->int_bindings[kk_ui.overlay.ref];
        if (kk_ui.overlay.draft != kk_ui.overlay.original) {
            event = binding->changed_event;
            if (!kk_ui_dialog_event_or_reject(event)) {
                return;
            }
            *binding->value = kk_ui.overlay.draft;
        }
    } else if (kk_ui.overlay.type == KK_UI_OVERLAY_BOOL) {
        const KK_UI_BoolBinding *binding =
            &kk_ui.app->bool_bindings[kk_ui.overlay.ref];
        if (kk_ui.overlay.draft != kk_ui.overlay.original) {
            event = binding->changed_event;
            if (!kk_ui_dialog_event_or_reject(event)) {
                return;
            }
            *binding->value = kk_ui.overlay.draft != 0;
        }
    }
    kk_ui_dialog_close(now);
}

static void kk_ui_dialog_confirm_action(uint32_t now)
{
    const KK_UI_ConfirmDesc *desc =
        &kk_ui.app->confirm_descs[kk_ui.overlay.ref];
    KK_UI_EventId event = kk_ui.overlay.focus == KK_UI_FOCUS_CONFIRM ?
                          desc->confirmed_event : desc->cancelled_event;
    if (kk_ui_dialog_event_or_reject(event)) {
        kk_ui_dialog_close(now);
    }
}

void KK_UI_DialogInput(KK_UI_InputEvent event, uint32_t now)
{
    int16_t next;

    if (!KK_UI_DialogActive() || kk_ui.overlay.closing != 0U) {
        return;
    }
    if (kk_ui.overlay.type == KK_UI_OVERLAY_MESSAGE) {
        if (event.action == KK_UI_INPUT_OK &&
            kk_ui_dialog_event_or_reject(kk_ui.overlay.event)) {
            kk_ui_dialog_close(now);
        }
        return;
    }
    if (kk_ui.overlay.type == KK_UI_OVERLAY_CONFIRM) {
        if (event.action == KK_UI_INPUT_OK) {
            kk_ui_dialog_confirm_action(now);
        } else {
            kk_ui_dialog_set_focus(
                kk_ui.overlay.focus == KK_UI_FOCUS_CANCEL ?
                KK_UI_FOCUS_CONFIRM : KK_UI_FOCUS_CANCEL, now);
        }
        return;
    }
    if (kk_ui.overlay.editing != 0U) {
        if (event.action == KK_UI_INPUT_OK) {
            kk_ui.overlay.editing = 0U;
            kk_ui_dialog_set_focus(KK_UI_FOCUS_CONFIRM, now);
        } else {
            kk_ui_dialog_change_value(event, now);
        }
        return;
    }
    if (event.action == KK_UI_INPUT_OK) {
        if (kk_ui.overlay.focus == KK_UI_FOCUS_VALUE) {
            kk_ui.overlay.editing = 1U;
            kk_ui.dirty = 1U;
        } else if (kk_ui.overlay.focus == KK_UI_FOCUS_CONFIRM) {
            kk_ui_dialog_commit(now);
        } else {
            kk_ui_dialog_close(now);
        }
        return;
    }
    next = (int16_t)kk_ui.overlay.focus +
           (event.action == KK_UI_INPUT_UP ? -1 : 1);
    if (next < KK_UI_FOCUS_VALUE) {
        next = KK_UI_FOCUS_CANCEL;
    } else if (next > KK_UI_FOCUS_CANCEL) {
        next = KK_UI_FOCUS_VALUE;
    }
    kk_ui_dialog_set_focus((uint8_t)next, now);
}

#else

void KK_UI_DialogInput(KK_UI_InputEvent event, uint32_t now)
{
    (void)event;
    (void)now;
}

#endif

static void kk_ui_toast_animate(uint32_t now)
{
#if KK_UI_ENABLE_TOAST
    if (kk_ui.toast.visible == 0U) {
        return;
    }
    if (kk_ui.toast.closing == 0U &&
        (int32_t)(now - kk_ui.toast.expires_at) >= 0) {
        kk_ui.toast.closing = 1U;
        kk_ui.toast.shown_at = now;
    }
    if (kk_ui.toast.closing != 0U) {
        uint16_t p = KK_UI_EaseQ12(now - kk_ui.toast.shown_at, KK_UI_TOAST_MS);
        kk_ui.toast.phase_q12 = (uint16_t)(KK_UI_Q12_ONE - p);
        kk_ui.dirty = 1U;
        if (p == KK_UI_Q12_ONE) {
            kk_ui.toast.visible = 0U;
        }
    } else if (kk_ui.toast.phase_q12 < KK_UI_Q12_ONE) {
        kk_ui.toast.phase_q12 = KK_UI_EaseQ12(now - kk_ui.toast.shown_at,
                                              KK_UI_TOAST_MS);
        kk_ui.dirty = 1U;
    }
#else
    (void)now;
#endif
}

void KK_UI_DialogAnimate(uint32_t now)
{
#if KK_UI_MODAL_ENABLED
    if (KK_UI_DialogActive()) {
        uint32_t elapsed = now - kk_ui.overlay.phase_started;
        bool redraw = false;
        if (kk_ui.overlay.closing != 0U) {
            uint16_t p = KK_UI_EaseQ12(elapsed, KK_UI_DIALOG_CLOSE_MS);
            kk_ui.overlay.phase_q12 = (uint16_t)KK_UI_LerpQ12(
                kk_ui.overlay.phase_from_q12, 0, p);
            redraw = true;
            if (p == KK_UI_Q12_ONE) {
                kk_ui.overlay.type = KK_UI_OVERLAY_NONE;
                kk_ui.overlay.closing = 0U;
            }
        } else if (kk_ui.overlay.phase_q12 < KK_UI_Q12_ONE) {
            kk_ui.overlay.phase_q12 = KK_UI_EaseQ12(elapsed,
                                                    KK_UI_DIALOG_OPEN_MS);
            redraw = true;
        }
        if (kk_ui.overlay.focus_moving != 0U) {
            redraw = true;
            if (now - kk_ui.overlay.focus_started >= KK_UI_FOCUS_MS) {
                kk_ui.overlay.focus_moving = 0U;
            }
        }
        if (kk_ui.overlay.value_moving != 0U) {
            uint32_t duration = kk_ui.overlay.type == KK_UI_OVERLAY_BOOL ?
                                KK_UI_BOOL_VALUE_MS : KK_UI_INT_VALUE_MS;
            redraw = true;
            kk_ui.overlay.value_q12 = KK_UI_EaseQ12(
                now - kk_ui.overlay.value_started, duration);
            if (kk_ui.overlay.value_q12 == KK_UI_Q12_ONE) {
                kk_ui.overlay.value_moving = 0U;
            }
        }
        if (redraw) {
            kk_ui.dirty = 1U;
        }
    }
#endif
    kk_ui_toast_animate(now);
}

KK_UI_Status KK_UI_ToastOpen(const char *text, uint32_t duration,
                             uint32_t now)
{
#if KK_UI_ENABLE_TOAST
    if (text == NULL) {
        return KK_UI_INVALID_ARGUMENT;
    }
    if (KK_UI_DialogActive()) {
        return KK_UI_BUSY;
    }
    if (duration == 0U) {
        duration = KK_UI_TOAST_DEFAULT_MS;
    }
    if (duration > (uint32_t)INT32_MAX) {
        duration = (uint32_t)INT32_MAX;
    }
    kk_ui.toast.text = text;
    kk_ui.toast.shown_at = now;
    kk_ui.toast.expires_at = now + duration;
    kk_ui.toast.phase_q12 = 0U;
    kk_ui.toast.visible = 1U;
    kk_ui.toast.closing = 0U;
    kk_ui.dirty = 1U;
    return KK_UI_OK;
#else
    (void)text; (void)duration; (void)now;
    return KK_UI_UNSUPPORTED;
#endif
}

#if KK_UI_MODAL_ENABLED
static const char *kk_ui_dialog_title(void)
{
    if (kk_ui.overlay.type == KK_UI_OVERLAY_INT) {
        return kk_ui.app->int_bindings[kk_ui.overlay.ref].title;
    }
    if (kk_ui.overlay.type == KK_UI_OVERLAY_BOOL) {
        return kk_ui.app->bool_bindings[kk_ui.overlay.ref].title;
    }
    return kk_ui.app->texts.message_title;
}

static void kk_ui_draw_dialog_text(const char *text)
{
    const char *newline;
    char first[32];
    size_t length = 0U;

    if (text == NULL) {
        return;
    }
    newline = text;
    while (*newline != '\0' && *newline != '\n') {
        ++newline;
    }
    if (*newline == '\n') {
        while (text[length] != '\n' && length + 1U < sizeof(first)) {
            first[length] = text[length];
            ++length;
        }
        first[length] = '\0';
        KK_UI_DrawCentered(10, 4, 108U, first);
        KK_UI_DrawCentered(10, 20, 108U, newline + 1);
    } else {
        KK_UI_DrawCentered(10, 13, 108U, text);
    }
}

static void kk_ui_draw_int_value(void)
{
    const KK_UI_IntBinding *binding =
        &kk_ui.app->int_bindings[kk_ui.overlay.ref];
    char old_text[20];
    char new_text[20];

    KK_UI_FormatInt(kk_ui.overlay.previous, binding->unit,
                    old_text, sizeof(old_text));
    KK_UI_FormatInt(kk_ui.overlay.draft, binding->unit,
                    new_text, sizeof(new_text));
    OLED_SetClipWindow(39, 21, 46U, 16U);
    if (kk_ui.overlay.value_moving != 0U) {
        int16_t offset = (int16_t)(16 * kk_ui.overlay.value_q12 /
                                   KK_UI_Q12_ONE);
        if (kk_ui.overlay.value_direction > 0) {
            KK_UI_DrawCentered(39, (int16_t)(21 - offset), 46U, old_text);
            KK_UI_DrawCentered(39, (int16_t)(37 - offset), 46U, new_text);
        } else {
            KK_UI_DrawCentered(39, (int16_t)(21 + offset), 46U, old_text);
            KK_UI_DrawCentered(39, (int16_t)(5 + offset), 46U, new_text);
        }
    } else {
        KK_UI_DrawCentered(39, 21, 46U, new_text);
    }
}

static void kk_ui_draw_bool_value(void)
{
    bool on = kk_ui.overlay.draft != 0;
    uint16_t p = kk_ui.overlay.value_moving != 0U ?
                 kk_ui.overlay.value_q12 : KK_UI_Q12_ONE;
    KK_UI_DrawCentered(31, 21, 14U, kk_ui.app->texts.off_text);
    KK_UI_DrawCentered(83, 21, 14U, kk_ui.app->texts.on_text);
    KK_UI_DrawSwitch(48, 22, 33U, 14U, on, p);
}

void KK_UI_DialogDraw(void)
{
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
    int32_t focus_x;
    int32_t focus_y;
    int32_t focus_w;
    int32_t focus_h;
    uint16_t p;

    if (!KK_UI_DialogActive()) {
        return;
    }
    p = kk_ui.overlay.phase_q12;
    x = (int16_t)(3 + 3 * p / KK_UI_Q12_ONE);
    y = (int16_t)(kk_ui.overlay.origin_y +
                  ((int32_t)1 - kk_ui.overlay.origin_y) * p /
                  KK_UI_Q12_ONE);
    width = (int16_t)(118 - 2 * p / KK_UI_Q12_ONE);
    height = (int16_t)(16 + 45 * p / KK_UI_Q12_ONE);
    OLED_ResetClipWindow();
    OLED_SetDrawMode(OLED_DRAW_CLEAR);
    OLED_DrawRBox(x, y, (uint16_t)width, (uint16_t)height, 3U);
    OLED_SetDrawMode(OLED_DRAW_SET);
    OLED_DrawRFrame(x, y, (uint16_t)width, (uint16_t)height, 3U);
    if (width <= 2 || height <= 2) {
        return;
    }
    OLED_SetClipWindow((int16_t)(x + 1), (int16_t)(y + 1),
                       (uint16_t)(width - 2), (uint16_t)(height - 2));
    OLED_SetFont(kk_ui.app->fonts.body_font);
    if (kk_ui.overlay.type == KK_UI_OVERLAY_CONFIRM) {
        kk_ui_draw_dialog_text(kk_ui.overlay.text);
        KK_UI_DrawCentered(15, 41, 36U, kk_ui.app->texts.cancel_text);
        KK_UI_DrawCentered(77, 41, 36U, kk_ui.app->texts.confirm_text);
    } else {
        KK_UI_DrawCentered(10, 3, 108U, kk_ui_dialog_title());
        OLED_DrawHLine(12, 19, 104U);
        if (kk_ui.overlay.type == KK_UI_OVERLAY_INT) {
            kk_ui_draw_int_value();
            OLED_SetClipWindow((int16_t)(x + 1), (int16_t)(y + 1),
                               (uint16_t)(width - 2),
                               (uint16_t)(height - 2));
            KK_UI_DrawCentered(15, 41, 36U, kk_ui.app->texts.cancel_text);
            KK_UI_DrawCentered(77, 41, 36U, kk_ui.app->texts.confirm_text);
            if (kk_ui.overlay.editing != 0U) {
                const KK_UI_IntBinding *binding =
                    &kk_ui.app->int_bindings[kk_ui.overlay.ref];
                if (kk_ui.overlay.draft < binding->maximum) {
                    OLED_DrawLine(91, 26, 95, 22);
                    OLED_DrawLine(95, 22, 99, 26);
                }
                if (kk_ui.overlay.draft > binding->minimum) {
                    OLED_DrawLine(91, 31, 95, 35);
                    OLED_DrawLine(95, 35, 99, 31);
                }
            }
        } else if (kk_ui.overlay.type == KK_UI_OVERLAY_BOOL) {
            kk_ui_draw_bool_value();
            KK_UI_DrawCentered(15, 41, 36U, kk_ui.app->texts.cancel_text);
            KK_UI_DrawCentered(77, 41, 36U, kk_ui.app->texts.confirm_text);
        } else {
            KK_UI_DrawCentered(8, 23, 112U, kk_ui.overlay.text);
            KK_UI_DrawCentered(46, 41, 36U, kk_ui.app->texts.confirm_text);
        }
    }
    kk_ui_dialog_current_focus(kk_ui.last_update, &focus_x, &focus_y,
                               &focus_w, &focus_h);
    OLED_SetDrawMode(OLED_DRAW_XOR);
    OLED_DrawRBox(KK_UI_RoundQ8(focus_x), KK_UI_RoundQ8(focus_y),
                  (uint16_t)KK_UI_RoundQ8(focus_w),
                  (uint16_t)KK_UI_RoundQ8(focus_h), 3U);
    OLED_SetDrawMode(OLED_DRAW_SET);
    OLED_ResetClipWindow();
}

#else

void KK_UI_DialogDraw(void)
{
}

#endif

void KK_UI_ToastDraw(void)
{
#if KK_UI_ENABLE_TOAST
    uint16_t width;
    int16_t x;
    int16_t y;
    if (kk_ui.toast.visible == 0U || KK_UI_DialogActive()) {
        return;
    }
    OLED_SetFont(kk_ui.app->fonts.body_font);
    width = (uint16_t)(OLED_GetUTF8Width(kk_ui.toast.text) + 12U);
    if (width > 120U) {
        width = 120U;
    }
    if ((width & 1U) != 0U) {
        ++width;
    }
    x = (int16_t)((KK_UI_SCREEN_WIDTH - width) / 2);
    y = (int16_t)(KK_UI_SCREEN_HEIGHT -
                  (16 * kk_ui.toast.phase_q12 / KK_UI_Q12_ONE));
    OLED_SetClipWindow(x, y, width, 16U);

    /* Make the overlay independent of the page beneath it. Clearing the full
     * bounding box also leaves clean black corner pixels around the rounded
     * white label instead of allowing page glyphs to touch its silhouette. */
    OLED_SetDrawMode(OLED_DRAW_CLEAR);
    OLED_DrawBox(x, y, width, 16U);
    OLED_SetDrawMode(OLED_DRAW_SET);
    OLED_DrawRBox(x, y, width, 16U, 3U);

    /* Draw black text into the opaque white label. */
    OLED_SetDrawMode(OLED_DRAW_CLEAR);
    KK_UI_DrawCentered(x, (int16_t)(y + 1), width, kk_ui.toast.text);
    OLED_SetDrawMode(OLED_DRAW_SET);
    OLED_ResetClipWindow();
#endif
}
