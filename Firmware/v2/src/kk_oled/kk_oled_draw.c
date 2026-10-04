#include "kk_oled_internal.h"

#include <limits.h>
#include <stdint.h>

/* 几何层只生成逻辑坐标像素，统一由核心完成旋转、裁剪和绘图模式处理。 */

/** 返回 32 位整数绝对值，供直线误差计算使用。 */
static int32_t oled_abs32(int32_t value)
{
    return (value < 0) ? -value : value;
}

/** 尺寸字段虽为 uint16_t，但公共契约只接受 int16_t 可表示的范围。 */
static bool oled_dimension_valid(uint16_t value)
{
    return value <= (uint16_t)INT16_MAX;
}

/** 当前逻辑画布的较长边，也是曲线半径的安全上限。 */
static uint16_t oled_canvas_extent(void)
{
    uint16_t width = OLED_InternalGetLogicalWidth();
    uint16_t height = OLED_InternalGetLogicalHeight();

    return (width > height) ? width : height;
}

/** 判断半开包围盒是否与当前裁剪窗口相交。 */
static bool oled_bounds_visible(int32_t x0, int32_t y0,
                                int32_t x1, int32_t y1)
{
    return OLED_InternalIntersectClip(&x0, &y0, &x1, &y1);
}

/** 校验矩形尺寸，并判断其半开包围盒是否可见。 */
static bool oled_rect_visible(int16_t x, int16_t y,
                              uint16_t width, uint16_t height)
{
    if (width == 0U || height == 0U ||
        !oled_dimension_valid(width) || !oled_dimension_valid(height)) {
        return false;
    }
    return oled_bounds_visible(x, y, (int32_t)x + width, (int32_t)y + height);
}

/** 校验曲线半径，并判断完整曲线包围盒是否可能可见。 */
static bool oled_curve_visible(int16_t x, int16_t y,
                               uint16_t radius_x, uint16_t radius_y)
{
    uint16_t extent = oled_canvas_extent();

    if (radius_x > extent || radius_y > extent) {
        return false;
    }
    return oled_bounds_visible((int32_t)x - radius_x,
                               (int32_t)y - radius_y,
                               (int32_t)x + radius_x + 1,
                               (int32_t)y + radius_y + 1);
}

/** 将圆弧角度归一化到 0～359。 */
static int16_t oled_normalize_angle(int16_t angle)
{
    int32_t normalized = angle; /* 归一化过程使用较宽的有符号类型。 */

    normalized %= 360;
    if (normalized < 0) {
        normalized += 360;
    }
    return (int16_t)normalized;
}

/** 交换两个 int16_t 值，用于按纵坐标排列三角形顶点。 */
static void oled_swap_i16(int16_t *a, int16_t *b)
{
    int16_t value = *a; /* 暂存 a 的原值。 */
    *a = *b;
    *b = value;
}

void OLED_DrawPixel(int16_t x, int16_t y)
{
    OLED_InternalPlot(x, y);
}

void OLED_DrawHLine(int16_t x, int16_t y, uint16_t width)
{
    int16_t clip_x0; /* 裁剪窗口左边界。 */
    int16_t clip_y0; /* 裁剪窗口上边界。 */
    int16_t clip_x1; /* 裁剪窗口右边界，不包含。 */
    int16_t clip_y1; /* 裁剪窗口下边界，不包含。 */
    int32_t begin = x;          /* 裁剪前后的起始列。 */
    int32_t end = begin + width; /* 半开结束列，使用 int32_t 防止加法溢出。 */
    int32_t px;                 /* 当前写入列。 */

    if (width == 0U || !oled_dimension_valid(width)) {
        return;
    }
    OLED_InternalGetClip(&clip_x0, &clip_y0, &clip_x1, &clip_y1);
    if (y < clip_y0 || y >= clip_y1 || end <= clip_x0 || begin >= clip_x1) {
        return;
    }
    /* 先将线段收缩到裁剪窗口，再逐像素写入。 */
    if (begin < clip_x0) {
        begin = clip_x0;
    }
    if (end > clip_x1) {
        end = clip_x1;
    }
    for (px = begin; px < end; ++px) {
        OLED_InternalPlot((int16_t)px, y);
    }
}

