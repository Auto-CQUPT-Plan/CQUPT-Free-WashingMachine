#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>

#include "generated/web_assets.h"

#define BAUD_RATE 2400

const char *SSID = "好孩子不玩米";
const char* PASSWORD = "12345678";
IPAddress localIP(172, 16, 0, 1);
IPAddress gateway(172, 16, 0, 1);
IPAddress subnet(255, 255, 255, 0);

ESP8266WebServer server(80);

const uint8_t CMD_1YUAN[]      = {0xAA, 0x06, 0x01, 0x98, 0x04, 0x00, 0x75, 0x03};
const uint8_t CMD_3YUAN[]      = {0xAA, 0x06, 0x01, 0x98, 0x03, 0x01, 0xD0, 0xAC};
const uint8_t CMD_4YUAN[]      = {0xAA, 0x06, 0x01, 0x98, 0x02, 0x02, 0xB2, 0x0D};
const uint8_t CMD_4YUAN_PLUS[] = {0xAA, 0x06, 0x01, 0x98, 0x02, 0x02, 0xB2, 0x0A};
const uint8_t CMD_6YUAN_1[]    = {0xAA, 0x06, 0x01, 0x98, 0x01, 0x03, 0x51, 0xC6};
const uint8_t CMD_6YUAN_2[]    = {0xAA, 0x06, 0x00, 0x9F, 0x01, 0x03, 0x09};

void sendCmd(const uint8_t* data, size_t len) {
    Serial.write(data, len);
    Serial.flush();
}

void handleRoot() {
    server.send_P(200, "text/html; charset=utf-8", WEB_ASSET_INDEX_CONTENT);
}

void registerStaticAssets() {
    for (size_t i = 0; i < WEB_ASSET_COUNT; ++i) {
        server.on(WEB_ASSETS[i].path, HTTP_GET, [i]() {
            server.send_P(200, WEB_ASSETS[i].contentType, WEB_ASSETS[i].content);
        });
    }
}

void setup() {
    Serial.begin(BAUD_RATE);
    delay(1000);

    LittleFS.begin();

    WiFi.softAPConfig(localIP, gateway, subnet);
    WiFi.softAP(SSID, PASSWORD);

    server.on("/", HTTP_GET, handleRoot);
    registerStaticAssets();

    server.on("/1", HTTP_POST, [](){ sendCmd(CMD_1YUAN, sizeof(CMD_1YUAN)); server.send(200); });
    server.on("/3", HTTP_POST, [](){ sendCmd(CMD_3YUAN, sizeof(CMD_3YUAN)); server.send(200); });
    server.on("/4", HTTP_POST, [](){ sendCmd(CMD_4YUAN, sizeof(CMD_4YUAN)); server.send(200); });
    server.on("/4p", HTTP_POST, [](){ sendCmd(CMD_4YUAN_PLUS, sizeof(CMD_4YUAN_PLUS)); server.send(200); });
    server.on("/6", HTTP_POST, [](){
        sendCmd(CMD_6YUAN_1, sizeof(CMD_6YUAN_1));
        delay(100);
        sendCmd(CMD_6YUAN_2, sizeof(CMD_6YUAN_2));
        server.send(200);
    });
    server.on("/custom", HTTP_POST, [](){
        String hex = server.arg("d");
        hex.replace(" ", "");
        for(size_t i=0; i<hex.length(); i+=2) {
            if(i+1 < hex.length()) {
                String b = hex.substring(i, i+2);
                Serial.write((uint8_t)strtol(b.c_str(), NULL, 16));
            }
        }
        Serial.flush();
        server.send(200);
    });

    server.begin();
}

void loop() {
    server.handleClient();
}
