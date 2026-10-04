#include "kk_oled_driver.h"

#include <Arduino.h>
#include <Wire.h>

/* kk_oled_internal.h 是纯 C 接口，以 C 链接引入。 */
extern "C" {
#include "kk_oled_internal.h"
}

/*
 * 硬件适配边界：0.96 寸 SSD1306 I2C 模组 + ESP8266 Arduino Wire。
 * I2C 引脚使用核心默认的 D1(GPIO5)=SCL、D2(GPIO4)=SDA，见 README 接线表。
 */

/** 7 位 I2C 设备地址；常见 0.96 寸模组为 0x3C，部分模组丝印可跳 0x3D。 */
#define OLED_I2C_ADDRESS 0x3CU
/** I2C 控制字节：后续内容为 SSD1306 命令。 */
#define OLED_CONTROL_COMMAND 0x00U
/** I2C 控制字节：后续内容为显示数据。 */
#define OLED_CONTROL_DATA 0x40U
/** 模组可见第 0 列相对 SSD1306 内部显存的列偏移。 */
#define OLED_COLUMN_OFFSET 0U
/** I2C 时钟频率。 */
#define OLED_I2C_CLOCK_HZ 400000U
/** 单次 I2C 传输携带的最大数据字节数（Wire 缓冲还需容纳 1 字节控制字节）。 */
#define OLED_I2C_CHUNK 64U

static bool oled_driver_busy; /**< 阻塞传输期间为 true。 */

/** 通过控制字节阻塞发送一段命令或数据，超过单片缓冲时自动分段。 */
static OLED_Status oled_send_blocking(uint8_t control, const uint8_t *data,
                                      uint16_t length)
{
    uint16_t offset = 0U;

    while (offset < length) {
        uint16_t chunk = (uint16_t)(length - offset);
        if (chunk > OLED_I2C_CHUNK) {
            chunk = OLED_I2C_CHUNK;
        }
        Wire.beginTransmission((uint8_t)OLED_I2C_ADDRESS);
        Wire.write(control);
        Wire.write(data + offset, chunk);
        if (Wire.endTransmission() != 0U) {
            return OLED_ERROR;
        }
        offset = (uint16_t)(offset + chunk);
    }
    return OLED_OK;
}

/** 阻塞发送一组 SSD1306 命令。 */
static OLED_Status oled_send_command_blocking(const uint8_t *command,
                                              uint16_t length)
{
    return oled_send_blocking(OLED_CONTROL_COMMAND, command, length);
}

/** 阻塞发送连续显存数据。 */
static OLED_Status oled_send_data_blocking(const uint8_t *data, uint16_t length)
{
    return oled_send_blocking(OLED_CONTROL_DATA, data, length);
}

/** 生成页寻址三字节命令：页号、列低四位、列高四位。 */
static void oled_prepare_page_command(uint8_t page, uint8_t column,
                                      uint8_t *command)
{
    uint8_t visible_column = (uint8_t)(column + OLED_COLUMN_OFFSET);

    command[0] = (uint8_t)(0xB0U | page);
    command[1] = (uint8_t)(visible_column & 0x0FU);
    command[2] = (uint8_t)(0x10U | (visible_column >> 4U));
}

/**
 * 等待屏幕上电稳定，发送 SSD1306 数据手册标准初始化序列（页寻址模式），
 * 逐页清零后再点亮。初始化阶段全部使用阻塞调用，保证返回时屏幕状态确定。
 */
