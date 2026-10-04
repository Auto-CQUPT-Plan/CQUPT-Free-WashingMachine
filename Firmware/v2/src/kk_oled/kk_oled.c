#include "kk_oled_internal.h"

#include <limits.h>
#include <string.h>

/* 核心层负责双缓冲、差分提交、画布状态和逻辑像素到物理显存的映射。 */

/** 刷新请求使用的底层传输方式。 */
typedef enum {
    OLED_UPDATE_BLOCKING = 0, /**< 阻塞发送命令和数据。 */
    OLED_UPDATE_IT,           /**< 命令、数据均使用中断发送。 */
    OLED_UPDATE_DMA           /**< 命令用中断，数据用 DMA。 */
} OLED_UpdateMode;

static uint8_t oled_buffers[2][OLED_BUFFER_SIZE]; /**< 库内静态双缓冲，按页连续存放。 */
static uint8_t oled_stable_index;                /**< 最近稳定可完整重发帧的缓冲区索引。 */
static uint8_t oled_draw_index = 1U;             /**< 用户当前正在绘制的缓冲区索引。 */
static uint8_t oled_transfer_index = 1U;         /**< 本次传输期间被冻结的缓冲区索引。 */
static uint8_t oled_dirty_min[OLED_PHYSICAL_PAGES]; /**< 每页首个差异列。 */
static uint8_t oled_dirty_max[OLED_PHYSICAL_PAGES]; /**< 每页最后一个差异列。 */
static volatile bool oled_async_pending;         /**< 核心是否等待异步传输完成。 */
static bool oled_force_full;                     /**< 下次刷新是否必须发送全屏。 */
static bool oled_initialized;                    /**< 屏幕和核心状态是否初始化成功。 */
static bool oled_power_save;                     /**< 当前是否处于显示关闭状态。 */
static volatile OLED_Status oled_last_status = OLED_OK; /**< 最近操作的可查询状态。 */

static OLED_Rotation oled_rotation = OLED_ROTATION_0; /**< 当前画布旋转方向。 */
static OLED_DrawMode oled_draw_mode = OLED_DRAW_SET;  /**< 当前前景像素写入方式。 */
static OLED_BackgroundMode oled_background_mode = OLED_BG_TRANSPARENT; /**< 背景写入方式。 */
static int16_t oled_clip_x0; /**< 裁剪窗口左边界，包含该列。 */
static int16_t oled_clip_y0; /**< 裁剪窗口上边界，包含该行。 */
static int16_t oled_clip_x1 = (int16_t)OLED_PHYSICAL_WIDTH;  /**< 裁剪窗口右边界，不包含。 */
static int16_t oled_clip_y1 = (int16_t)OLED_PHYSICAL_HEIGHT; /**< 裁剪窗口下边界，不包含。 */

/** 将全部页标记为“没有差异”。 */
static void oled_reset_dirty(void)
{
    uint8_t page;

    for (page = 0U; page < OLED_PHYSICAL_PAGES; ++page) {
        oled_dirty_min[page] = OLED_PHYSICAL_WIDTH;
        oled_dirty_max[page] = 0U;
    }
}

/** 比较新旧帧，为每页生成一个首尾差异列区间。 */
static bool oled_prepare_dirty(uint8_t new_index, uint8_t old_index, bool full)
{
    uint8_t page;    /* 当前扫描的物理页。 */
    bool any = false; /* 是否至少存在一个需要发送的页。 */

    oled_reset_dirty();
    for (page = 0U; page < OLED_PHYSICAL_PAGES; ++page) {
        uint16_t base = (uint16_t)page * OLED_PHYSICAL_WIDTH; /* 当前页首字节。 */
        uint16_t x; /* 当前比较列。 */

        /* 传输失败后的恢复刷新直接覆盖整页，不再比较旧帧。 */
        if (full) {
            oled_dirty_min[page] = 0U;
            oled_dirty_max[page] = OLED_PHYSICAL_WIDTH - 1U;
            any = true;
            continue;
        }

        /* 从左、右两端分别寻找差异，得到本页唯一连续发送区间。 */
        for (x = 0U; x < OLED_PHYSICAL_WIDTH; ++x) {
            if (oled_buffers[new_index][base + x] != oled_buffers[old_index][base + x]) {
                oled_dirty_min[page] = (uint8_t)x;
                break;
            }
        }
        if (x == OLED_PHYSICAL_WIDTH) {
            continue;
        }
        for (x = OLED_PHYSICAL_WIDTH; x > 0U; --x) {
            uint16_t column = x - 1U;
            if (oled_buffers[new_index][base + column] != oled_buffers[old_index][base + column]) {
                oled_dirty_max[page] = (uint8_t)column;
                break;
            }
        }
        any = true;
    }
    return any;
}

