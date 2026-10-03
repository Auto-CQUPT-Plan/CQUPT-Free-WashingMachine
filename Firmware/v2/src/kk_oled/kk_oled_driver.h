#ifndef KK_OLED_DRIVER_H
#define KK_OLED_DRIVER_H

#include "kk_oled.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 当前 SSD1306 模组的物理宽度。 */
#define OLED_PHYSICAL_WIDTH 128U
/** 当前 SSD1306 模组的物理高度。 */
#define OLED_PHYSICAL_HEIGHT 64U
/** 每页 8 行像素，因此 64 行屏幕共有 8 页。 */
#define OLED_PHYSICAL_PAGES (OLED_PHYSICAL_HEIGHT / 8U)

/** 初始化 I2C 总线与 SSD1306，清空全部页后点亮显示。 */
OLED_Status OLED_DriverInit(void);

/** 阻塞发送核心准备好的全部差异页。 */
OLED_Status OLED_DriverWriteBlocking(void);

/** ESP8266 的 Wire 外设没有可用的中断/DMA 传输能力，固定返回 OLED_UNSUPPORTED。 */
OLED_Status OLED_DriverWriteIT(void);

/** 同上，固定返回 OLED_UNSUPPORTED。 */
OLED_Status OLED_DriverWriteDMA(void);

/** 查询驱动状态机是否正在传输（阻塞实现恒为 false）。 */
bool OLED_DriverIsBusy(void);

/** 阻塞发送 SSD1306 对比度命令。 */
OLED_Status OLED_DriverSetContrast(uint8_t value);

/** 阻塞发送显示关闭 AE 或显示开启 AF。 */
OLED_Status OLED_DriverSetPowerSave(bool enable);

#ifdef __cplusplus
}
#endif

#endif
