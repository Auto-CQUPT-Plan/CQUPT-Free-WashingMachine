# KK_UI Runtime

KK_UI 是依赖 [KK_OLED](https://gitee.com/keysking/kk_oled) 的轻量级、单例、无堆分配 OLED 菜单 UI。运行时源码位于 `include/` 与 `src/`，应用页面、业务事件、字体和图标由项目层持有。

第一版内置：首页图标选择器、可嵌套普通菜单、只读信息页、全屏自定义页、整数/开关编辑、确认框、消息框、Toast、事件队列和无状态绘制辅助。

## 基本约束

- 以 128×64、1 bit 单色图形 OLED 为已验证参考布局。
- 不使用动态内存，不分配帧缓冲；刷新缓冲由 KK_OLED 持有。
- 页面拓扑静态声明，菜单项可动态隐藏或禁用。
- 输入和动画由毫秒时间驱动，刷新可选择 blocking、IT 或 DMA。
- 项目代码不得绕过 KK_UI 直接清屏或提交完整画面刷新。

公共接口以 `include/kk_ui.h` 和 `include/kk_ui_draw.h` 为准，编译期配置位于 `include/kk_ui_config.h`。首次接入、项目使用和受控扩展分别由发布仓库中的 `kk-ui-port`、`kk-ui-use` 和 `kk-ui-extend` Skills 指导。

许可证：[MIT](LICENSE.txt)。
