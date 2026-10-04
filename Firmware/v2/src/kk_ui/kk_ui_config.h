#ifndef KK_UI_CONFIG_H
#define KK_UI_CONFIG_H

/* Fixed-capacity runtime configuration. Projects may override any value from
 * their build system or from a project-local copy of this file. */
#ifndef KK_UI_NAV_DEPTH
#define KK_UI_NAV_DEPTH 8U
#endif

#ifndef KK_UI_EVENT_QUEUE_LENGTH
#define KK_UI_EVENT_QUEUE_LENGTH 4U
#endif

#ifndef KK_UI_KEY_DEBOUNCE_MS
#define KK_UI_KEY_DEBOUNCE_MS 20U
#endif

#ifndef KK_UI_KEY_REPEAT_DELAY_MS
#define KK_UI_KEY_REPEAT_DELAY_MS 450U
#endif

#ifndef KK_UI_FRAME_INTERVAL_MS
#define KK_UI_FRAME_INTERVAL_MS 20U
#endif

#ifndef KK_UI_DISPLAY_TIMEOUT_MS
#define KK_UI_DISPLAY_TIMEOUT_MS 250U
#endif

#define KK_UI_REFRESH_BLOCKING 0
#define KK_UI_REFRESH_IT       1
#define KK_UI_REFRESH_DMA      2

#ifndef KK_UI_REFRESH_MODE
#define KK_UI_REFRESH_MODE KK_UI_REFRESH_DMA
#endif

#ifndef KK_UI_ENABLE_HOME
#define KK_UI_ENABLE_HOME 1
#endif
#ifndef KK_UI_ENABLE_MENU
#define KK_UI_ENABLE_MENU 1
#endif
#ifndef KK_UI_ENABLE_INFO
#define KK_UI_ENABLE_INFO 1
#endif
#ifndef KK_UI_ENABLE_CUSTOM
#define KK_UI_ENABLE_CUSTOM 1
#endif
#ifndef KK_UI_ENABLE_INT_EDITOR
#define KK_UI_ENABLE_INT_EDITOR 1
#endif
#ifndef KK_UI_ENABLE_BOOL_EDITOR
#define KK_UI_ENABLE_BOOL_EDITOR 1
#endif
#ifndef KK_UI_ENABLE_CONFIRM
#define KK_UI_ENABLE_CONFIRM 1
#endif
#ifndef KK_UI_ENABLE_MESSAGE
#define KK_UI_ENABLE_MESSAGE 1
#endif
#ifndef KK_UI_ENABLE_TOAST
#define KK_UI_ENABLE_TOAST 1
#endif
#ifndef KK_UI_ENABLE_DRAW_HELPERS
#define KK_UI_ENABLE_DRAW_HELPERS 1
#endif

#ifndef KK_UI_TOAST_DEFAULT_MS
#define KK_UI_TOAST_DEFAULT_MS 1500U
#endif

#if KK_UI_NAV_DEPTH < 1U
#error "KK_UI_NAV_DEPTH must be at least 1"
#endif
#if KK_UI_NAV_DEPTH > 255U
#error "KK_UI_NAV_DEPTH must fit the runtime uint8_t stack count"
#endif
#if KK_UI_EVENT_QUEUE_LENGTH < 1U
#error "KK_UI_EVENT_QUEUE_LENGTH must be at least 1"
#endif
#if KK_UI_EVENT_QUEUE_LENGTH > 255U
#error "KK_UI_EVENT_QUEUE_LENGTH must fit the runtime uint8_t queue count"
#endif
#if KK_UI_FRAME_INTERVAL_MS < 1U
#error "KK_UI_FRAME_INTERVAL_MS must be at least 1"
#endif

#endif