void OLED_DrawVLine(int16_t x, int16_t y, uint16_t height)
{
    int16_t clip_x0; /* 裁剪窗口左边界。 */
    int16_t clip_y0; /* 裁剪窗口上边界。 */
    int16_t clip_x1; /* 裁剪窗口右边界，不包含。 */
    int16_t clip_y1; /* 裁剪窗口下边界，不包含。 */
    int32_t begin = y;           /* 裁剪前后的起始行。 */
    int32_t end = begin + height; /* 半开结束行。 */
    int32_t py;                  /* 当前写入行。 */

    if (height == 0U || !oled_dimension_valid(height)) {
        return;
    }
    OLED_InternalGetClip(&clip_x0, &clip_y0, &clip_x1, &clip_y1);
    if (x < clip_x0 || x >= clip_x1 || end <= clip_y0 || begin >= clip_y1) {
        return;
    }
    /* 先将线段收缩到裁剪窗口，再逐像素写入。 */
    if (begin < clip_y0) {
        begin = clip_y0;
    }
    if (end > clip_y1) {
        end = clip_y1;
    }
    for (py = begin; py < end; ++py) {
        OLED_InternalPlot(x, (int16_t)py);
    }
}

/** Bresenham 直线内核；include_last=false 时不写入终点。 */
static void oled_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                           bool include_last)
{
    int32_t x = x0; /* 当前横坐标。 */
    int32_t y = y0; /* 当前纵坐标。 */
    int32_t dx = oled_abs32((int32_t)x1 - x0); /* 横向距离。 */
    int32_t sx = (x0 < x1) ? 1 : -1;          /* 横向步进方向。 */
    int32_t dy = -oled_abs32((int32_t)y1 - y0); /* 负值纵向距离。 */
    int32_t sy = (y0 < y1) ? 1 : -1;           /* 纵向步进方向。 */
    int32_t error = dx + dy;                    /* Bresenham 累积误差。 */

    if (!oled_bounds_visible((x0 < x1) ? x0 : x1,
                             (y0 < y1) ? y0 : y1,
                             (int32_t)((x0 > x1) ? x0 : x1) + 1,
                             (int32_t)((y0 > y1) ? y0 : y1) + 1)) {
        return;
    }

    /* 对称 Bresenham 算法同时覆盖所有八个方向。 */
    for (;;) {
        if (x == x1 && y == y1) {
            if (include_last) {
                OLED_InternalPlot((int16_t)x, (int16_t)y);
            }
            break;
        }
        OLED_InternalPlot((int16_t)x, (int16_t)y);
        {
            int32_t doubled = error * 2; /* 用两倍误差避免小数运算。 */
            if (doubled >= dy) {
                error += dy;
                x += sx;
            }
            if (doubled <= dx) {
                error += dx;
                y += sy;
            }
        }
    }
}

void OLED_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    oled_draw_line(x0, y0, x1, y1, true);
}

void OLED_DrawFrame(int16_t x, int16_t y, uint16_t width, uint16_t height)
{
    int32_t right;  /* 最右列，包含。 */
    int32_t bottom; /* 最下行，包含。 */

    if (!oled_rect_visible(x, y, width, height)) {
        return;
    }
    /* 高度为 1 或宽度为 1 时避免重复或越界绘制对边。 */
    OLED_DrawHLine(x, y, width);
    if (height > 1U) {
        bottom = (int32_t)y + height - 1;
        if (bottom >= INT16_MIN && bottom <= INT16_MAX) {
            OLED_DrawHLine(x, (int16_t)bottom, width);
        }
    }
    if (height > 2U) {
        OLED_DrawVLine(x, (int16_t)(y + 1), (uint16_t)(height - 2U));
        if (width > 1U) {
            right = (int32_t)x + width - 1;
            if (right >= INT16_MIN && right <= INT16_MAX) {
                OLED_DrawVLine((int16_t)right, (int16_t)(y + 1),
                               (uint16_t)(height - 2U));
            }
        }
    }
}

void OLED_DrawBox(int16_t x, int16_t y, uint16_t width, uint16_t height)
{
    int16_t clip_x0; /* 由水平线内部继续处理的左边界。 */
    int16_t clip_y0; /* 裁剪后的首行下限。 */
    int16_t clip_x1; /* 由水平线内部继续处理的右边界。 */
    int16_t clip_y1; /* 裁剪后的末行上限。 */
    int32_t begin_y = y;             /* 实际开始扫描行。 */
    int32_t end_y = begin_y + height; /* 半开结束行。 */
    int32_t py;                       /* 当前填充行。 */

    if (!oled_rect_visible(x, y, width, height)) {
        return;
    }
    OLED_InternalGetClip(&clip_x0, &clip_y0, &clip_x1, &clip_y1);
    (void)clip_x0;
    (void)clip_x1;
    if (end_y <= clip_y0 || begin_y >= clip_y1) {
        return;
    }
    if (begin_y < clip_y0) {
        begin_y = clip_y0;
    }
    if (end_y > clip_y1) {
        end_y = clip_y1;
    }
    /* 实心矩形由裁剪后的多条水平线组成。 */
    for (py = begin_y; py < end_y; ++py) {
        OLED_DrawHLine(x, (int16_t)py, width);
    }
}

