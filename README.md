# CQUPT-Free-WashingMachine
>  **重庆邮电大学-免费洗衣机**

> 本项目为 *Auto CQUPT Plan* 的一部分

> [!Warning]
>
> **本项目仅供学习研究，请勿用于非法用途，否则后果自负！！！**

![images](./images/cover.png)

**适配平台：ESP8266 系列 MCU**

**相关博客：[补络阁](https://blog.tuf3i.click/posts/acp-washing-machine/)**

### 1. 默认配置：

```c++
#define BAUD_RATE 2400
const char* SSID = "WashingMachine";
const char* PASSWORD = "12345678";
IPAddress localIP(172, 16, 0, 1);
IPAddress gateway(172, 16, 0, 1);
IPAddress subnet(255, 255, 255, 0);
```

| 字段      | 值             | 备注                           |
| --------- | -------------- | ------------------------------ |
| BAUD_RATE | 2400           | UATR波特率 **2400**            |
| SSID      | WashingMachine | 热点名称 **WashingMachine**    |
| PASSWORD  | 12345678       | 热点密码 **12345678**          |
| localIP   | 172.16.0.1     | 本机IP **172.16.0.1**          |
| gateway   | 172.16.0.1     | 热点默认网关 **172.16.0.1**    |
| subnet    | 255.255.255.0  | 热点子网掩码 **255.255.255.0** |

### 2. 关键函数

#### 2.1 UATR通信

```c++
const uint8_t CMD_1YUAN[]      = {0xAA, 0x06, 0x01, 0x98, 0x04, 0x00, 0x75, 0x03};
const uint8_t CMD_3YUAN[]      = {0xAA, 0x06, 0x01, 0x98, 0x03, 0x01, 0xD0, 0xAC};
const uint8_t CMD_4YUAN[]      = {0xAA, 0x06, 0x01, 0x98, 0x02, 0x02, 0xB2, 0x0D};
const uint8_t CMD_4YUAN_PLUS[] = {0xAA, 0x06, 0x01, 0x98, 0x02, 0x02, 0xB2, 0x0A};
const uint8_t CMD_6YUAN_1[]    = {0xAA, 0x06, 0x01, 0x98, 0x01, 0x03, 0x51, 0xC6};
const uint8_t CMD_6YUAN_2[]    = {0xAA, 0x06, 0x00, 0x9F, 0x01, 0x03, 0x09};

void setupSerial(unsigned long BAUD_RATE) {
    Serial.begin(BAUD_RATE);
}

void sendCmd(const uint8_t* data, size_t len) {
    Serial.write(data, len);
    Serial.flush();
}
```

### 3. 接线示例

>  以下是 **Node MCU** 与 **洗衣机控制板卡** 的线路连接

| NodeMCU | 洗衣机控制板卡 |
| ------- | -------------- |
| VCC     | 5V             |
| GND     | GND            |
| TXD     | RXD            |
| RXD     | TXD            |

### 4. 烧写固件

```powershell
pio run --upload-port COM4
```
