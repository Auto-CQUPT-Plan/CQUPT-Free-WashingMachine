#include "kk_oled_internal.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* 本文件独立解析 u8g2 字体数据，不依赖或链接 u8g2 程序代码。 */

/** u8g2 字体固定头部长度，头部之后才是字形记录。 */
#define OLED_FONT_HEADER_SIZE 23U

/** 从 23 字节字体头解析出的全局压缩参数和索引偏移。 */
typedef struct {
    uint8_t bits_per_0;       /**< RLE 中连续背景像素数量的位宽。 */
    uint8_t bits_per_1;       /**< RLE 中连续前景像素数量的位宽。 */
    uint8_t bits_width;       /**< 字形像素宽度字段的位宽。 */
    uint8_t bits_height;      /**< 字形像素高度字段的位宽。 */
    uint8_t bits_x;           /**< 字形水平偏移字段的位宽。 */
    uint8_t bits_y;           /**< 字形垂直偏移字段的位宽。 */
    uint8_t bits_advance;     /**< 字符 advance 字段的位宽。 */
    uint8_t max_width;        /**< 字体内最大字形宽度。 */
    uint8_t max_height;       /**< 字体内最大字形高度。 */
    int8_t x_offset;          /**< 全字体包围盒水平偏移。 */
    int8_t y_offset;          /**< 全字体包围盒垂直偏移。 */
    int8_t ascent_text;       /**< 常规文字参考上升量。 */
    int8_t descent_text;      /**< 常规文字参考下降量。 */
    int8_t ascent_extended;   /**< 扩展文字参考上升量。 */
    int8_t descent_extended;  /**< 扩展文字参考下降量。 */
    uint16_t start_upper;     /**< 大写 ASCII 索引相对字形区的偏移。 */
    uint16_t start_lower;     /**< 小写 ASCII 索引相对字形区的偏移。 */
    uint16_t start_unicode;   /**< Unicode 跳转表相对字形区的偏移。 */
} OLED_FontInfo;

/** LSB-first 位流读取位置，可自然跨越字节边界。 */
typedef struct {
    const uint8_t *pointer; /**< 当前读取字节。 */
    uint8_t bit;            /**< 当前字节内下一位的位置，范围 0～7。 */
} OLED_BitReader;

/** 单个字形记录解码出的度量与 RLE 起点。 */
typedef struct {
    uint8_t width;          /**< 字形位图宽度。 */
    uint8_t height;         /**< 字形位图高度。 */
    int8_t x_offset;        /**< 字形相对书写点的水平偏移。 */
    int8_t y_offset;        /**< 字形相对基线的垂直偏移。 */
    int8_t advance;         /**< 绘制后书写点的前进量。 */
    OLED_BitReader bitmap;  /**< 度量字段之后的 RLE 位流位置。 */
} OLED_GlyphInfo;

static const uint8_t *oled_font; /**< 当前字体数组首地址，NULL 表示禁用文字。 */
static OLED_FontInfo oled_font_info; /**< 当前字体头的解析结果。 */
static OLED_Rotation oled_font_direction = OLED_ROTATION_0; /**< 字符串前进方向。 */
static OLED_FontPosition oled_font_position = OLED_FONT_POS_BASELINE; /**< 坐标参考方式。 */
static OLED_FontRefHeight oled_font_ref_height = OLED_FONT_REF_TEXT; /**< 参考高度范围。 */
static int16_t oled_font_ascent;  /**< 当前参考高度模式的上升量。 */
static int16_t oled_font_descent; /**< 当前参考高度模式的下降量。 */

/** 读取字体索引使用的 16 位大端整数。 */
static uint16_t oled_read_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

/** 从 LSB-first 位流读取 count 位无符号值，并推进读取位置。 */
static uint8_t oled_read_bits(OLED_BitReader *reader, uint8_t count)
{
    uint16_t value; /* 拼接当前字节和下一字节后的临时值。 */
    uint8_t total;  /* 读取后相对当前字节起点的总位数。 */

    if (count == 0U) {
        return 0U;
    }
    value = (uint16_t)(reader->pointer[0] >> reader->bit);
    total = (uint8_t)(reader->bit + count);
    /* 字段跨字节时，从下一字节低位补齐高位部分。 */
    if (total > 8U) {
        value |= (uint16_t)reader->pointer[1] << (8U - reader->bit);
    }
    reader->pointer += total >> 3U;
    reader->bit = total & 7U;
    if (count == 8U) {
        return (uint8_t)value;
    }
    return (uint8_t)(value & ((1U << count) - 1U));
}

