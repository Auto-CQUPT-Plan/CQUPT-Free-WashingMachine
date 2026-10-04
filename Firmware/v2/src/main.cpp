#include <Arduino.h>

#include "config.h"
#include "generated/kk_app_font.h"
#include "kk_oled.h"
#include "rotary.h"
#include "washer.h"
#include "washer_app.h"

#include "kk_ui.h"

#ifdef OLED_DIAGNOSTIC

#include <Wire.h>

/*
 * OLED 诊断模式（pio run -e diag）：绕过 KK_OLED/驱动，直接用 Wire 测试硬件。
 * USB 串口 115200 查看输出；按提示观察屏幕，把串口结果反馈回来即可定位问题。
 */

namespace {

constexpr uint8_t DIAG_I2C_SDA = PIN_OLED_SDA;
constexpr uint8_t DIAG_I2C_SCL = PIN_OLED_SCL;

/** SSD1306 标准初始化序列（页寻址），与 kk_oled_driver.cpp 保持一致。 */
const uint8_t DIAG_INIT_COMMANDS[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
    0x8D, 0x14, 0x20, 0x02, 0xA1, 0xC8, 0xDA, 0x12,
    0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
};

bool diag_cmd(uint8_t addr, const uint8_t *commands, size_t length)
{
    Wire.beginTransmission(addr);
    Wire.write((uint8_t)0x00);
    Wire.write(commands, length);
    return Wire.endTransmission() == 0;
}

bool diag_data(uint8_t addr, const uint8_t *data, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        size_t chunk = length - offset;
        if (chunk > 64U) {
            chunk = 64U;
        }
        Wire.beginTransmission(addr);
        Wire.write((uint8_t)0x40);
        Wire.write(data + offset, chunk);
        if (Wire.endTransmission() != 0) {
            return false;
        }
        offset += chunk;
    }
    return true;
}

/** 全屏边框：上下两页全亮，中间页首末列亮，用于验证页寻址和列偏移。 */
bool diag_draw_border(uint8_t addr)
{
    for (uint8_t page = 0; page < 8; ++page) {
        const uint8_t seek[3] = {(uint8_t)(0xB0 | page), 0x00, 0x10};
        uint8_t row[128];
        if (!diag_cmd(addr, seek, sizeof(seek))) {
            return false;
        }
        for (uint16_t i = 0; i < sizeof(row); ++i) {
            row[i] = (page == 0 || page == 7) ? 0xFF
                                              : (i == 0 || i == 127) ? 0xFF : 0x00;
        }
        if (!diag_data(addr, row, sizeof(row))) {
            return false;
        }
    }
    return true;
}

void diag_wait(uint32_t ms)
{
    uint32_t start = millis();
    while (millis() - start < ms) {
        delay(10);
    }
}

