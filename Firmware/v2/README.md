# Firmware v2 — OLED 屏幕 + 旋转编码器 本地控制固件

v2 把 v1 的「手机连热点开网页」交互，换成了 **0.96 寸 OLED 屏幕 + 360° 旋转编码器**的本地交互：设备即插即用，不需要手机、不建热点，旋转选择洗衣模式、按下确认，ESP8266 直接向洗衣机控制板发送对应金额的串口指令。

> 屏幕驱动基于 [KK_OLED](https://gitee.com/keysking/kk_oled)、界面框架基于 [KK_UI](https://gitee.com/keysking/kk_ui)（均为 MIT 许可，以可编辑源码形式移植到 `src/kk_oled/`、`src/kk_ui/`，并按 ESP8266 + SSD1306 做了驱动适配）。

## 界面与操作

上电先显示「樱花洗衣券铺」启动画面，随后进入图标首页：

```text
首页
 ├── [洗衣机图标] 洗衣 → 选择模式
 │                     ├ 1元脱水     ─┐
 │                     ├ 3元快洗      │ 确认框「启动 4元标准洗?」
 │                     ├ 4元标准洗    │   确定 → 串口发送 → Toast「指令已发送」
 │                     ├ 4元加强洗    │   取消 → 返回菜单
 │                     ├ 6元大件洗   ─┘
 │                     └ 屏幕亮度   → 10~100% 编辑，实时预览对比度
 └ [i 图标]       信息 → 关于（固件版本 / 串口波特率 / 操作说明）
```

- **旋转编码器**：移动光标 / 调整数值（顺时针向下）
- **按下编码器**：确认 / 进入；信息页与返回项按下即返回
- 6 元大件洗会自动间隔 100ms 连发两段指令帧，与 v1 行为一致

## 硬件清单

| 器件 | 说明 |
| ---- | ---- |
| ESP8266 | NodeMCU V2（ESP-12E/F，4MB Flash），其他 ESP8266 板亦可 |
| 0.96 寸 OLED | SSD1306 控制器，128×64，**I2C 四针**（GND/VCC/SCL/SDA），地址默认 0x3C |
| 旋转编码器 | EC11 类带按压开关模块（KY-040 等），360° 旋转，A/B/C 五针 |
| 洗衣机控制板 | 商用投币洗衣机主板，串口 2400 波特率 |

## 接线

### 1. ESP8266 ↔ 洗衣机控制板（与 v1 相同）

| NodeMCU | 洗衣机控制板卡 |
| ------- | -------------- |
| VCC (5V) | 5V |
| GND     | GND |
| TXD     | RXD |
| RXD     | TXD |

> 串口为 UART0（TX=GPIO1，RX=GPIO0），波特率 **2400**。若烧录固件时失败，先断开与控制板的 TX/RX 连线再烧（GPIO0/GPIO1 参与启动与下载时序）。

### 2. ESP8266 ↔ OLED（I2C）

| OLED | NodeMCU | 备注 |
| ---- | ------- | ---- |
| GND  | GND | |
| VCC  | 3V3 | |
| SCL  | D1 (GPIO5) | ESP8266 Wire 默认 SCL |
| SDA  | D2 (GPIO4) | ESP8266 Wire 默认 SDA |

> 模组地址常见为 0x3C；少数为 0x3D，可在 `src/kk_oled/kk_oled_driver.cpp` 的 `OLED_I2C_ADDRESS` 修改。屏幕不亮且板载 LED 慢闪，通常是接线或地址不对。

### 3. ESP8266 ↔ 旋转编码器

| 编码器 | NodeMCU | 备注 |
| ------ | ------- | ---- |
| + (VCC) | 3V3 | 模块自带上拉；裸开关可悬空 |
| GND    | GND | |
| CLK (A) | D5 (GPIO14) | A 相 |
| DT (B)  | D6 (GPIO12) | B 相 |
| SW      | D7 (GPIO13) | 按压开关，按下为低 |

## 目录结构

```text
Firmware/v2/
├─ platformio.ini              # 构建配置（阻塞刷新宏、头文件路径）
├─ src/
│  ├─ main.cpp                 # 入口：初始化串口/编码器/OLED，启动画面，主循环
│  ├─ config.h                 # 引脚与波特率等硬件参数的唯一来源
│  ├─ washer.h/.cpp            # 洗衣指令帧与串口发送（继承 v1 协议）
│  ├─ rotary.h/.cpp            # 编码器四状态正交解码（中断）+ 按键读取
│  ├─ washer_app.h/.c          # KK_UI 应用描述：首页/菜单/确认框/亮度/关于页
│  ├─ kk_oled/                 # KK_OLED 图形库（MIT，含自写 ESP8266 驱动）
│  │  └─ kk_oled_driver.cpp    # SSD1306 + Wire 阻塞驱动（IT/DMA 返回不支持）
│  ├─ kk_ui/                   # KK_UI 界面框架（MIT）
│  └─ generated/               # 自动生成，勿手改
│     ├─ kk_app_font.h/.c      # 中文字模（LEDFont 生成的 u8g2 格式子集）
│     ├─ app_icons.h           # 32×32 首页图标（PIL 生成的 XBM）
│     └─ font-resources.json   # 字模来源记录
├─ scripts/
│  ├─ generate_font.py         # 扫描 UI 文案 → 调 LEDFont API 重新生成字模
│  └─ generate_icons.py        # 重新生成首页图标
└─ include/ lib/ test/         # PlatformIO 标准目录（暂未使用）
```

## 软件说明

- **主循环**（`src/main.cpp`）：每轮把编码器增量与按键电平交给 `KK_UI_Update()`，再由 `KK_UI_AppProcessEvents()` 处理业务事件（发指令、调对比度、弹 Toast）。刷新采用阻塞模式（`KK_UI_REFRESH_MODE=KK_UI_REFRESH_BLOCKING`），KK_OLED 只把脏页差异经 I2C 发给屏幕。
- **编码器解码**（`src/rotary.cpp`）：A/B 相双边沿中断 + 16 项跳变表，一个机械档位计 1，双比特毛刺自动丢弃。
- **中文字模**：运行时不带全量字库。改了任何 UI 文案后，运行 `python scripts/generate_font.py` 重新生成字模（脚本会扫描源码里的字符串字面量，自动收集所需字符，调用 LEDFont 在线服务生成 WenQuanYi 12/14px 子集）。
- **图标**：改图标就编辑 `scripts/generate_icons.py` 里的绘图代码并重新运行。

## 构建与烧录

```powershell
cd Firmware/v2
pio run -e nodemcuv2                        # 编译
pio run -e nodemcuv2 --upload-port COM4     # 烧录
```

当前资源占用：RAM 46%（双缓冲显存 + UI 状态），Flash 28%。

## 与 v1 的差异

| | v1（`Firmware/v1`） | v2（本目录） |
| --- | --- | --- |
| 交互 | 手机连接热点，网页点选 | OLED 屏幕 + 编码器，本地操作 |
| WiFi | 开放热点 + Web 服务器 | 完全不启用，更省电 |
| 自定义 HEX | 网页输入任意指令 | 已移除（逆向调试请用 v1） |
| 串口指令 | 相同（0xAA 帧头 2400 波特率） | 相同 |
| 前端维护 | `web/` 编译期嵌入固件 | 字模/图标由脚本生成后编译 |

## 待验证

代码已通过 PlatformIO 完整编译，但尚未在实物上验证显示与交互；实机接好后建议按以下顺序检查：

1. 上电屏幕点亮并显示启动画面（不亮 → 查 I2C 地址/接线）
2. 首页图标、旋转方向是否符合直觉（反了就对调 D5/D6）
3. 确认后用 USB 串口（2400 波特率）观察是否输出预期 HEX，再接洗衣机主板实测