/** 按 u8g2 的偏置编码读取有符号字段。 */
static int8_t oled_read_signed_bits(OLED_BitReader *reader, uint8_t count)
{
    int16_t value; /* 先读取无符号值，再减去符号偏置。 */

    if (count == 0U) {
        return 0;
    }
    value = oled_read_bits(reader, count);
    value -= (int16_t)(1U << (count - 1U));
    return (int8_t)value;
}

/** 根据 TEXT、EXTENDED 或 ALL 模式重新计算参考上升量和下降量。 */
static void oled_update_ref_height(void)
{
    int16_t ascent;  /* 本次计算得到的上升量。 */
    int16_t descent; /* 本次计算得到的下降量。 */

    if (oled_font == NULL) {
        oled_font_ascent = 0;
        oled_font_descent = 0;
        return;
    }
    ascent = oled_font_info.ascent_text;
    descent = oled_font_info.descent_text;
    if (oled_font_ref_height == OLED_FONT_REF_EXTENDED) {
        /* 扩展模式在常规 A/g 高度基础上纳入扩展字符边界。 */
        if (ascent < oled_font_info.ascent_extended) {
            ascent = oled_font_info.ascent_extended;
        }
        if (descent > oled_font_info.descent_extended) {
            descent = oled_font_info.descent_extended;
        }
    } else if (oled_font_ref_height == OLED_FONT_REF_ALL) {
        int16_t all_ascent = (int16_t)oled_font_info.max_height +
                             oled_font_info.y_offset; /* 全字形包围盒顶部。 */
        if (ascent < all_ascent) {
            ascent = all_ascent;
        }
        if (descent > oled_font_info.y_offset) {
            descent = oled_font_info.y_offset;
        }
    }
    oled_font_ascent = ascent;
    oled_font_descent = descent;
}

void OLED_SetFont(const uint8_t *font)
{
    oled_font = font;
    if (font == NULL) {
        memset(&oled_font_info, 0, sizeof(oled_font_info));
        oled_update_ref_height();
        return;
    }

    /* 按固定偏移解析 23 字节头；跳转偏移使用大端存储。 */
    oled_font_info.bits_per_0 = font[2];
    oled_font_info.bits_per_1 = font[3];
    oled_font_info.bits_width = font[4];
    oled_font_info.bits_height = font[5];
    oled_font_info.bits_x = font[6];
    oled_font_info.bits_y = font[7];
    oled_font_info.bits_advance = font[8];
    oled_font_info.max_width = font[9];
    oled_font_info.max_height = font[10];
    oled_font_info.x_offset = (int8_t)font[11];
    oled_font_info.y_offset = (int8_t)font[12];
    oled_font_info.ascent_text = (int8_t)font[13];
    oled_font_info.descent_text = (int8_t)font[14];
    oled_font_info.ascent_extended = (int8_t)font[15];
    oled_font_info.descent_extended = (int8_t)font[16];
    oled_font_info.start_upper = oled_read_be16(font + 17);
    oled_font_info.start_lower = oled_read_be16(font + 19);
    oled_font_info.start_unicode = oled_read_be16(font + 21);
    oled_update_ref_height();
}

void OLED_SetFontDirection(OLED_Rotation direction)
{
    if (direction <= OLED_ROTATION_270) {
        oled_font_direction = direction;
    }
}

void OLED_SetFontPosition(OLED_FontPosition position)
{
    if (position <= OLED_FONT_POS_CENTER) {
        oled_font_position = position;
    }
}

void OLED_SetFontRefHeight(OLED_FontRefHeight ref_height)
{
    if (ref_height <= OLED_FONT_REF_ALL) {
        oled_font_ref_height = ref_height;
        oled_update_ref_height();
    }
}

