#include <Arduino.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <Servo.h>

#include "project_config.h"
#include "voice_protocol.h"

namespace {

struct ServoCalibration {
  uint8_t pin;
  uint8_t neutralAngle;
  uint8_t onAngle;
  uint8_t offAngle;
};

constexpr ServoCalibration SERVO_CONFIG[2] = {
    {ProjectConfig::SERVO_1_PIN, ProjectConfig::SERVO_1_NEUTRAL_ANGLE,
     ProjectConfig::SERVO_1_ON_ANGLE, ProjectConfig::SERVO_1_OFF_ANGLE},
    {ProjectConfig::SERVO_2_PIN, ProjectConfig::SERVO_2_NEUTRAL_ANGLE,
     ProjectConfig::SERVO_2_ON_ANGLE, ProjectConfig::SERVO_2_OFF_ANGLE},
};

Servo servos[2];
HardwareSerial& voiceSerial = Serial;
VoiceProtocol::Parser voiceParser;
ESP8266WebServer server(80);
DNSServer dnsServer;

bool lightKnown[2] = {false, false};
bool lightOn[2] = {false, false};
bool actionBusy = false;
bool fallbackApRunning = false;
bool mdnsRunning = false;
uint32_t disconnectedSinceMs = 0;
uint32_t lastWifiRetryMs = 0;
char fallbackApName[32] = {0};

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <meta name="theme-color" content="#172033">
  <title>双路灯光控制</title>
  <style>
    :root{color-scheme:dark;--bg:#0c1220;--panel:#172033;--line:#2a3650;--text:#f5f7fb;--muted:#9da9bd;--on:#22c55e;--off:#ef4444;--accent:#38bdf8}
    *{box-sizing:border-box}body{margin:0;min-height:100vh;font-family:system-ui,-apple-system,"Segoe UI","Microsoft YaHei",sans-serif;background:radial-gradient(circle at top,#1e3150 0,var(--bg) 55%);color:var(--text);display:grid;place-items:center;padding:22px}
    main{width:min(760px,100%)}header{margin-bottom:20px}h1{margin:0 0 8px;font-size:clamp(27px,6vw,42px)}.sub{color:var(--muted);margin:0}.grid{display:grid;grid-template-columns:repeat(2,1fr);gap:16px}.card,.all{background:rgba(23,32,51,.94);border:1px solid var(--line);border-radius:20px;padding:20px;box-shadow:0 16px 45px #0005}.card h2{margin:0;font-size:22px;display:flex;align-items:center;justify-content:space-between;gap:12px}.badge{font-size:13px;padding:6px 10px;border-radius:999px;background:#334155;color:#d8e0ec}.badge.on{background:#14532d;color:#bbf7d0}.badge.off{background:#7f1d1d;color:#fecaca}.buttons{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:28px}button{appearance:none;border:0;border-radius:13px;padding:14px 12px;font-size:16px;font-weight:750;color:white;cursor:pointer;transition:transform .12s,filter .12s,opacity .12s}button:hover{filter:brightness(1.12)}button:active{transform:scale(.97)}button:disabled{opacity:.45;cursor:wait}.on-btn{background:var(--on)}.off-btn{background:var(--off)}.all{margin-top:16px}.all h2{margin:0 0 14px}.all .buttons{margin-top:0}.all-on{background:#0d9488}.all-off{background:#7c3aed}.status{min-height:24px;text-align:center;color:var(--muted);margin:15px 0 0}.status.error{color:#fca5a5}.foot{text-align:center;color:#718096;font-size:13px;margin-top:12px}@media(max-width:560px){.grid{grid-template-columns:1fr}.card,.all{padding:17px}}
  </style>
</head>
<body>
<main>
  <header><h1>双路灯光控制</h1><p class="sub">ESP8266 · 舵机机械开关</p></header>
  <section class="grid">
    <article class="card"><h2>灯光 1 <span id="s1" class="badge">未知</span></h2><div class="buttons"><button class="on-btn" data-target="1" data-state="on">打开</button><button class="off-btn" data-target="1" data-state="off">关闭</button></div></article>
    <article class="card"><h2>灯光 2 <span id="s2" class="badge">未知</span></h2><div class="buttons"><button class="on-btn" data-target="2" data-state="on">打开</button><button class="off-btn" data-target="2" data-state="off">关闭</button></div></article>
  </section>
  <section class="all"><h2>一键控制</h2><div class="buttons"><button class="all-on" data-target="all" data-state="on">全部打开</button><button class="all-off" data-target="all" data-state="off">全部关闭</button></div></section>
  <p id="msg" class="status">正在读取设备状态…</p>
  <p class="foot">同一局域网可访问 light-control.local</p>
</main>
<script>
const msg=document.querySelector('#msg'),buttons=[...document.querySelectorAll('button')];
function badge(id,value){const e=document.querySelector(id);e.className='badge'+(value===true?' on':value===false?' off':'');e.textContent=value===true?'已打开':value===false?'已关闭':'未知'}
function render(data){badge('#s1',data.light1);badge('#s2',data.light2);msg.className='status';msg.textContent=`${data.network} · ${data.ip}`}
async function state(){try{const r=await fetch('/api/state',{cache:'no-store'});if(!r.ok)throw Error();render(await r.json())}catch(e){msg.className='status error';msg.textContent='无法读取设备状态'}}
async function act(target,value){buttons.forEach(b=>b.disabled=true);msg.className='status';msg.textContent='舵机动作中…';try{const r=await fetch(`/api/action?target=${target}&state=${value}`,{method:'POST'});const data=await r.json();if(!r.ok)throw Error(data.error||'请求失败');render(data)}catch(e){msg.className='status error';msg.textContent=e.message||'控制失败'}finally{buttons.forEach(b=>b.disabled=false)}}
buttons.forEach(b=>b.addEventListener('click',()=>act(b.dataset.target,b.dataset.state)));state();
</script>
</body>
</html>
)HTML";

bool wifiCredentialsConfigured() {
  return ProjectConfig::WIFI_SSID[0] != '\0' &&
         strcmp(ProjectConfig::WIFI_SSID, "YOUR_WIFI_SSID") != 0;
}

String activeIpAddress() {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP().toString();
  }
  if (fallbackApRunning) {
    return WiFi.softAPIP().toString();
  }
  return F("0.0.0.0");
}

String networkDescription() {
  if (WiFi.status() == WL_CONNECTED) {
    return fallbackApRunning ? F("Wi-Fi + 应急热点") : F("Wi-Fi 已连接");
  }
  return fallbackApRunning ? String(F("热点 ")) + fallbackApName
                           : F("Wi-Fi 未连接");
}

String buildStateJson() {
  String json;
  json.reserve(160);
  json += F("{\"light1\":");
  json += lightKnown[0] ? (lightOn[0] ? F("true") : F("false")) : F("null");
  json += F(",\"light2\":");
  json += lightKnown[1] ? (lightOn[1] ? F("true") : F("false")) : F("null");
  json += F(",\"busy\":");
  json += actionBusy ? F("true") : F("false");
  json += F(",\"network\":\"");
  json += networkDescription();
  json += F("\",\"ip\":\"");
  json += activeIpAddress();
  json += F("\"}");
  return json;
}

void sendJsonState(int statusCode = 200) {
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.send(statusCode, F("application/json; charset=utf-8"), buildStateJson());
}

void sendJsonError(int statusCode, const __FlashStringHelper* error) {
  String json(F("{\"error\":\""));
  json += error;
  json += F("\"}");
  server.send(statusCode, F("application/json; charset=utf-8"), json);
}

void attachAtNeutral(uint8_t index) {
  servos[index].attach(SERVO_CONFIG[index].pin,
                       ProjectConfig::SERVO_MIN_PULSE_US,
                       ProjectConfig::SERVO_MAX_PULSE_US);
  servos[index].write(SERVO_CONFIG[index].neutralAngle);
}

bool operateServos(uint8_t target, bool turnOn) {
  if (actionBusy || target < 1 || target > 3) {
    return false;
  }

  actionBusy = true;
  const bool moveFirst = target == 1 || target == 3;
  const bool moveSecond = target == 2 || target == 3;

  if (moveFirst) attachAtNeutral(0);
  if (moveSecond) attachAtNeutral(1);
  delay(ProjectConfig::SERVO_SETTLE_MS);

  // 两路目标角连续写入，时间差只有微秒量级，可视为同时动作。
  if (moveFirst) {
    servos[0].write(turnOn ? SERVO_CONFIG[0].onAngle
                           : SERVO_CONFIG[0].offAngle);
  }
  if (moveSecond) {
    servos[1].write(turnOn ? SERVO_CONFIG[1].onAngle
                            : SERVO_CONFIG[1].offAngle);
  }
  delay(ProjectConfig::SERVO_PRESS_MS);

  if (moveFirst) servos[0].write(SERVO_CONFIG[0].neutralAngle);
  if (moveSecond) servos[1].write(SERVO_CONFIG[1].neutralAngle);
  delay(ProjectConfig::SERVO_RETURN_MS);

  if (moveFirst) {
    servos[0].detach();
    lightKnown[0] = true;
    lightOn[0] = turnOn;
  }
  if (moveSecond) {
    servos[1].detach();
    lightKnown[1] = true;
    lightOn[1] = turnOn;
  }

  actionBusy = false;
  Serial1.printf("[SERVO] target=%u, state=%s\n", target,
                 turnOn ? "ON" : "OFF");
  return true;
}

VoiceProtocol::Result executeVoiceFrame(const VoiceProtocol::Frame& frame) {
  if (frame.parameter != VoiceProtocol::OFF &&
      frame.parameter != VoiceProtocol::ON) {
    return VoiceProtocol::INVALID_PARAMETER;
  }
  if (frame.command < VoiceProtocol::LIGHT_1 ||
      frame.command > VoiceProtocol::ALL_LIGHTS) {
    return VoiceProtocol::INVALID_COMMAND;
  }

  operateServos(frame.command, frame.parameter == VoiceProtocol::ON);
  return VoiceProtocol::OK;
}

void sendVoiceResponse(uint8_t command, VoiceProtocol::Result result) {
  const uint8_t response[5] = {
      VoiceProtocol::RESPONSE_HEADER_1,
      VoiceProtocol::RESPONSE_HEADER_2,
      command,
      static_cast<uint8_t>(result),
      VoiceProtocol::responseChecksum(command, static_cast<uint8_t>(result)),
  };
  voiceSerial.write(response, sizeof(response));
}

void pollVoiceSerial() {
  while (voiceSerial.available() > 0) {
    const uint8_t value = static_cast<uint8_t>(voiceSerial.read());
    VoiceProtocol::Frame frame{};
    if (voiceParser.push(value, millis(), frame)) {
      Serial1.printf("[VOICE] command=0x%02X, parameter=0x%02X\n",
                     frame.command, frame.parameter);
      const VoiceProtocol::Result result = executeVoiceFrame(frame);
      sendVoiceResponse(frame.command, result);
    }
  }
}

void handleRoot() {
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.send_P(200, PSTR("text/html; charset=utf-8"), INDEX_HTML);
}

void handleAction() {
  if (!server.hasArg(F("target")) || !server.hasArg(F("state"))) {
    sendJsonError(400, F("缺少 target 或 state 参数"));
    return;
  }

  const String targetText = server.arg(F("target"));
  const String stateText = server.arg(F("state"));
  uint8_t target = 0;
  if (targetText == F("1")) target = 1;
  if (targetText == F("2")) target = 2;
  if (targetText == F("all")) target = 3;
  if (target == 0 || (stateText != F("on") && stateText != F("off"))) {
    sendJsonError(400, F("控制参数无效"));
    return;
  }

  if (!operateServos(target, stateText == F("on"))) {
    sendJsonError(409, F("舵机正在执行其他动作"));
    return;
  }
  sendJsonState();
}

void configureWebServer() {
  server.on(F("/"), HTTP_GET, handleRoot);
  server.on(F("/api/state"), HTTP_GET, []() { sendJsonState(); });
  server.on(F("/api/action"), HTTP_POST, handleAction);

  // 常见系统的联网探测地址，连接应急热点后可直接弹出控制页。
  server.on(F("/generate_204"), HTTP_GET, handleRoot);
  server.on(F("/hotspot-detect.html"), HTTP_GET, handleRoot);
  server.onNotFound([]() {
    if (server.uri().startsWith(F("/api/"))) {
      sendJsonError(404, F("接口不存在"));
    } else {
      handleRoot();
    }
  });
  server.begin();
  Serial1.println(F("[HTTP] server started on port 80"));
}

void startFallbackAp() {
  if (fallbackApRunning) return;

  snprintf(fallbackApName, sizeof(fallbackApName), "%s-%06X",
           ProjectConfig::AP_NAME_PREFIX, ESP.getChipId());
  WiFi.mode(wifiCredentialsConfigured() ? WIFI_AP_STA : WIFI_AP);
  fallbackApRunning = WiFi.softAP(fallbackApName, ProjectConfig::AP_PASSWORD);
  if (fallbackApRunning) {
    dnsServer.start(53, "*", WiFi.softAPIP());
    Serial1.printf("[WiFi] AP: %s, password: %s, IP: %s\n", fallbackApName,
                   ProjectConfig::AP_PASSWORD,
                   WiFi.softAPIP().toString().c_str());
  } else {
    Serial1.println(F("[WiFi] failed to start fallback AP"));
  }
}

void configureWifi() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.hostname(ProjectConfig::MDNS_HOSTNAME);

  if (!wifiCredentialsConfigured()) {
    Serial1.println(F("[WiFi] credentials not configured; starting AP"));
    startFallbackAp();
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ProjectConfig::WIFI_SSID, ProjectConfig::WIFI_PASSWORD);
  Serial1.printf("[WiFi] connecting to %s", ProjectConfig::WIFI_SSID);
  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         static_cast<uint32_t>(millis() - startedAt) <
             ProjectConfig::WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial1.print('.');
  }
  Serial1.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial1.printf("[WiFi] connected, IP: %s\n",
                   WiFi.localIP().toString().c_str());
  } else {
    Serial1.println(F("[WiFi] connection timeout; starting fallback AP"));
    disconnectedSinceMs = millis();
    startFallbackAp();
  }
}