/** 将第一八分区的一点镜像到圆周，并跳过坐标轴和对角线上的重复点。 */
static void oled_plot_circle_points(int16_t cx, int16_t cy, int32_t x, int32_t y)
{
    OLED_InternalPlot((int16_t)(cx + x), (int16_t)(cy + y));
    if (y != 0) {
        OLED_InternalPlot((int16_t)(cx + x), (int16_t)(cy - y));
    }
    if (x != 0) {
        OLED_InternalPlot((int16_t)(cx - x), (int16_t)(cy + y));
        if (y != 0) {
            OLED_InternalPlot((int16_t)(cx - x), (int16_t)(cy - y));
        }
    }
    if (x != y) {
        OLED_InternalPlot((int16_t)(cx + y), (int16_t)(cy + x));
        OLED_InternalPlot((int16_t)(cx + y), (int16_t)(cy - x));
        if (y != 0) {
            OLED_InternalPlot((int16_t)(cx - y), (int16_t)(cy + x));
            OLED_InternalPlot((int16_t)(cx - y), (int16_t)(cy - x));
        }
    }
}

void OLED_DrawCircle(int16_t x, int16_t y, uint16_t radius)
{
    int32_t px = radius;   /* 当前八分区点的横向半径。 */
    int32_t py = 0;        /* 当前八分区点的纵向半径。 */
    int32_t error = 1 - px; /* 中点圆算法误差。 */

    if (!oled_curve_visible(x, y, radius, radius)) {
        return;
    }
    /* 只计算一个八分区，其余点通过对称关系得到。 */
    do {
        oled_plot_circle_points(x, y, px, py);
        ++py;
        if (error < 0) {
            error += 2 * py + 1;
        } else {
            --px;
            error += 2 * (py - px) + 1;
        }
    } while (px >= py);
}

void OLED_DrawDisc(int16_t x, int16_t y, uint16_t radius)
{
    /* FilledEllipse 每个扫描行只写一次，天然满足 XOR 唯一写入语义。 */
    OLED_DrawFilledEllipse(x, y, radius, radius);
}

/** 根据椭圆方程求指定纵向偏移处仍位于椭圆内的最大 x。 */
static int32_t oled_ellipse_x(uint16_t radius_x, uint16_t radius_y, int32_t dy)
{
    int32_t rx = radius_x; /* 水平半径。 */
    int32_t ry = radius_y; /* 垂直半径。 */
    int32_t rx2 = rx * rx; /* 水平半径平方。 */
    int32_t ry2 = ry * ry; /* 垂直半径平方。 */
    int32_t limit = rx2 * ry2; /* 椭圆隐式方程右侧。 */
    int32_t px = rx;       /* 从最外侧向内寻找边界。 */
    int32_t dy2 = dy * dy; /* 当前纵向偏移平方。 */

    while (px > 0 && px * px * ry2 + dy2 * rx2 > limit) {
        --px;
    }
    return px;
}

/** 绘制椭圆四向对称点，并避免坐标轴上的重复写入。 */
static void oled_plot_ellipse_points(int16_t cx, int16_t cy, int32_t x, int32_t y)
{
    OLED_InternalPlot((int16_t)(cx + x), (int16_t)(cy + y));
    if (x != 0) {
        OLED_InternalPlot((int16_t)(cx - x), (int16_t)(cy + y));
    }
    if (y != 0) {
        OLED_InternalPlot((int16_t)(cx + x), (int16_t)(cy - y));
        if (x != 0) {
            OLED_InternalPlot((int16_t)(cx - x), (int16_t)(cy - y));
        }
    }
}