[[noreturn]] void run_oled_diagnostic()
{
    Serial.begin(115200);
    delay(2000);
    Serial.println();
    Serial.println(F("=== OLED I2C 诊断模式 ==="));

    Wire.begin(DIAG_I2C_SDA, DIAG_I2C_SCL);
    Wire.setClock(100000);

    /* [1/4] 总线扫描：默认 D2=SDA/D1=SCL，找不到再试引脚对调。 */
    Serial.println(F("[1/4] 扫描 I2C 总线..."));
    uint8_t address = 0;
    for (uint8_t attempt = 0; attempt < 2 && address == 0; ++attempt) {
        if (attempt == 1) {
            Serial.println(F("  默认引脚没扫到，尝试对调 SDA/SCL..."));
            Wire.begin(DIAG_I2C_SCL, DIAG_I2C_SDA);
        }
        for (uint8_t addr = 8; addr < 120; ++addr) {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) {
                Serial.print(F("  发现设备: 0x"));
                Serial.println(addr, HEX);
                if (address == 0) {
                    address = addr;
                }
            }
        }
    }

    if (address == 0) {
        Serial.println(F("  !! 总线上没有任何设备。排查："));
        Serial.println(F("  - OLED 的 SDA 接 D2(GPIO4)、SCL 接 D1(GPIO5)"));
        Serial.println(F("  - VCC 接 3V3、GND 共地；模块是 4 针 I2C 版而非 SPI 版"));
        pinMode(LED_BUILTIN, OUTPUT);
        while (true) {
            digitalWrite(LED_BUILTIN, LOW);
            delay(500);
            digitalWrite(LED_BUILTIN, HIGH);
            delay(500);
        }
    }
    Serial.print(F("  使用地址 0x"));
    Serial.println(address, HEX);

    /* [2/4] 整屏点亮：0xA5 无视显存内容，亮 = 总线/地址/供电/控制器都通。 */
    Serial.println(F("[2/4] 测试A: 整屏点亮命令 0xA5，屏幕应全亮 3 秒..."));
    {
        const uint8_t on[] = {0xAE, 0xA5};
        diag_cmd(address, on, sizeof(on));
    }
    bool test_a = true; /* 命令层面成功与否由用户目视确认。 */
    diag_wait(3000);

    /* [3/4] 标准初始化 + 边框：验证页寻址写入。 */
    Serial.println(F("[3/4] 测试B: 初始化 + 边框图案，应显示白色边框 3 秒..."));
    bool test_b = diag_cmd(address, DIAG_INIT_COMMANDS, sizeof(DIAG_INIT_COMMANDS));
    test_b = diag_draw_border(address) && test_b;
    {
        const uint8_t on[] = {0xAF};
        test_b = diag_cmd(address, on, sizeof(on)) && test_b;
    }
    Serial.println(test_b ? F("  命令发送成功") : F("  !! 命令发送失败（NACK）"));
    diag_wait(3000);

    Serial.println(F("[4/4] 判定（请对照屏幕现象）："));
    Serial.println(F("  A亮+B亮  -> 硬件没问题，把本输出发给开发者查固件绘制链路"));
    Serial.println(F("  A亮+B不亮 -> 屏幕非标准 SSD1306（列偏移/命令差异），告知屏幕型号"));
    Serial.println(F("  A不亮    -> 总线通但命令未生效：模块供电或控制器异常"));
    Serial.println(F("进入循环：边框会每秒反色一次，串口持续输出心跳。"));

    bool invert = false;
    while (true) {
        const uint8_t toggle[] = {(uint8_t)(invert ? 0xA6 : 0xA7)};
        diag_cmd(address, toggle, sizeof(toggle));
        invert = !invert;
        Serial.println(F("alive"));
        diag_wait(800);
    }
}

}  // namespace

#endif  // OLED_DIAGNOSTIC

namespace {

/** 上电画面：店铺名 + 版本，居中绘制后交还 KK_UI 接管刷新。 */
void draw_splash()
{
    const char *title = "樱花洗衣券铺";
    const char *subtitle = "CQUPT v2.0.0";

    OLED_Clear();
    OLED_SetFont(kk_font_app_title);
    OLED_DrawUTF8((int16_t)((OLED_GetWidth() - OLED_GetUTF8Width(title)) / 2),
                  18, title);
    OLED_SetFont(kk_font_app_body);
    OLED_DrawUTF8((int16_t)((OLED_GetWidth() - OLED_GetUTF8Width(subtitle)) / 2),
                  44, subtitle);
    OLED_Update();
}

/** OLED 初始化失败（未接线/地址不对）时慢闪板载 LED 提示。 */
[[noreturn]] void blink_error()
{
    pinMode(LED_BUILTIN, OUTPUT);
    while (true) {
        digitalWrite(LED_BUILTIN, LOW);
        delay(120);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(380);
    }
}

}  // namespace

void setup()
{
#ifdef OLED_DIAGNOSTIC
    run_oled_diagnostic(); /* 诊断模式不返回。 */
#endif
    washer_begin(WASHER_BAUD_RATE);
    rotary::begin();

    if (OLED_Init() != OLED_OK) {
        blink_error();
    }
    draw_splash();
    delay(900);

    KK_UI_AppReset(millis());
    KK_UI_Init(KK_UI_AppGet());
}

void loop()
{
    uint32_t now = millis();
    KK_UI_Input input = {};

    input.encoder_delta = rotary::take_steps();
    if (rotary::ok_held()) {
        input.keys = KK_UI_KEY_OK;
    }

    KK_UI_AppUpdate(now);
    KK_UI_Update(now, input);
    KK_UI_AppProcessEvents();
}
