#include "washer_app.h"

#include "generated/app_icons.h"
#include "generated/kk_app_font.h"
#include "kk_oled.h"
#include "washer.h"

#include <stddef.h>

/*
 * UI 页面结构：
 *   首页（两个图标）── 洗衣 → 模式菜单（5 个模式确认 + 亮度编辑）
 *                    └ 信息 → 关于页
 * 每个洗衣模式对应一个确认框，确认事件触发串口指令发送。
 */

enum {
    PAGE_HOME = 1,   /* 路由表第 0 项对应页面 ID 1。 */
    PAGE_LAUNDRY,
    PAGE_ABOUT
};

enum {
    EVENT_WASH_1 = 1, /* 各洗衣模式的确认事件，下标与 washer_send_mode 一致。 */
    EVENT_WASH_3,
    EVENT_WASH_4,
    EVENT_WASH_4P,
    EVENT_WASH_6,
    EVENT_BRIGHTNESS_CHANGED
};

enum {
    CONFIRM_WASH_1 = 0,
    CONFIRM_WASH_3,
    CONFIRM_WASH_4,
    CONFIRM_WASH_4P,
    CONFIRM_WASH_6
};

enum {
    INT_BRIGHTNESS = 0
};

static int32_t brightness = 80;      /* 屏幕亮度百分比。 */
static uint8_t applied_contrast;     /* 已成功下发到屏幕的对比度。 */
static uint8_t failed_contrast;      /* 上次发送失败的对比度，等值不再重试。 */

static const KK_UI_HomeItem home_items[] = {
    {"洗衣", app_icon_washer, PAGE_LAUNDRY},
    {"信息", app_icon_info, PAGE_ABOUT},
};

static const KK_UI_MenuItem laundry_items[] = {
    {"1元脱水", KK_UI_MENU_CONFIRM, CONFIRM_WASH_1},
    {"3元快洗", KK_UI_MENU_CONFIRM, CONFIRM_WASH_3},
    {"4元标准洗", KK_UI_MENU_CONFIRM, CONFIRM_WASH_4},
    {"4元加强洗", KK_UI_MENU_CONFIRM, CONFIRM_WASH_4P},
    {"6元大件洗", KK_UI_MENU_CONFIRM, CONFIRM_WASH_6},
    {"屏幕亮度", KK_UI_MENU_INT, INT_BRIGHTNESS},
};

static const KK_UI_InfoRow about_rows[] = {
    {"固件版本", "v2.0.0"},
    {"串口波特率", "2400"},
    {"适配平台", "ESP8266"},
    {"操作", "旋转选择 按下确认"},
};

static const KK_UI_HomePage home_pages[] = {
    {home_items, (uint16_t)(sizeof(home_items) / sizeof(home_items[0]))},
};

static const KK_UI_MenuPage menu_pages[] = {
    {"选择模式", laundry_items, NULL,
     (uint16_t)(sizeof(laundry_items) / sizeof(laundry_items[0]))},
};

static const KK_UI_InfoPage info_pages[] = {
    {"关于", about_rows, (uint16_t)(sizeof(about_rows) / sizeof(about_rows[0]))},
};

static const KK_UI_IntBinding int_bindings[] = {
    {"屏幕亮度", &brightness, 10, 100, 5U, "%", EVENT_BRIGHTNESS_CHANGED},
};

static const KK_UI_ConfirmDesc confirm_descs[] = {
    {"启动 1元脱水?", EVENT_WASH_1, KK_UI_EVENT_NONE},
    {"启动 3元快洗?", EVENT_WASH_3, KK_UI_EVENT_NONE},
    {"启动 4元标准洗?", EVENT_WASH_4, KK_UI_EVENT_NONE},
    {"启动 4元加强洗?", EVENT_WASH_4P, KK_UI_EVENT_NONE},
    {"启动 6元大件洗?", EVENT_WASH_6, KK_UI_EVENT_NONE},
};

static const KK_UI_PageRoute routes[] = {
    {KK_UI_PAGE_HOME, 0U},
    {KK_UI_PAGE_MENU, 0U},
    {KK_UI_PAGE_INFO, 0U},
};