/** 完成同步或空差异帧提交，并清空交换后的绘制缓冲区。 */
static void oled_commit_blocking(void)
{
    uint8_t old_stable = oled_stable_index; /* 提交后复用为下一帧绘制缓冲区。 */

    /* 新帧成为稳定重发基准；旧稳定帧清零后交给用户绘制下一帧。 */
    oled_stable_index = oled_draw_index;
    oled_draw_index = old_stable;
    memset(oled_buffers[oled_draw_index], 0, OLED_BUFFER_SIZE);
    oled_force_full = false;
    oled_last_status = OLED_OK;
}

/** 统一处理三种刷新入口的差分准备、驱动启动和缓冲区交换。 */
static OLED_Status oled_begin_update(OLED_UpdateMode mode)
{
    OLED_Status status; /* 驱动启动或阻塞传输结果。 */

    if (!oled_initialized) {
        oled_last_status = OLED_ERROR;
        return OLED_ERROR;
    }
    if (OLED_IsBusy()) {
        return OLED_BUSY;
    }

    /* 在接触总线前固定传输帧并计算各页差异区间。 */
    oled_transfer_index = oled_draw_index;
    if (!oled_prepare_dirty(oled_transfer_index, oled_stable_index, oled_force_full)) {
        /* 空差异帧不访问总线，但仍按正常帧完成缓冲区交换。 */
        oled_commit_blocking();
        return OLED_OK;
    }

    if (mode == OLED_UPDATE_BLOCKING) {
        status = OLED_DriverWriteBlocking();
        if (status == OLED_OK) {
            oled_commit_blocking();
        } else {
            /* 屏幕可能只收到半帧，下次必须完整覆盖。 */
            oled_force_full = true;
            oled_last_status = status;
        }
        return status;
    }

    status = (mode == OLED_UPDATE_IT) ? OLED_DriverWriteIT() : OLED_DriverWriteDMA();
    if (status != OLED_OK) {
        oled_force_full = true;
        oled_last_status = status;
        return status;
    }

    /* 传输缓冲区被冻结；旧稳定缓冲区立即成为下一帧绘制缓冲区。 */
    oled_draw_index = oled_stable_index;
    memset(oled_buffers[oled_draw_index], 0, OLED_BUFFER_SIZE);
    oled_async_pending = true;
    oled_last_status = OLED_BUSY;
    return OLED_OK;
}

OLED_Status OLED_Init(void)
{
    OLED_Status status; /* 屏幕驱动初始化结果。 */

    if (OLED_IsBusy()) {
        return OLED_BUSY;
    }

    /* 恢复所有核心状态，保证重复初始化也从确定的空白帧开始。 */
    memset(oled_buffers, 0, sizeof(oled_buffers));
    oled_stable_index = 0U;
    oled_draw_index = 1U;
    oled_transfer_index = 1U;
    oled_async_pending = false;
    oled_force_full = false;
    oled_power_save = false;
    oled_rotation = OLED_ROTATION_0;
    oled_draw_mode = OLED_DRAW_SET;
    oled_background_mode = OLED_BG_TRANSPARENT;
    OLED_ResetClipWindow();

    status = OLED_DriverInit();
    oled_initialized = (status == OLED_OK);
    oled_last_status = status;
    return status;
}

OLED_Status OLED_Update(void)
{
    return oled_begin_update(OLED_UPDATE_BLOCKING);
}

OLED_Status OLED_UpdateIT(void)
{
    return oled_begin_update(OLED_UPDATE_IT);
}

OLED_Status OLED_UpdateDMA(void)
{
    return oled_begin_update(OLED_UPDATE_DMA);
}

bool OLED_IsBusy(void)
{
    return oled_async_pending || OLED_DriverIsBusy();
}

OLED_Status OLED_GetLastStatus(void)
{
    return oled_last_status;
}

OLED_Status OLED_SetContrast(uint8_t value)
{
    OLED_Status status;

    if (OLED_IsBusy()) {
        return OLED_BUSY;
    }
    status = OLED_DriverSetContrast(value);
    oled_last_status = status;
    return status;
}

