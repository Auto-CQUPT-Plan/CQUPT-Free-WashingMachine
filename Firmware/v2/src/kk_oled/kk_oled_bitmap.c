#include "kk_oled_internal.h"

#include <stddef.h>
#include <stdint.h>

/* 位图层只处理 XBM 取样与旋转，最终像素仍交给核心执行裁剪和绘图模式。 */

/* 0 至 90 度正弦，Q15；其余象限通过对称关系得到。 */
static const int16_t oled_sin_quarter_q15[91] = {
    0, 572, 1144, 1715, 2286, 2856, 3425, 3993, 4560, 5126, 5690, 6252, 6813,
    7371, 7927, 8481, 9032, 9580, 10126, 10668, 11207, 11743, 12275, 12803,
    13328, 13848, 14364, 14876, 15383, 15886, 16383, 16876, 17364, 17846,
    18323, 18794, 19260, 19720, 20173, 20621, 21062, 21497, 21925, 22347,
    22762, 23170, 23571, 23964, 24351, 24730, 25101, 25465, 25821, 26169,
    26509, 26841, 27165, 27481, 27788, 28087, 28377, 28659, 28932, 29196,
    29451, 29697, 29934, 30162, 30381, 30591, 30791, 30982, 31163, 31335,
    31498, 31650, 31794, 31927, 32051, 32165, 32269, 32364, 32448, 32523,
    32587, 32642, 32687, 32722, 32747, 32762, 32767
};

/** 将任意整数角归一化到 0～359。 */
static int16_t oled_angle_0_359(int16_t angle)
{
    int32_t normalized = angle; /* 使用较宽类型避免取模过程受符号范围影响。 */

    normalized %= 360;
    if (normalized < 0) {
        normalized += 360;
    }
    return (int16_t)normalized;
}

/** 利用四象限对称关系查询 Q15 正弦值。 */
int16_t OLED_InternalSinQ15(int16_t angle)
{
    int16_t normalized = oled_angle_0_359(angle); /* 归一化后的查表角度。 */

    if (normalized <= 90) {
        return oled_sin_quarter_q15[normalized];
    }
    if (normalized <= 180) {
        return oled_sin_quarter_q15[180 - normalized];
    }
    if (normalized <= 270) {
        return (int16_t)-oled_sin_quarter_q15[normalized - 180];
    }
    return (int16_t)-oled_sin_quarter_q15[360 - normalized];
}

/** 通过 sin(angle+90°) 得到 Q15 余弦值。 */
int16_t OLED_InternalCosQ15(int16_t angle)
{
    int16_t normalized = oled_angle_0_359(angle);

    return OLED_InternalSinQ15((int16_t)((normalized + 90) % 360));
}

/** 位图单边不得超过当前逻辑画布的较长边。 */
static bool oled_bitmap_size_valid(uint16_t width, uint16_t height)
{
    uint16_t logical_width = OLED_InternalGetLogicalWidth();
    uint16_t logical_height = OLED_InternalGetLogicalHeight();
    uint16_t extent = (logical_width > logical_height) ? logical_width : logical_height;

    return width > 0U && height > 0U && width <= extent && height <= extent;
}

/** 读取行优先、字节内 LSB-first 的一个 XBM 源像素。 */
static bool oled_xbm_pixel(const uint8_t *bitmap, uint16_t width,
                           uint16_t x, uint16_t y)
{
    uint16_t stride = (uint16_t)((width + 7U) >> 3U); /* 每行占用字节数。 */
    uint32_t index = (uint32_t)y * stride + (x >> 3U); /* 源字节下标。 */
    return (bitmap[index] & (uint8_t)(1U << (x & 7U))) != 0U;
}

void OLED_DrawXBM(int16_t x, int16_t y, uint16_t width, uint16_t height,
                  const uint8_t *bitmap)
{
    int32_t visible_x0 = x;                  /* 裁剪后的目标左边界。 */
    int32_t visible_y0 = y;                  /* 裁剪后的目标首行。 */
    int32_t visible_x1 = (int32_t)x + width; /* 裁剪后的目标右边界。 */
    int32_t visible_y1 = (int32_t)y + height; /* 裁剪后的目标末行。 */
    int32_t destination_y;                   /* 当前可见目标行。 */

    if (bitmap == NULL || !oled_bitmap_size_valid(width, height) ||
        !OLED_InternalIntersectClip(&visible_x0, &visible_y0,
                                    &visible_x1, &visible_y1)) {
        return;
    }
    /* 只读取落入裁剪窗口的源位图子区域。 */
    for (destination_y = visible_y0; destination_y < visible_y1; ++destination_y) {
        int32_t destination_x;
        uint16_t source_y = (uint16_t)(destination_y - y);

        for (destination_x = visible_x0; destination_x < visible_x1; ++destination_x) {
            uint16_t source_x = (uint16_t)(destination_x - x);
            OLED_InternalPlotSource((int16_t)destination_x, (int16_t)destination_y,
                                    oled_xbm_pixel(bitmap, width, source_x, source_y));
        }
    }
}