void configureMdns() {
  mdnsRunning = MDNS.begin(ProjectConfig::MDNS_HOSTNAME);
  if (mdnsRunning) {
    MDNS.addService("http", "tcp", 80);
    Serial1.printf("[mDNS] http://%s.local/\n", ProjectConfig::MDNS_HOSTNAME);
  }
}

void maintainWifi() {
  const uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    disconnectedSinceMs = 0;
    return;
  }
  if (!wifiCredentialsConfigured()) return;

  if (disconnectedSinceMs == 0) disconnectedSinceMs = now;
  if (static_cast<uint32_t>(now - lastWifiRetryMs) >=
      ProjectConfig::WIFI_RETRY_INTERVAL_MS) {
    lastWifiRetryMs = now;
    WiFi.reconnect();
  }
  if (!fallbackApRunning &&
      static_cast<uint32_t>(now - disconnectedSinceMs) >=
          ProjectConfig::WIFI_AP_FALLBACK_MS) {
    startFallbackAp();
  }
}

}  // namespace

void setup() {
  // UART0 使用原理图的 RX(GPIO3)/TX(GPIO1) 与 SU-03T 通信。
  voiceSerial.begin(ProjectConfig::VOICE_BAUD_RATE);
  // UART1 仅有 TX，诊断输出位于 D4/GPIO2，不会混入语音协议。
  Serial1.begin(115200);
  Serial1.println();
  Serial1.println(F("ESP8266 dual-servo light controller"));

  configureWifi();
  configureMdns();
  configureWebServer();

  Serial1.println(F("[VOICE] waiting for AA 55 CMD PARAM XOR frames"));
}

void loop() {
  pollVoiceSerial();
  server.handleClient();
  if (fallbackApRunning) dnsServer.processNextRequest();
  if (mdnsRunning) MDNS.update();
  maintainWifi();
  yield();
}