static const KK_UI_App app = {
    .root_page = PAGE_HOME,
    .routes = routes,
    .route_count = (uint16_t)(sizeof(routes) / sizeof(routes[0])),
    .home_pages = home_pages,
    .home_page_count = (uint16_t)(sizeof(home_pages) / sizeof(home_pages[0])),
    .menu_pages = menu_pages,
    .menu_page_count = (uint16_t)(sizeof(menu_pages) / sizeof(menu_pages[0])),
    .info_pages = info_pages,
    .info_page_count = (uint16_t)(sizeof(info_pages) / sizeof(info_pages[0])),
    .custom_page_count = 0U,
    .int_bindings = int_bindings,
    .int_binding_count = (uint16_t)(sizeof(int_bindings) / sizeof(int_bindings[0])),
    .bool_bindings = NULL,
    .bool_binding_count = 0U,
    .confirm_descs = confirm_descs,
    .confirm_desc_count = (uint16_t)(sizeof(confirm_descs) / sizeof(confirm_descs[0])),
    .fonts = {kk_font_app_body, kk_font_app_title, kk_font_app_body},
    .texts = {"返回", "取消", "确定", "开", "关", "提示"},
};

const KK_UI_App *KK_UI_AppGet(void)
{
    return &app;
}

void KK_UI_AppReset(uint32_t now_ms)
{
    (void)now_ms;
    brightness = 80;
    applied_contrast = 0U;
    failed_contrast = 0U;
}

void KK_UI_AppUpdate(uint32_t now_ms)
{
    (void)now_ms;
}

/** 把亮度百分比换算成 SSD1306 对比度并下发，显示空闲时才发送。 */
static void app_service_contrast(void)
{
    int32_t desired = brightness;
    int32_t draft;
    uint8_t contrast;
    OLED_Status status;

    if (KK_UI_GetIntEditorDraft(INT_BRIGHTNESS, &draft)) {
        desired = draft; /* 编辑中实时预览。 */
    }
    if (!KK_UI_IsDisplayIdle()) {
        return;
    }
    contrast = (uint8_t)((desired * 255 + 50) / 100);
    if (contrast == applied_contrast || contrast == failed_contrast) {
        return;
    }
    status = OLED_SetContrast(contrast);
    if (status == OLED_OK) {
        applied_contrast = contrast;
    } else if (status == OLED_ERROR) {
        failed_contrast = contrast;
    }
}

void KK_UI_AppProcessEvents(void)
{
    KK_UI_EventId event;

    while (KK_UI_PollEvent(&event)) {
        switch (event) {
        case EVENT_WASH_1:
            washer_send_mode(0U);
            (void)KK_UI_ShowToast("指令已发送", 0U);
            break;
        case EVENT_WASH_3:
            washer_send_mode(1U);
            (void)KK_UI_ShowToast("指令已发送", 0U);
            break;
        case EVENT_WASH_4:
            washer_send_mode(2U);
            (void)KK_UI_ShowToast("指令已发送", 0U);
            break;
        case EVENT_WASH_4P:
            washer_send_mode(3U);
            (void)KK_UI_ShowToast("指令已发送", 0U);
            break;
        case EVENT_WASH_6:
            washer_send_mode(4U);
            (void)KK_UI_ShowToast("指令已发送", 0U);
            break;
        case EVENT_BRIGHTNESS_CHANGED:
        default:
            break;
        }
    }
    app_service_contrast();
}

/* custom_page_count 为 0 时不会被调用，但运行时要求这五个符号存在。 */
void KK_UI_CustomOnEnter(KK_UI_PageId page)
{
    (void)page;
}

void KK_UI_CustomOnLeave(KK_UI_PageId page)
{
    (void)page;
}

void KK_UI_CustomOnInput(KK_UI_PageId page, KK_UI_InputEvent event)
{
    (void)page;
    (void)event;
}

bool KK_UI_CustomOnTick(KK_UI_PageId page, uint32_t now_ms)
{
    (void)page;
    (void)now_ms;
    return false;
}

void KK_UI_CustomOnDraw(KK_UI_PageId page, int16_t x_offset, int16_t clip_x,
                        uint16_t clip_width)
{
    (void)page;
    (void)x_offset;
    (void)clip_x;
    (void)clip_width;
}