void OLED_DrawEllipse(int16_t x, int16_t y, uint16_t radius_x, uint16_t radius_y)
{
    int32_t rx = radius_x; /* 水平半径。 */
    int32_t ry = radius_y; /* 垂直半径。 */
    int32_t rx2;           /* 水平半径平方。 */
    int32_t ry2;           /* 垂直半径平方。 */
    int32_t px = 0;        /* 当前第一象限横向偏移。 */
    int32_t py = ry;       /* 当前第一象限纵向偏移。 */
    int32_t dx = 0;        /* 椭圆方程横向增量。 */
    int32_t dy;            /* 椭圆方程纵向增量。 */
    int32_t decision;      /* 放大四倍后的整数判定量。 */
    int32_t last_x = -1;   /* 防止两个中点区域在交界处重复同一点。 */
    int32_t last_y = -1;

    if (!oled_curve_visible(x, y, radius_x, radius_y)) {
        return;
    }
    if (radius_x == 0U) {
        OLED_DrawVLine(x, (int16_t)(y - (int32_t)radius_y),
                       (uint16_t)(2U * radius_y + 1U));
        return;
    }
    if (radius_y == 0U) {
        OLED_DrawHLine((int16_t)(x - (int32_t)radius_x), y,
                       (uint16_t)(2U * radius_x + 1U));
        return;
    }

    rx2 = rx * rx;
    ry2 = ry * ry;
    dy = 2 * rx2 * py;
    decision = 4 * ry2 - 4 * rx2 * ry + rx2;

    /* 中点椭圆第一区域：切线较陡，逐列推进。 */
    while (dx < dy) {
        if (px != last_x || py != last_y) {
            oled_plot_ellipse_points(x, y, px, py);
            last_x = px;
            last_y = py;
        }
        ++px;
        dx += 2 * ry2;
        if (decision < 0) {
            decision += 4 * dx + 4 * ry2;
        } else {
            --py;
            dy -= 2 * rx2;
            decision += 4 * dx - 4 * dy + 4 * ry2;
        }
    }

    decision = ry2 * (2 * px + 1) * (2 * px + 1) +
               4 * rx2 * (py - 1) * (py - 1) - 4 * rx2 * ry2;
    /* 第二区域逐行推进，直到下方坐标轴。 */
    while (py >= 0) {
        if (px != last_x || py != last_y) {
            oled_plot_ellipse_points(x, y, px, py);
            last_x = px;
            last_y = py;
        }
        --py;
        dy -= 2 * rx2;
        if (decision > 0) {
            decision += 4 * rx2 - 4 * dy;
        } else {
            ++px;
            dx += 2 * ry2;
            decision += 4 * dx - 4 * dy + 4 * rx2;
        }
    }
}

void OLED_DrawFilledEllipse(int16_t x, int16_t y, uint16_t radius_x,
                            uint16_t radius_y)
{
    int32_t offset; /* 相对中心的纵向扫描偏移。 */

    if (!oled_curve_visible(x, y, radius_x, radius_y)) {
        return;
    }
    if (radius_x == 0U) {
        OLED_DrawVLine(x, (int16_t)(y - (int32_t)radius_y),
                       (uint16_t)(2U * radius_y + 1U));
        return;
    }
    if (radius_y == 0U) {
        OLED_DrawHLine((int16_t)(x - (int32_t)radius_x), y,
                       (uint16_t)(2U * radius_x + 1U));
        return;
    }
    /* 每个 y 偏移只需一对对称水平线即可填满椭圆。 */
    for (offset = 0; offset <= radius_y; ++offset) {
        int32_t px = oled_ellipse_x(radius_x, radius_y, offset); /* 半行宽度。 */
        uint16_t width = (uint16_t)(2 * px + 1); /* 包含中心像素的整行宽度。 */
        OLED_DrawHLine((int16_t)(x - px), (int16_t)(y + offset), width);
        if (offset != 0) {
            OLED_DrawHLine((int16_t)(x - px), (int16_t)(y - offset), width);
        }
    }
}

/** 使用起止方向向量的叉积判断圆周点是否位于顺时针圆弧内。 */
static bool oled_arc_contains(int32_t vx, int32_t vy,
                              int16_t start, int16_t end)
{
    int16_t normalized_start = oled_normalize_angle(start); /* 起始角。 */
    int16_t normalized_end = oled_normalize_angle(end);     /* 结束角。 */
    int16_t sweep = (int16_t)((normalized_end - normalized_start + 360) % 360); /* 扫过角度。 */
    int32_t sx; /* 起始方向向量 x。 */
    int32_t sy; /* 起始方向向量 y。 */
    int32_t ex; /* 结束方向向量 x。 */
    int32_t ey; /* 结束方向向量 y。 */
    int32_t from_start; /* 起始向量到候选点的叉积。 */
    int32_t to_end;     /* 候选点到结束向量的叉积。 */

    if (sweep == 0) {
        return true;
    }
    sx = OLED_InternalCosQ15(normalized_start);
    sy = OLED_InternalSinQ15(normalized_start);
    ex = OLED_InternalCosQ15(normalized_end);
    ey = OLED_InternalSinQ15(normalized_end);
    from_start = sx * vy - sy * vx;
    to_end = vx * ey - vy * ex;
    /* 小圆弧要求同时位于两条边界内；大圆弧只需满足其中一侧。 */
    return (sweep <= 180) ? (from_start >= 0 && to_end >= 0)
                          : (from_start >= 0 || to_end >= 0);
}

