# Firmware 前后端分离说明

这个目录仍然是 ESP8266 / PlatformIO 固件项目，但页面代码已经从 `src/main.cpp` 中拆出，方便单独维护。

## 目录结构

```text
Firmware/
├─ src/
│  ├─ main.cpp                    # 后端：WiFi AP、HTTP 路由、串口指令
│  └─ generated/web_assets.h      # 自动生成：把前端文件嵌入固件
├─ web/
│  ├─ index.html                  # 前端页面结构
│  ├─ styles.css                  # 前端样式
│  └─ app.js                      # 前端交互与 fetch 请求
├─ scripts/
│  ├─ generate_web_assets.py      # 生成 src/generated/web_assets.h
│  └─ platformio_prebuild.py      # PlatformIO 编译前自动生成
└─ platformio.ini
```

## 开发方式

1. 修改前端：只编辑 `web/index.html`、`web/styles.css`、`web/app.js`。
2. 修改后端：编辑 `src/main.cpp`，保留现有接口语义：
   - `GET /` 返回页面
   - `GET /styles.css` 返回样式
   - `GET /app.js` 返回脚本
   - `POST /1`、`/3`、`/4`、`/4p`、`/6` 发送预置串口指令
   - `POST /custom` 发送自定义十六进制指令
3. 正常执行 PlatformIO 构建即可，`extra_scripts` 会自动重新生成 `src/generated/web_assets.h`。

手动生成也可以运行：

```bash
python3 scripts/generate_web_assets.py
```

Windows / PowerShell 下仍然从 `Firmware` 目录构建：

```powershell
cd D:\admin\esp32_projects\CQUPT-Free-WashingMachine\Firmware
pio run -e nodemcuv2
```

## 说明

当前方案没有引入 LittleFS 上传流程，也没有依赖 CDN、npm、React/Vue 等重型前端工具。前端文件会在编译前嵌入到程序 Flash 的 PROGMEM 中，上传固件后页面随固件一起更新，适合 ESP8266 这种资源较小的场景。