/** 按 ASCII 跳转点或 Unicode 跳转表查找字形压缩数据。 */
static const uint8_t *oled_find_glyph(uint16_t codepoint)
{
    const uint8_t *cursor; /* 当前索引项或字形记录位置。 */

    if (oled_font == NULL) {
        return NULL;
    }
    cursor = oled_font + OLED_FONT_HEADER_SIZE;
    if (codepoint <= 0x00FFU) {
        /* ASCII 利用 A/a 两个跳转点减少顺序扫描长度。 */
        if (codepoint >= (uint16_t)'a') {
            cursor += oled_font_info.start_lower;
        } else if (codepoint >= (uint16_t)'A') {
            cursor += oled_font_info.start_upper;
        }
        while (cursor[1] != 0U) {
            if (cursor[0] == (uint8_t)codepoint) {
                return cursor + 2;
            }
            cursor += cursor[1];
        }
        return NULL;
    }

    /* Unicode 先通过四字节跳转表定位区段，再在区段内顺序查找。 */
    cursor += oled_font_info.start_unicode;
    {
        const uint8_t *lookup = cursor; /* 当前四字节跳转表项。 */
        uint16_t indexed_codepoint;     /* 该表项覆盖区段的起始编码。 */

        do {
            cursor += oled_read_be16(lookup);
            indexed_codepoint = oled_read_be16(lookup + 2);
            lookup += 4;
        } while (indexed_codepoint < codepoint);
    }

    for (;;) {
        uint16_t encoded = oled_read_be16(cursor); /* 当前 Unicode 字形编码。 */
        if (encoded == 0U) {
            return NULL;
        }
        if (encoded == codepoint) {
            return cursor + 3;
        }
        cursor += cursor[2];
    }
}

/** 从字形压缩数据开头读取宽高、偏移和 advance。 */
static bool oled_decode_glyph_info(const uint8_t *data, OLED_GlyphInfo *glyph)
{
    OLED_BitReader reader; /* 度量字段共用的位流读取器。 */

    if (data == NULL) {
        return false;
    }
    reader.pointer = data;
    reader.bit = 0U;
    glyph->width = oled_read_bits(&reader, oled_font_info.bits_width);
    glyph->height = oled_read_bits(&reader, oled_font_info.bits_height);
    glyph->x_offset = oled_read_signed_bits(&reader, oled_font_info.bits_x);
    glyph->y_offset = oled_read_signed_bits(&reader, oled_font_info.bits_y);
    glyph->advance = oled_read_signed_bits(&reader, oled_font_info.bits_advance);
    /* 保存度量字段之后的位置，后续从这里开始解压像素。 */
    glyph->bitmap = reader;
    return true;
}

/** 查找请求字形；缺字时只返回空格度量，并标记为不绘制。 */
static bool oled_get_glyph_or_space(uint16_t codepoint, OLED_GlyphInfo *glyph,
                                    bool *is_requested_glyph)
{
    const uint8_t *data = oled_find_glyph(codepoint); /* 请求字形的压缩数据。 */

    if (data != NULL) {
        *is_requested_glyph = true;
        return oled_decode_glyph_info(data, glyph);
    }
    *is_requested_glyph = false;
    if (codepoint == 0x0020U) {
        return false;
    }
    return oled_decode_glyph_info(oled_find_glyph(0x0020U), glyph);
}

/** 把当前文字位置语义换算成相对基线的垂直修正量。 */
static int16_t oled_font_vref(void)
{
    switch (oled_font_position) {
    case OLED_FONT_POS_TOP:
        return (int16_t)(oled_font_ascent + 1);
    case OLED_FONT_POS_BOTTOM:
        return oled_font_descent;
    case OLED_FONT_POS_CENTER:
        return (int16_t)(oled_font_descent +
                         (oled_font_ascent - oled_font_descent) / 2);
    case OLED_FONT_POS_BASELINE:
    default:
        return 0;
    }
}

/** 按文字方向将字形局部坐标旋转到逻辑画布并写入一个源像素。 */
static void oled_plot_font_pixel(int16_t origin_x, int16_t origin_y,
                                 int32_t local_x, int32_t local_y,
                                 bool source_pixel)
{
    int32_t target_x; /* 旋转后的逻辑画布横坐标。 */
    int32_t target_y; /* 旋转后的逻辑画布纵坐标。 */

    /* 文字只围绕书写点旋转，不改变整个画布方向。 */
    switch (oled_font_direction) {
    case OLED_ROTATION_90:
        target_x = (int32_t)origin_x - local_y;
        target_y = (int32_t)origin_y + local_x;
        break;
    case OLED_ROTATION_180:
        target_x = (int32_t)origin_x - local_x;
        target_y = (int32_t)origin_y - local_y;
        break;
    case OLED_ROTATION_270:
        target_x = (int32_t)origin_x + local_y;
        target_y = (int32_t)origin_y - local_x;
        break;
    case OLED_ROTATION_0:
    default:
        target_x = (int32_t)origin_x + local_x;
        target_y = (int32_t)origin_y + local_y;
        break;
    }
    if (target_x >= INT16_MIN && target_x <= INT16_MAX &&
        target_y >= INT16_MIN && target_y <= INT16_MAX) {
        OLED_InternalPlotSource((int16_t)target_x, (int16_t)target_y, source_pixel);
    }
}