/** 仅当候选圆周点处于目标角度范围时绘制。 */
static void oled_plot_arc_point(int16_t cx, int16_t cy, int32_t x, int32_t y,
                                int16_t start, int16_t end)
{
    if (oled_arc_contains(x, y, start, end)) {
        OLED_InternalPlot((int16_t)(cx + x), (int16_t)(cy + y));
    }
}

/** 将圆弧候选点镜像到八个方向，并跳过重合的轴点和对角点。 */
static void oled_plot_arc_points(int16_t cx, int16_t cy, int32_t x, int32_t y,
                                 int16_t start, int16_t end)
{
    oled_plot_arc_point(cx, cy, x, y, start, end);
    if (y != 0) {
        oled_plot_arc_point(cx, cy, x, -y, start, end);
    }
    if (x != 0) {
        oled_plot_arc_point(cx, cy, -x, y, start, end);
        if (y != 0) {
            oled_plot_arc_point(cx, cy, -x, -y, start, end);
        }
    }
    if (x != y) {
        oled_plot_arc_point(cx, cy, y, x, start, end);
        oled_plot_arc_point(cx, cy, y, -x, start, end);
        if (y != 0) {
            oled_plot_arc_point(cx, cy, -y, x, start, end);
            oled_plot_arc_point(cx, cy, -y, -x, start, end);
        }
    }
}

void OLED_DrawArc(int16_t x, int16_t y, uint16_t radius,
                  int16_t start_angle, int16_t end_angle)
{
    int32_t px = radius;   /* 当前圆周横向距离。 */
    int32_t py = 0;        /* 当前圆周纵向距离。 */
    int32_t error = 1 - px; /* 中点圆算法误差。 */

    if (oled_normalize_angle(start_angle) == oled_normalize_angle(end_angle)) {
        OLED_DrawCircle(x, y, radius);
        return;
    }
    if (!oled_curve_visible(x, y, radius, radius)) {
        return;
    }
    /* 生成八个对称圆周点，再按顺时针角度范围筛选。 */
    do {
        oled_plot_arc_points(x, y, px, py, start_angle, end_angle);
        ++py;
        if (error < 0) {
            error += 2 * py + 1;
        } else {
            --px;
            error += 2 * (py - px) + 1;
        }
    } while (px >= py);
}

/** 计算圆角矩形某一行相对左右边界的缩进。 */
static int32_t oled_rounded_inset(uint16_t height, uint16_t radius,
                                  int32_t local_y)
{
    if (local_y < radius) {
        return (int32_t)radius -
               oled_ellipse_x(radius, radius, radius - local_y);
    }
    if (local_y >= (int32_t)height - radius) {
        int32_t dy = local_y - ((int32_t)height - 1 - radius);
        return (int32_t)radius - oled_ellipse_x(radius, radius, dy);
    }
    return 0;
}

/** 绘制左右两个互不重复的轮廓区间，重叠时合并为一个区间。 */
static void oled_draw_rounded_outline_row(int32_t left, int32_t previous_left,
                                          int32_t right, int32_t previous_right,
                                          int16_t y)
{
    int32_t left_begin;
    int32_t left_end;
    int32_t right_begin;
    int32_t right_end;

    if (left < previous_left) {
        left_begin = left;
        left_end = previous_left - 1;
    } else if (left > previous_left) {
        left_begin = previous_left + 1;
        left_end = left;
    } else {
        left_begin = left;
        left_end = left;
    }
    if (right < previous_right) {
        right_begin = right;
        right_end = previous_right - 1;
    } else if (right > previous_right) {
        right_begin = previous_right + 1;
        right_end = right;
    } else {
        right_begin = right;
        right_end = right;
    }
    if (left_end + 1 >= right_begin) {
        OLED_DrawHLine((int16_t)left_begin, y,
                       (uint16_t)(right_end - left_begin + 1));
    } else {
        OLED_DrawHLine((int16_t)left_begin, y,
                       (uint16_t)(left_end - left_begin + 1));
        OLED_DrawHLine((int16_t)right_begin, y,
                       (uint16_t)(right_end - right_begin + 1));
    }
}