OLED_Status OLED_SetPowerSave(bool enable)
{
    OLED_Status status; /* 关屏命令、恢复帧或开屏命令的结果。 */

    if (OLED_IsBusy()) {
        return OLED_BUSY;
    }
    if (!oled_initialized) {
        oled_last_status = OLED_ERROR;
        return OLED_ERROR;
    }
    if (enable == oled_power_save) {
        oled_last_status = OLED_OK;
        return OLED_OK;
    }

    if (enable) {
        status = OLED_DriverSetPowerSave(true);
        if (status == OLED_OK) {
            oled_power_save = true;
        }
        oled_last_status = status;
        return status;
    }

    /* 显示保持关闭，先完整恢复最近稳定帧，再发送 AF 点亮。 */
    oled_transfer_index = oled_stable_index;
    (void)oled_prepare_dirty(oled_transfer_index, oled_transfer_index, true);
    status = OLED_DriverWriteBlocking();
    if (status == OLED_OK) {
        status = OLED_DriverSetPowerSave(false);
    }
    if (status == OLED_OK) {
        oled_power_save = false;
        /* 唤醒重发不消费异步错误约定的“下一次正常刷新全刷”标志。 */
    } else {
        oled_force_full = true;
    }
    oled_last_status = status;
    return status;
}

uint16_t OLED_GetWidth(void)
{
    return OLED_InternalGetLogicalWidth();
}

uint16_t OLED_GetHeight(void)
{
    return OLED_InternalGetLogicalHeight();
}

void OLED_Clear(void)
{
    memset(oled_buffers[oled_draw_index], 0, OLED_BUFFER_SIZE);
}

void OLED_Fill(void)
{
    memset(oled_buffers[oled_draw_index], 0xFF, OLED_BUFFER_SIZE);
}

void OLED_SetRotation(OLED_Rotation rotation)
{
    if (rotation > OLED_ROTATION_270) {
        rotation = OLED_ROTATION_0;
    }
    oled_rotation = rotation;
    OLED_ResetClipWindow();
}

void OLED_SetDrawMode(OLED_DrawMode mode)
{
    if (mode <= OLED_DRAW_XOR) {
        oled_draw_mode = mode;
    }
}

void OLED_SetBackgroundMode(OLED_BackgroundMode mode)
{
    if (mode <= OLED_BG_SOLID) {
        oled_background_mode = mode;
    }
}

void OLED_SetClipWindow(int16_t x, int16_t y, uint16_t width, uint16_t height)
{
    int32_t x1 = (int32_t)x + width; /* 请求窗口的半开右边界。 */
    int32_t y1 = (int32_t)y + height; /* 请求窗口的半开下边界。 */
    int32_t logical_width = OLED_InternalGetLogicalWidth();   /* 当前逻辑宽度。 */
    int32_t logical_height = OLED_InternalGetLogicalHeight(); /* 当前逻辑高度。 */

    if (width > (uint16_t)INT16_MAX || height > (uint16_t)INT16_MAX) {
        return;
    }

    /* 先裁剪左上角，再裁剪右下角，并保持窗口不会出现反向区间。 */
    if (x < 0) {
        oled_clip_x0 = 0;
    } else if (x > logical_width) {
        oled_clip_x0 = (int16_t)logical_width;
    } else {
        oled_clip_x0 = x;
    }
    if (y < 0) {
        oled_clip_y0 = 0;
    } else if (y > logical_height) {
        oled_clip_y0 = (int16_t)logical_height;
    } else {
        oled_clip_y0 = y;
    }

    if (x1 < oled_clip_x0) {
        x1 = oled_clip_x0;
    }
    if (y1 < oled_clip_y0) {
        y1 = oled_clip_y0;
    }
    if (x1 > logical_width) {
        x1 = logical_width;
    }
    if (y1 > logical_height) {
        y1 = logical_height;
    }
    oled_clip_x1 = (int16_t)x1;
    oled_clip_y1 = (int16_t)y1;
}

void OLED_ResetClipWindow(void)
{
    oled_clip_x0 = 0;
    oled_clip_y0 = 0;
    oled_clip_x1 = (int16_t)OLED_InternalGetLogicalWidth();
    oled_clip_y1 = (int16_t)OLED_InternalGetLogicalHeight();
}