/** 将一个 RLE 游程按行优先顺序展开到字形局部坐标。 */
static void oled_draw_run(int16_t origin_x, int16_t origin_y,
                          const OLED_GlyphInfo *glyph, uint32_t *position,
                          uint8_t length, bool source_pixel)
{
    uint32_t total = (uint32_t)glyph->width * glyph->height; /* 字形总像素数。 */
    uint32_t remaining = length; /* 当前游程尚未处理的像素数。 */

    while (remaining > 0U && *position < total) {
        uint32_t local_x = *position % glyph->width; /* 行内像素位置。 */
        uint32_t local_y = *position / glyph->width; /* 字形内行号。 */
        int32_t glyph_x = (int32_t)glyph->x_offset + local_x; /* 相对书写点 x。 */
        int32_t glyph_y = (int32_t)oled_font_vref() - glyph->height -
                          glyph->y_offset + local_y; /* 相对输入 y 的目标位置。 */
        oled_plot_font_pixel(origin_x, origin_y, glyph_x, glyph_y, source_pixel);
        ++(*position);
        --remaining;
    }
}

/** 解压一个字形的 0/1 游程，并按当前背景模式绘制。 */
static void oled_render_glyph(int16_t x, int16_t y, OLED_GlyphInfo *glyph)
{
    OLED_BitReader reader = glyph->bitmap; /* RLE 数据读取位置。 */
    uint32_t position = 0U;               /* 已展开的行优先像素数。 */
    uint32_t total = (uint32_t)glyph->width * glyph->height; /* 结束条件。 */

    if (glyph->width == 0U || glyph->height == 0U) {
        return;
    }
    while (position < total) {
        uint8_t zero_count = oled_read_bits(&reader, oled_font_info.bits_per_0); /* 背景游程。 */
        uint8_t one_count = oled_read_bits(&reader, oled_font_info.bits_per_1);  /* 前景游程。 */
        uint8_t repeat; /* 为 1 时重复使用当前一对游程长度。 */

        do {
            oled_draw_run(x, y, glyph, &position, zero_count, false);
            oled_draw_run(x, y, glyph, &position, one_count, true);
            repeat = oled_read_bits(&reader, 1U);
        } while (repeat != 0U && position < total);
    }
}

int16_t OLED_DrawGlyph(int16_t x, int16_t y, uint16_t codepoint)
{
    OLED_GlyphInfo glyph; /* 请求字形或回退空格的度量。 */
    bool found;           /* 是否真正找到请求字形。 */

    if (oled_font == NULL || !oled_get_glyph_or_space(codepoint, &glyph, &found)) {
        return 0;
    }
    /* 缺字只返回空格 advance，不绘制空格的像素。 */
    if (found) {
        oled_render_glyph(x, y, &glyph);
    }
    return glyph.advance;
}

int16_t OLED_GetGlyphAdvance(uint16_t codepoint)
{
    OLED_GlyphInfo glyph; /* 请求字形或回退空格的度量。 */
    bool found;           /* 此接口只读度量，不需要区分是否回退。 */

    if (oled_font == NULL || !oled_get_glyph_or_space(codepoint, &glyph, &found)) {
        return 0;
    }
    (void)found;
    return glyph.advance;
}

