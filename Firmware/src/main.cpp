#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>

#define BAUD_RATE 2400

const char* SSID = "WashingMachine";
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
    server.send(200, "text/html", R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>洗衣机控制</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;padding:20px}
.container{max-width:400px;margin:0 auto;background:rgba(255,255,255,0.95);border-radius:20px;padding:30px;box-shadow:0 20px 60px rgba(0,0,0,0.3)}
h1{text-align:center;color:#333;margin-bottom:30px;font-size:24px}
.btn-grid{display:grid;gap:15px;margin-bottom:30px}
button{padding:18px;border:none;border-radius:12px;font-size:16px;font-weight:600;cursor:pointer;transition:all 0.3s;color:white;text-shadow:0 1px 2px rgba(0,0,0,0.2)}
button:active{transform:scale(0.95)}
.btn-1{background:linear-gradient(135deg,#f093fb 0%,#f5576c 100%)}
.btn-3{background:linear-gradient(135deg,#4facfe 0%,#00f2fe 100%)}
.btn-4{background:linear-gradient(135deg,#43e97b 0%,#38f9d7 100%)}
.btn-4p{background:linear-gradient(135deg,#fa709a 0%,#fee140 100%)}
.btn-6{background:linear-gradient(135deg,#30cfd0 0%,#330867 100%)}
.custom-section{border-top:2px dashed #ddd;padding-top:25px}
.custom-section h3{color:#666;margin-bottom:15px;font-size:14px;text-transform:uppercase;letter-spacing:1px}
textarea{width:100%;padding:12px;border:2px solid #e0e0e0;border-radius:8px;font-family:monospace;font-size:14px;resize:vertical;min-height:60px;margin-bottom:10px}
textarea:focus{outline:none;border-color:#667eea}
.btn-send{width:100%;background:#333;color:white}
.status{text-align:center;margin-top:15px;padding:10px;border-radius:8px;font-size:14px;opacity:0;transition:opacity 0.3s}
.status.show{opacity:1}
.status.success{background:#d4edda;color:#155724}
.status.error{background:#f8d7da;color:#721c24}
</style>
</head>
<body>
<div class="container">
<h1>🧺 洗衣机控制面板</h1>
<div class="btn-grid">
<button class="btn-1" onclick="send('1')">1元脱水</button>
<button class="btn-3" onclick="send('3')">3元快速洗</button>
<button class="btn-4" onclick="send('4')">4元标准洗</button>
<button class="btn-4p" onclick="send('4p')">4元标准洗(加强版)</button>
<button class="btn-6" onclick="send('6')">6元加强洗</button>
</div>
<div class="custom-section">
<h3>自定义指令 (十六进制)</h3>
<textarea id="custom" placeholder="AA 06 01 98..."></textarea>
<button class="btn-send" onclick="sendCustom()">发送自定义指令</button>
</div>
<div id="status" class="status"></div>
</div>
<script>
function show(msg,err){const e=document.getElementById('status');e.textContent=msg;e.className='status show '+(err?'error':'success');setTimeout(()=>e.className='status',2000)}
async function send(t){try{await fetch('/'+t,{method:'POST'});show('已发送')}catch(e){show('失败',1)}}
async function sendCustom(){const d=document.getElementById('custom').value.trim();if(!d)return;try{await fetch('/custom',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'d='+encodeURIComponent(d)});show('已发送')}catch(e){show('失败',1)}}
</script>
</body>
</html>
)rawliteral");
}

void setup() {
    Serial.begin(BAUD_RATE);
    delay(1000);
    
    LittleFS.begin();
    
    WiFi.softAPConfig(localIP, gateway, subnet);
    WiFi.softAP(SSID, PASSWORD);
    
    server.on("/", handleRoot);
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