void OLED_InternalPlotSource(int16_t x, int16_t y, bool source_pixel)
{
    int16_t physical_x; /* 旋转映射后的物理列。 */
    int16_t physical_y; /* 旋转映射后的物理行。 */
    uint16_t index;     /* 页式缓冲区中的字节下标。 */
    uint8_t mask;       /* 该字节内对应物理行的位掩码。 */
    uint8_t *value;     /* 当前绘制缓冲区的目标字节。 */

    /* 所有上层图元统一在这里完成半开区间裁剪。 */
    if (x < oled_clip_x0 || x >= oled_clip_x1 || y < oled_clip_y0 || y >= oled_clip_y1) {
        return;
    }
    if (!source_pixel) {
        if (oled_background_mode == OLED_BG_TRANSPARENT || oled_draw_mode == OLED_DRAW_XOR) {
            return;
        }
    }

    /* 将旋转后的逻辑画布坐标还原到固定的 128×64 物理坐标。 */
    switch (oled_rotation) {
    case OLED_ROTATION_90:
        physical_x = (int16_t)(OLED_PHYSICAL_WIDTH - 1U) - y;
        physical_y = x;
        break;
    case OLED_ROTATION_180:
        physical_x = (int16_t)(OLED_PHYSICAL_WIDTH - 1U) - x;
        physical_y = (int16_t)(OLED_PHYSICAL_HEIGHT - 1U) - y;
        break;
    case OLED_ROTATION_270:
        physical_x = y;
        physical_y = (int16_t)(OLED_PHYSICAL_HEIGHT - 1U) - x;
        break;
    case OLED_ROTATION_0:
    default:
        physical_x = x;
        physical_y = y;
        break;
    }

    /* 页式布局：同一列的连续 8 行存放在一个字节中。 */
    index = (uint16_t)physical_x + ((uint16_t)physical_y >> 3U) * OLED_PHYSICAL_WIDTH;
    mask = (uint8_t)(1U << ((uint16_t)physical_y & 7U));
    value = &oled_buffers[oled_draw_index][index];

    /* 前景像素服从 SET/CLEAR/XOR；实心背景使用 SET/CLEAR 的反操作。 */
    if (source_pixel) {
        if (oled_draw_mode == OLED_DRAW_SET) {
            *value |= mask;
        } else if (oled_draw_mode == OLED_DRAW_CLEAR) {
            *value &= (uint8_t)~mask;
        } else {
            *value ^= mask;
        }
    } else if (oled_draw_mode == OLED_DRAW_SET) {
        *value &= (uint8_t)~mask;
    } else {
        *value |= mask;
    }
}

void OLED_InternalPlot(int16_t x, int16_t y)
{
    OLED_InternalPlotSource(x, y, true);
}

uint16_t OLED_InternalGetLogicalWidth(void)
{
    return (oled_rotation == OLED_ROTATION_90 || oled_rotation == OLED_ROTATION_270)
               ? OLED_PHYSICAL_HEIGHT
               : OLED_PHYSICAL_WIDTH;
}

uint16_t OLED_InternalGetLogicalHeight(void)
{
    return (oled_rotation == OLED_ROTATION_90 || oled_rotation == OLED_ROTATION_270)
               ? OLED_PHYSICAL_WIDTH
               : OLED_PHYSICAL_HEIGHT;
}

void OLED_InternalGetClip(int16_t *x0, int16_t *y0, int16_t *x1, int16_t *y1)
{
    *x0 = oled_clip_x0;
    *y0 = oled_clip_y0;
    *x1 = oled_clip_x1;
    *y1 = oled_clip_y1;
}

bool OLED_InternalIntersectClip(int32_t *x0, int32_t *y0,
                                int32_t *x1, int32_t *y1)
{
    if (*x0 < oled_clip_x0) {
        *x0 = oled_clip_x0;
    }
    if (*y0 < oled_clip_y0) {
        *y0 = oled_clip_y0;
    }
    if (*x1 > oled_clip_x1) {
        *x1 = oled_clip_x1;
    }
    if (*y1 > oled_clip_y1) {
        *y1 = oled_clip_y1;
    }
    return *x0 < *x1 && *y0 < *y1;
}

const uint8_t *OLED_InternalGetTransferBuffer(void)
{
    return oled_buffers[oled_transfer_index];
}

uint8_t OLED_InternalGetTransferMinX(uint8_t page)
{
    return (page < OLED_PHYSICAL_PAGES) ? oled_dirty_min[page] : OLED_PHYSICAL_WIDTH;
}

uint8_t OLED_InternalGetTransferMaxX(uint8_t page)
{
    return (page < OLED_PHYSICAL_PAGES) ? oled_dirty_max[page] : 0U;
}

void OLED_InternalTransferFinished(OLED_Status status)
{
    if (!oled_async_pending) {
        return;
    }

    /* 无论成功与否，冻结帧都是下一次休眠恢复时可完整重发的稳定图像。 */
    oled_stable_index = oled_transfer_index;
    oled_async_pending = false;
    oled_last_status = status;
    if (status == OLED_OK) {
        oled_force_full = false;
    } else {
        /* 异步失败后屏幕内容不可信，下一帧强制全屏恢复。 */
        oled_force_full = true;
    }
}

#if defined(KK_OLED_TEST)
const uint8_t *OLED_InternalTestGetDrawBuffer(void)
{
    return oled_buffers[oled_draw_index];
}

const uint8_t *OLED_InternalTestGetStableBuffer(void)
{
    return oled_buffers[oled_stable_index];
}

bool OLED_InternalTestIsForceFull(void)
{
    return oled_force_full;
}
#endif