/** 解码一个 UTF-8 字符；非法字节和非 BMP 字符按约定返回空格。 */
static uint16_t oled_utf8_next(const char *text, size_t *consumed)
{
    const uint8_t *bytes = (const uint8_t *)text; /* 当前 UTF-8 序列。 */
    uint8_t first = bytes[0];                     /* 序列首字节。 */

    *consumed = 1U;
    if (first < 0x80U) {
        return first;
    }
    if (first >= 0xC2U && first <= 0xDFU &&
        bytes[1] >= 0x80U && bytes[1] <= 0xBFU) {
        *consumed = 2U;
        return (uint16_t)(((uint16_t)(first & 0x1FU) << 6U) | (bytes[1] & 0x3FU));
    }
    if (first >= 0xE0U && first <= 0xEFU &&
        bytes[1] >= 0x80U && bytes[1] <= 0xBFU &&
        bytes[2] >= 0x80U && bytes[2] <= 0xBFU &&
        !(first == 0xE0U && bytes[1] < 0xA0U) &&
        !(first == 0xEDU && bytes[1] >= 0xA0U)) {
        *consumed = 3U;
        return (uint16_t)(((uint16_t)(first & 0x0FU) << 12U) |
                          ((uint16_t)(bytes[1] & 0x3FU) << 6U) |
                          (bytes[2] & 0x3FU));
    }
    if (first >= 0xF0U && first <= 0xF4U &&
        bytes[1] >= 0x80U && bytes[1] <= 0xBFU &&
        bytes[2] >= 0x80U && bytes[2] <= 0xBFU &&
        bytes[3] >= 0x80U && bytes[3] <= 0xBFU &&
        !(first == 0xF0U && bytes[1] < 0x90U) &&
        !(first == 0xF4U && bytes[1] >= 0x90U)) {
        /* 合法非 BMP 字符整体消费，但当前 uint16_t 接口只能回退为空格。 */
        *consumed = 4U;
        return 0x0020U;
    }
    /* 畸形序列只消费当前无效起始字节，下次继续检查后续字节。 */
    return 0x0020U;
}

/** 沿当前文字方向移动书写点。 */
static void oled_advance_pen(int16_t *x, int16_t *y, int16_t advance)
{
    switch (oled_font_direction) {
    case OLED_ROTATION_90:
        *y = (int16_t)((int32_t)*y + advance);
        break;
    case OLED_ROTATION_180:
        *x = (int16_t)((int32_t)*x - advance);
        break;
    case OLED_ROTATION_270:
        *y = (int16_t)((int32_t)*y - advance);
        break;
    case OLED_ROTATION_0:
    default:
        *x = (int16_t)((int32_t)*x + advance);
        break;
    }
}

/** 将累计 advance 限制到公共接口的 int16_t 返回范围。 */
static int16_t oled_clamp_i16(int32_t value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)value;
}

int16_t OLED_DrawUTF8(int16_t x, int16_t y, const char *utf8)
{
    int32_t total = 0;          /* 全部字符 advance 的宽范围累计值。 */
    const char *cursor = utf8;  /* 当前待解码 UTF-8 字节。 */

    if (oled_font == NULL || utf8 == NULL) {
        return 0;
    }
    while (*cursor != '\0') {
        size_t consumed; /* 当前字符消耗的 UTF-8 字节数。 */
        uint16_t codepoint = oled_utf8_next(cursor, &consumed); /* BMP 码点或空格。 */
        int16_t advance = OLED_DrawGlyph(x, y, codepoint);      /* 当前字符前进量。 */
        total += advance;
        oled_advance_pen(&x, &y, advance);
        cursor += consumed;
    }
    return oled_clamp_i16(total);
}

uint16_t OLED_GetUTF8Width(const char *utf8)
{
    const char *cursor = utf8;  /* 当前待解码 UTF-8 字节。 */
    int32_t width = 0;          /* 先按 advance 累加的总宽度。 */
    int16_t last_advance = 0;   /* 最后字符已计入的 advance。 */
    int16_t last_visible_width = 0; /* 最后字符的 width + x_offset。 */
    bool has_character = false; /* 用于区分空字符串和零宽字符。 */

    if (oled_font == NULL || utf8 == NULL) {
        return 0U;
    }
    while (*cursor != '\0') {
        OLED_GlyphInfo glyph; /* 当前字形或回退空格度量。 */
        bool found;           /* 宽度计算不绘制，因此只用于完成统一查找。 */
        size_t consumed;      /* 当前字符消耗的字节数。 */
        uint16_t codepoint = oled_utf8_next(cursor, &consumed); /* 当前 BMP 码点。 */

        if (oled_get_glyph_or_space(codepoint, &glyph, &found)) {
            (void)found;
            last_advance = glyph.advance;
            last_visible_width = (int16_t)glyph.width + glyph.x_offset;
            width += glyph.advance;
        } else {
            last_advance = 0;
            last_visible_width = 0;
        }
        has_character = true;
        cursor += consumed;
    }
    /* 尾字形不保留右侧 advance 空白，改用其真实可见右边界。 */
    if (has_character) {
        width -= last_advance;
        width += last_visible_width;
    }
    if (width <= 0) {
        return 0U;
    }
    if (width > UINT16_MAX) {
        return UINT16_MAX;
    }
    return (uint16_t)width;
}

int16_t OLED_GetFontAscent(void)
{
    return oled_font_ascent;
}

int16_t OLED_GetFontDescent(void)
{
    return oled_font_descent;
}