void OLED_DrawRFrame(int16_t x, int16_t y, uint16_t width, uint16_t height,
                     uint16_t radius)
{
    uint16_t max_radius; /* 不让四个圆角相互交叉的最大半径。 */
    int32_t visible_x0 = x;                  /* 裁剪后的左边界。 */
    int32_t visible_y0 = y;                  /* 裁剪后的首行。 */
    int32_t visible_x1 = (int32_t)x + width; /* 裁剪后的右边界。 */
    int32_t visible_y1 = (int32_t)y + height; /* 裁剪后的末行。 */
    int32_t py;         /* 当前目标扫描行。 */

    if (width == 0U || height == 0U ||
        !oled_dimension_valid(width) || !oled_dimension_valid(height) ||
        !OLED_InternalIntersectClip(&visible_x0, &visible_y0,
                                    &visible_x1, &visible_y1)) {
        return;
    }
    max_radius = (width < height ? width : height);
    max_radius = (max_radius - 1U) / 2U;
    if (radius > max_radius) {
        radius = max_radius;
    }
    if (radius > oled_canvas_extent()) {
        return;
    }
    if (radius == 0U) {
        OLED_DrawFrame(x, y, width, height);
        return;
    }

    (void)visible_x0;
    (void)visible_x1;
    /* 每行直接生成圆角轮廓，避免极小矩形的四段圆弧互相重叠。 */
    for (py = visible_y0; py < visible_y1; ++py) {
        int32_t local_y = py - y;
        int32_t inset = oled_rounded_inset(height, radius, local_y);
        int32_t left;
        int32_t right;

        left = (int32_t)x + inset;
        right = (int32_t)x + width - 1 - inset;
        if (local_y == 0 || local_y == (int32_t)height - 1) {
            OLED_DrawHLine((int16_t)left, (int16_t)py,
                           (uint16_t)(right - left + 1));
        } else {
            int32_t previous_inset = oled_rounded_inset(height, radius,
                                                        local_y - 1);
            oled_draw_rounded_outline_row(left, (int32_t)x + previous_inset,
                                          right,
                                          (int32_t)x + width - 1 - previous_inset,
                                          (int16_t)py);
        }
    }
}

void OLED_DrawRBox(int16_t x, int16_t y, uint16_t width, uint16_t height,
                   uint16_t radius)
{
    uint16_t max_radius; /* 不让四个圆角相互交叉的最大半径。 */
    int32_t visible_x0 = x;                  /* 裁剪后的左边界。 */
    int32_t visible_y0 = y;                  /* 裁剪后的首行。 */
    int32_t visible_x1 = (int32_t)x + width; /* 裁剪后的右边界。 */
    int32_t visible_y1 = (int32_t)y + height; /* 裁剪后的末行。 */
    int32_t py;         /* 当前目标扫描行。 */

    if (width == 0U || height == 0U ||
        !oled_dimension_valid(width) || !oled_dimension_valid(height) ||
        !OLED_InternalIntersectClip(&visible_x0, &visible_y0,
                                    &visible_x1, &visible_y1)) {
        return;
    }
    max_radius = (width < height ? width : height);
    max_radius = (max_radius - 1U) / 2U;
    if (radius > max_radius) {
        radius = max_radius;
    }
    if (radius > oled_canvas_extent()) {
        return;
    }
    if (radius == 0U) {
        OLED_DrawBox(x, y, width, height);
        return;
    }

    (void)visible_x0;
    (void)visible_x1;
    /* 每个可见扫描行只写一个连续区间，避免组合填充在 XOR 下抵消。 */
    for (py = visible_y0; py < visible_y1; ++py) {
        int32_t local_y = py - y; /* 相对矩形顶部的行号。 */
        int32_t inset = 0;        /* 当前行左右两侧的圆角缩进。 */

        inset = oled_rounded_inset(height, radius, local_y);
        OLED_DrawHLine((int16_t)((int32_t)x + inset), (int16_t)py,
                       (uint16_t)((int32_t)width - 2 * inset));
    }
}

/** 绘制三个共线顶点覆盖的最外侧线段。 */
static void oled_draw_collinear_triangle(int16_t x0, int16_t y0,
                                         int16_t x1, int16_t y1,
                                         int16_t x2, int16_t y2)
{
    int16_t min_x = x0;
    int16_t min_y = y0;
    int16_t max_x = x0;
    int16_t max_y = y0;

    if (x1 < min_x || (x1 == min_x && y1 < min_y)) {
        min_x = x1;
        min_y = y1;
    }
    if (x2 < min_x || (x2 == min_x && y2 < min_y)) {
        min_x = x2;
        min_y = y2;
    }
    if (x1 > max_x || (x1 == max_x && y1 > max_y)) {
        max_x = x1;
        max_y = y1;
    }
    if (x2 > max_x || (x2 == max_x && y2 > max_y)) {
        max_x = x2;
        max_y = y2;
    }
    OLED_DrawLine(min_x, min_y, max_x, max_y);
}