OLED_Status OLED_DriverInit(void)
{
    static const uint8_t init_commands[] = {
        0xAEU,             /* 显示关闭 */
        0xD5U, 0x80U,      /* 时钟分频/振荡频率 */
        0xA8U, 0x3FU,      /* 复用率 64 */
        0xD3U, 0x00U,      /* 显示偏移 0 */
        0x40U,             /* 起始行 0 */
        0x8DU, 0x14U,      /* 内部电荷泵开启 */
        0x20U, 0x02U,      /* 页寻址模式（本驱动的逐页写法依赖它） */
        0xA1U,             /* 段重映射：列 127 映射到 SEG0 */
        0xC8U,             /* 行扫描方向：COM63 到 COM0 */
        0xDAU, 0x12U,      /* COM 引脚配置（128x64） */
        0x81U, 0xCFU,      /* 对比度 */
        0xD9U, 0xF1U,      /* 预充电周期 */
        0xDBU, 0x40U,      /* VCOMH 电压 */
        0xA4U,             /* 显示内容跟随 RAM */
        0xA6U              /* 正常显示（非反色） */
    };
    static const uint8_t zeros[OLED_PHYSICAL_WIDTH] = {0}; /**< 初始化清屏数据。 */
    uint8_t page; /**< 当前清零页。 */

    if (oled_driver_busy) {
        return OLED_BUSY;
    }
    oled_driver_busy = true;
    Wire.begin();
    Wire.setClock(OLED_I2C_CLOCK_HZ);
    delay(120U); /* SSD1306 上电需要等待复位稳定。 */

    if (oled_send_command_blocking(init_commands,
                                   (uint16_t)sizeof(init_commands)) != OLED_OK) {
        oled_driver_busy = false;
        return OLED_ERROR;
    }
    /* 页寻址模式下必须逐页设置地址并写入 128 个零字节。 */
    for (page = 0U; page < OLED_PHYSICAL_PAGES; ++page) {
        uint8_t command[3];
        oled_prepare_page_command(page, 0U, command);
        if (oled_send_command_blocking(command, sizeof(command)) != OLED_OK ||
            oled_send_data_blocking(zeros, sizeof(zeros)) != OLED_OK) {
            oled_driver_busy = false;
            return OLED_ERROR;
        }
    }
    {
        /* 所有页清零成功后才发送 AF，避免上电随机画面。 */
        const uint8_t display_on = 0xAFU;
        if (oled_send_command_blocking(&display_on, 1U) != OLED_OK) {
            oled_driver_busy = false;
            return OLED_ERROR;
        }
    }
    oled_driver_busy = false;
    return OLED_OK;
}

/** 按页阻塞发送核心生成的连续差异区间。 */
OLED_Status OLED_DriverWriteBlocking(void)
{
    const uint8_t *buffer = OLED_InternalGetTransferBuffer(); /**< 冻结的传输帧。 */
    uint8_t page; /**< 当前检查或发送的物理页。 */

    if (oled_driver_busy) {
        return OLED_BUSY;
    }
    oled_driver_busy = true;
    for (page = 0U; page < OLED_PHYSICAL_PAGES; ++page) {
        uint8_t min_x = OLED_InternalGetTransferMinX(page); /**< 本页首个差异列。 */
        uint8_t max_x = OLED_InternalGetTransferMaxX(page); /**< 本页最后差异列。 */
        uint8_t command[3];
        uint16_t length; /**< 本页需要连续发送的字节数。 */

        if (min_x >= OLED_PHYSICAL_WIDTH) {
            continue;
        }
        /* 每个脏页先重新定位，再从 min_x 连续写到 max_x。 */
        oled_prepare_page_command(page, min_x, command);
        length = (uint16_t)(max_x - min_x + 1U);
        if (oled_send_command_blocking(command, sizeof(command)) != OLED_OK ||
            oled_send_data_blocking(buffer + (uint16_t)page * OLED_PHYSICAL_WIDTH +
                                        min_x,
                                    length) != OLED_OK) {
            oled_driver_busy = false;
            return OLED_ERROR;
        }
    }
    oled_driver_busy = false;
    return OLED_OK;
}

OLED_Status OLED_DriverWriteIT(void)
{
    return OLED_UNSUPPORTED;
}

OLED_Status OLED_DriverWriteDMA(void)
{
    return OLED_UNSUPPORTED;
}

bool OLED_DriverIsBusy(void)
{
    return oled_driver_busy;
}

OLED_Status OLED_DriverSetContrast(uint8_t value)
{
    const uint8_t command[2] = {0x81U, value}; /* 对比度命令及参数。 */

    if (oled_driver_busy) {
        return OLED_BUSY;
    }
    return oled_send_command_blocking(command, sizeof(command));
}

OLED_Status OLED_DriverSetPowerSave(bool enable)
{
    uint8_t command = enable ? 0xAEU : 0xAFU; /* AE 关屏，AF 开屏。 */

    if (oled_driver_busy) {
        return OLED_BUSY;
    }
    return oled_send_command_blocking(&command, 1U);
}