/** 将 Q15 乘加结果按最近邻规则还原为整数坐标。 */
static int32_t oled_q15_round(int32_t value)
{
    if (value >= 0) {
        return (value + 16384) / 32768;
    }
    return (value - 16384) / 32768;
}

/** 将一个源位图角点正向旋转，并扩展目标包围盒。 */
static void oled_include_rotated_corner(int16_t target_x, int16_t target_y,
                                        int32_t local_x, int32_t local_y,
                                        int32_t cosine, int32_t sine,
                                        int32_t *min_x, int32_t *min_y,
                                        int32_t *max_x, int32_t *max_y)
{
    int32_t x = (int32_t)target_x +
                oled_q15_round(local_x * cosine - local_y * sine);
    int32_t y = (int32_t)target_y +
                oled_q15_round(local_x * sine + local_y * cosine);

    if (x < *min_x) {
        *min_x = x;
    }
    if (x > *max_x) {
        *max_x = x;
    }
    if (y < *min_y) {
        *min_y = y;
    }
    if (y > *max_y) {
        *max_y = y;
    }
}

void OLED_DrawRotatedXBM(int16_t target_x, int16_t target_y,
                         uint16_t width, uint16_t height,
                         const uint8_t *bitmap,
                         int16_t anchor_x, int16_t anchor_y,
                         int16_t angle)
{
    int32_t sine;    /* 旋转角 Q15 正弦。 */
    int32_t cosine;  /* 旋转角 Q15 余弦。 */
    int32_t scan_x0 = target_x; /* 旋转包围盒与裁剪窗口交集。 */
    int32_t scan_y0 = target_y;
    int32_t scan_x1 = target_x;
    int32_t scan_y1 = target_y;
    int32_t destination_y; /* 当前扫描的目标行。 */

    if (bitmap == NULL || !oled_bitmap_size_valid(width, height) ||
        anchor_x < 0 || anchor_y < 0 ||
        anchor_x >= (int16_t)width || anchor_y >= (int16_t)height) {
        return;
    }
    sine = OLED_InternalSinQ15(angle);
    cosine = OLED_InternalCosQ15(angle);

    /* 四个源像素角点决定旋转目标的保守包围盒。 */
    oled_include_rotated_corner(target_x, target_y, -anchor_x, -anchor_y,
                                cosine, sine, &scan_x0, &scan_y0, &scan_x1, &scan_y1);
    oled_include_rotated_corner(target_x, target_y,
                                (int32_t)width - 1 - anchor_x, -anchor_y,
                                cosine, sine, &scan_x0, &scan_y0, &scan_x1, &scan_y1);
    oled_include_rotated_corner(target_x, target_y, -anchor_x,
                                (int32_t)height - 1 - anchor_y,
                                cosine, sine, &scan_x0, &scan_y0, &scan_x1, &scan_y1);
    oled_include_rotated_corner(target_x, target_y,
                                (int32_t)width - 1 - anchor_x,
                                (int32_t)height - 1 - anchor_y,
                                cosine, sine, &scan_x0, &scan_y0, &scan_x1, &scan_y1);
    /* 最近邻覆盖可能越过角点中心一格，因此四周保守扩展一个像素。 */
    --scan_x0;
    --scan_y0;
    scan_x1 += 2;
    scan_y1 += 2;
    if (!OLED_InternalIntersectClip(&scan_x0, &scan_y0, &scan_x1, &scan_y1)) {
        return;
    }

    /* 只在旋转包围盒内反向取样，保证目标像素最多写入一次。 */
    for (destination_y = scan_y0; destination_y < scan_y1; ++destination_y) {
        int32_t destination_x; /* 当前扫描的目标列。 */
        int32_t dy = destination_y - target_y; /* 相对目标锚点的纵向距离。 */
        for (destination_x = scan_x0; destination_x < scan_x1; ++destination_x) {
            int32_t dx = destination_x - target_x; /* 相对目标锚点的横向距离。 */
            int32_t source_x = anchor_x +
                               oled_q15_round(dx * cosine + dy * sine); /* 反向旋转后的源列。 */
            int32_t source_y = anchor_y +
                               oled_q15_round(-dx * sine + dy * cosine); /* 反向旋转后的源行。 */

            /* 旋转后落在源位图外部的目标像素保持原样。 */
            if (source_x >= 0 && source_x < width && source_y >= 0 && source_y < height) {
                OLED_InternalPlotSource((int16_t)destination_x, (int16_t)destination_y,
                                        oled_xbm_pixel(bitmap, width,
                                                       (uint16_t)source_x,
                                                       (uint16_t)source_y));
            }
        }
    }
}