/** 三角形的一条有向边；三条边均按包含起点、不包含终点解释。 */
typedef struct {
    int16_t x0;
    int16_t y0;
    int16_t x1;
    int16_t y1;
} OLED_TriangleEdge;

/** 判断像素是否属于一条半开 Bresenham 边。 */
static bool oled_triangle_edge_contains(OLED_TriangleEdge edge,
                                        int16_t x, int16_t y)
{
    int32_t dx = oled_abs32((int32_t)edge.x1 - edge.x0);
    int32_t dy = oled_abs32((int32_t)edge.y1 - edge.y0);
    int32_t sx = (edge.x0 < edge.x1) ? 1 : -1;
    int32_t sy = (edge.y0 < edge.y1) ? 1 : -1;

    if (x == edge.x1 && y == edge.y1) {
        return false;
    }
    if (dx >= dy) {
        int32_t step = ((int32_t)x - edge.x0) * sx;
        uint32_t offset;

        if (step < 0 || step > dx || x != (int32_t)edge.x0 + sx * step) {
            return false;
        }
        offset = ((uint32_t)step * (uint32_t)dy + (uint32_t)dx / 2U) /
                 (uint32_t)dx;
        return y == (int32_t)edge.y0 + sy * (int32_t)offset;
    }
    {
        int32_t step = ((int32_t)y - edge.y0) * sy;
        uint32_t offset;

        if (step < 0 || step > dy || y != (int32_t)edge.y0 + sy * step) {
            return false;
        }
        offset = ((uint32_t)step * (uint32_t)dx + (uint32_t)dy / 2U) /
                 (uint32_t)dy;
        return x == (int32_t)edge.x0 + sx * (int32_t)offset;
    }
}

/** 绘制半开边，并跳过已经由前序边拥有的离散像素。 */
static void oled_draw_unique_triangle_edge(OLED_TriangleEdge edge,
                                           const OLED_TriangleEdge *previous,
                                           uint8_t previous_count)
{
    int32_t x = edge.x0;
    int32_t y = edge.y0;
    int32_t dx = oled_abs32((int32_t)edge.x1 - edge.x0);
    int32_t sx = (edge.x0 < edge.x1) ? 1 : -1;
    int32_t dy = -oled_abs32((int32_t)edge.y1 - edge.y0);
    int32_t sy = (edge.y0 < edge.y1) ? 1 : -1;
    int32_t error = dx + dy;

    while (x != edge.x1 || y != edge.y1) {
        uint8_t index;
        bool owned = false;

        for (index = 0U; index < previous_count; ++index) {
            if (oled_triangle_edge_contains(previous[index], (int16_t)x, (int16_t)y)) {
                owned = true;
                break;
            }
        }
        if (!owned) {
            OLED_InternalPlot((int16_t)x, (int16_t)y);
        }
        {
            int32_t doubled = error * 2;
            if (doubled >= dy) {
                error += dy;
                x += sx;
            }
            if (doubled <= dx) {
                error += dx;
                y += sy;
            }
        }
    }
}

void OLED_DrawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                       int16_t x2, int16_t y2)
{
    int32_t min_x = x0;
    int32_t min_y = y0;
    int32_t max_x = x0;
    int32_t max_y = y0;
    int64_t area;
    OLED_TriangleEdge edges[3];

    if (x1 < min_x) min_x = x1;
    if (x2 < min_x) min_x = x2;
    if (y1 < min_y) min_y = y1;
    if (y2 < min_y) min_y = y2;
    if (x1 > max_x) max_x = x1;
    if (x2 > max_x) max_x = x2;
    if (y1 > max_y) max_y = y1;
    if (y2 > max_y) max_y = y2;
    if (!oled_bounds_visible(min_x, min_y, max_x + 1, max_y + 1)) {
        return;
    }

    area = (int64_t)((int32_t)x1 - x0) * ((int32_t)y2 - y0) -
           (int64_t)((int32_t)y1 - y0) * ((int32_t)x2 - x0);
    if (area == 0) {
        oled_draw_collinear_triangle(x0, y0, x1, y1, x2, y2);
        return;
    }
    edges[0] = (OLED_TriangleEdge){x0, y0, x1, y1};
    edges[1] = (OLED_TriangleEdge){x1, y1, x2, y2};
    edges[2] = (OLED_TriangleEdge){x2, y2, x0, y0};
    /* 后续半开边跳过前序边已拥有的栅格点，顶点和细边重合处都只写一次。 */
    oled_draw_unique_triangle_edge(edges[0], edges, 0U);
    oled_draw_unique_triangle_edge(edges[1], edges, 1U);
    oled_draw_unique_triangle_edge(edges[2], edges, 2U);
}

/** 在一条边上按给定 y 线性插值得到对应 x。 */
static int32_t oled_interpolate_x(int16_t x0, int16_t y0,
                                  int16_t x1, int16_t y1, int32_t y)
{
    int32_t dy = (int32_t)y1 - y0; /* 边的纵向跨度。 */
    int32_t dx = (int32_t)x1 - x0; /* 边的横向跨度。 */
    uint32_t magnitude;            /* 无符号乘积可覆盖 65535×65535。 */
    int32_t delta;

    if (dy == 0) {
        return x0;
    }
    magnitude = (uint32_t)(y - y0) *
                (uint32_t)((dx < 0) ? -dx : dx);
    delta = (int32_t)(magnitude / (uint32_t)dy);
    return (dx < 0) ? (int32_t)x0 - delta : (int32_t)x0 + delta;
}

void OLED_DrawFilledTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                             int16_t x2, int16_t y2)
{
    int16_t clip_x0; /* 水平线内部继续处理的左边界。 */
    int16_t clip_y0; /* 扫描起始行下限。 */
    int16_t clip_x1; /* 水平线内部继续处理的右边界。 */
    int16_t clip_y1; /* 扫描结束行上限。 */
    int32_t begin_y; /* 裁剪后的首个扫描行。 */
    int32_t end_y;   /* 裁剪后的最后扫描行，包含。 */
    int32_t y;       /* 当前扫描行。 */
    int32_t min_x = x0;
    int32_t min_y = y0;
    int32_t max_x = x0;
    int32_t max_y = y0;
    int64_t area;

    if (x1 < min_x) min_x = x1;
    if (x2 < min_x) min_x = x2;
    if (y1 < min_y) min_y = y1;
    if (y2 < min_y) min_y = y2;
    if (x1 > max_x) max_x = x1;
    if (x2 > max_x) max_x = x2;
    if (y1 > max_y) max_y = y1;
    if (y2 > max_y) max_y = y2;
    if (!oled_bounds_visible(min_x, min_y, max_x + 1, max_y + 1)) {
        return;
    }
    area = (int64_t)((int32_t)x1 - x0) * ((int32_t)y2 - y0) -
           (int64_t)((int32_t)y1 - y0) * ((int32_t)x2 - x0);
    if (area == 0) {
        oled_draw_collinear_triangle(x0, y0, x1, y1, x2, y2);
        return;
    }

    /* 先按 y 从小到大排序，y1 成为上下两段短边的分界点。 */
    if (y0 > y1) {
        oled_swap_i16(&x0, &x1);
        oled_swap_i16(&y0, &y1);
    }
    if (y1 > y2) {
        oled_swap_i16(&x1, &x2);
        oled_swap_i16(&y1, &y2);
    }
    if (y0 > y1) {
        oled_swap_i16(&x0, &x1);
        oled_swap_i16(&y0, &y1);
    }

    OLED_InternalGetClip(&clip_x0, &clip_y0, &clip_x1, &clip_y1);
    (void)clip_x0;
    (void)clip_x1;
    begin_y = (y0 < clip_y0) ? clip_y0 : y0;
    end_y = (y2 >= clip_y1) ? (int32_t)clip_y1 - 1 : y2;
    for (y = begin_y; y <= end_y; ++y) {
        int32_t xa = oled_interpolate_x(x0, y0, x2, y2, y); /* 长边交点。 */
        int32_t xb;    /* 当前短边交点。 */
        int32_t left;  /* 当前扫描线左端。 */
        int32_t right; /* 当前扫描线右端。 */

        /* 上半段使用边 0→1，下半段使用边 1→2。 */
        if (y <= y1 && y1 != y0) {
            xb = oled_interpolate_x(x0, y0, x1, y1, y);
        } else {
            xb = oled_interpolate_x(x1, y1, x2, y2, y);
        }
        left = (xa < xb) ? xa : xb;
        right = (xa < xb) ? xb : xa;
        /* 先收缩到裁剪窗口，避免横跨整个 int16_t 坐标域时宽度回绕。 */
        if (right >= clip_x0 && left < clip_x1) {
            if (left < clip_x0) {
                left = clip_x0;
            }
            if (right >= clip_x1) {
                right = (int32_t)clip_x1 - 1;
            }
            OLED_DrawHLine((int16_t)left, (int16_t)y,
                           (uint16_t)(right - left + 1));
        }
    }
}
