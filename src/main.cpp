/**
 * ============================================================
 *  HANHGRIN - ESP8266 Pomodoro Desk Clock + Mini Game Console
 *  -----------------------------------------------------------
 *  Author  : Hanh Grin
 *  License : MIT
 *  Board   : ESP8266 NodeMCU V2 (ESP-12E)
 *  Build   : Visual Studio Code + PlatformIO
 *  Repo    : esp8266-pomodoro-clock
 * ============================================================
 *  Nối dây nhanh:
 *    OLED SH1106 (I2C 0x3C) : SCL->D1, SDA->D2, VCC->3V3, GND->GND
 *    RTC DS3231             : chung bus I2C với OLED
 *    DHT22                  : DATA -> D6 (GPIO12)
 *    Nút TRAI / CHON / PHAI : D3 (GPIO0) / D5 (GPIO14) / D7 (GPIO13)
 *                             (chân còn lại cua nut noi GND)
 * ============================================================
 */
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <SH1106Wire.h>
#include <RTClib.h>
#include <DHT.h>
#include <EEPROM.h>

#define DHTPIN 12
#define BTN_LEFT  0
#define BTN_RIGHT 13
#define BTN_SEL   14

#define FW_VER "TimEm"
#define MAGIC 0xC501
// ⚠️ ĐỔI WIFI CỦA BẠN Ở ĐÂY — Change your WiFi AP here!
// AP name: avoid Vietnamese diacritics; password min 8 chars.
#define AP_SSID "TEST PROMO"
#define AP_PASS "111222333"

// ===== MÀN HÌNH CÓ HỖ TRỢ XOAY 90° =====
class RotSH1106 : public SH1106Wire {
public:
  RotSH1106(uint8_t addr, int sda, int scl) : SH1106Wire(addr, sda, scl) {}

  // rot: 0=ngang thường, 1=ngang lật 180°, 2=dọc 90°CW, 3=dọc 90°CCW
  void displayRot(uint8_t rot) {
    if (rot < 2) { display(); return; }

    static uint8_t tmp[128 * 8];
    memcpy(tmp, buffer, sizeof(tmp));
    memset(buffer, 0, sizeof(tmp));

    for (int x = 0; x < 128; x++) {
      for (int y = 0; y < 64; y++) {
        if (!((tmp[x + (y >> 3) * 128] >> (y & 7)) & 1)) continue;
        int nx, ny;
        if (rot == 2) { nx = 63 - y; ny = x; }
        else          { nx = y;      ny = 127 - x; }
        if (nx < 0 || nx >= 128 || ny < 0 || ny >= 64) continue;
        buffer[nx + (ny >> 3) * 128] |= (1 << (ny & 7));
      }
    }

    display();
    memcpy(buffer, tmp, sizeof(tmp));
  }
};

RotSH1106 disp(0x3c, 4, 5);
RTC_DS3231 rtc;
DHT dht(DHTPIN, DHT22);
ESP8266WebServer server(80);
DNSServer dnsServer;

struct Config { uint8_t focusMin, breakMin, dimHour; bool autoDim; uint8_t rotation; };
struct Save { uint16_t magic; Config cfg; uint16_t cycles; uint16_t weekly[7]; };
Save sv;

enum Screen { SCR_MAIN, SCR_MODE, SCR_GAMES, SCR_PLAYING };
Screen screen = SCR_MAIN;

uint8_t cursorPos = 0;
uint8_t gameIdx = 0;
uint8_t currentMode = 0;
uint8_t currentGame = 0;
const uint8_t MAIN_COUNT = 9;
const uint8_t GAMES_COUNT = 6;

bool booting = true;
bool pomoOn = false, pomoPhase = false;
unsigned long pomoEnd = 0, blinkUntil = 0, nyanUntil = 0;
bool nyanRunning = false;
uint8_t qtMin = 10; bool qtOn = false; unsigned long qtEnd = 0;
float tempC = 0, hum = 0, tMin = 99, tMax = -99;
bool dhtOK = false, rtcOK = false;
float hist[24]; uint8_t histIdx = 0; bool histSeed = false;
unsigned long lastDht = 0, lastHist = 0, lastDraw = 0, lastBtn = 0, bootStart = 0;
unsigned long gameStartTime = 0;
unsigned long bothHoldStart = 0;
uint8_t setIdx = 0;
uint32_t btnTotal = 0;
bool inverted = false;
bool dispFlipped = false;

// ===== POWER SAVE =====
unsigned long lastActivity = 0;
bool powerSave = false;
const unsigned long IDLE_TIMEOUT_MS = 30000;

// SHOOT
int shipX = 60;
int bulX[3], bulY[3]; bool bulOn[3];
int enX[5], enY[5]; bool enOn[5];
uint16_t gameScore = 0; bool gameOver = false;
unsigned long lastShot = 0, lastSpawn = 0, lastStep = 0;

// TETRIS
#define TW 10
#define TH 16
#define TS 3
uint16_t tetRows[TH];
uint8_t tetPiece, tetNext, tetRot;
int8_t tetPX, tetPY;
bool tetOver;
uint16_t tetScore, tetLines;
unsigned long tetDropTime;

const int8_t PIECES[7][4][8] = {
  {{0,0,1,0,2,0,3,0},{0,0,0,1,0,2,0,3},{0,0,1,0,2,0,3,0},{0,0,0,1,0,2,0,3}},
  {{0,0,1,0,0,1,1,1},{0,0,1,0,0,1,1,1},{0,0,1,0,0,1,1,1},{0,0,1,0,0,1,1,1}},
  {{1,0,0,1,1,1,2,1},{0,0,0,1,1,1,0,2},{0,0,1,0,2,0,1,1},{1,0,0,1,1,1,1,2}},
  {{1,0,2,0,0,1,1,1},{0,0,0,1,1,1,1,2},{1,0,2,0,0,1,1,1},{0,0,0,1,1,1,1,2}},
  {{0,0,1,0,1,1,2,1},{1,0,0,1,1,1,0,2},{0,0,1,0,1,1,2,1},{1,0,0,1,1,1,0,2}},
  {{0,0,1,0,2,0,2,1},{0,0,0,1,0,2,1,2},{0,0,0,1,1,1,2,1},{0,0,1,0,1,1,1,2}},
  {{0,0,1,0,2,0,0,1},{0,0,1,0,1,1,1,2},{0,1,1,1,2,1,2,0},{0,0,0,1,0,2,1,0}}
};

// FLAPPY
float flappyY, flappyV;
uint8_t flappyPipes[3];
uint8_t flappyGapY[3];
bool flappyPassed[3];
uint16_t flappyScore;
bool flappyOver;
unsigned long flappyLast;
unsigned long flappyFlapTime = 0;

// CAT
uint8_t catHunger = 100;
uint8_t catHappy = 100;
unsigned long catLastDecay = 0;
unsigned long catAnimTime = 0;
uint8_t catAnimType = 0;
int8_t catAnimX = 64;
uint8_t catAnimY = 20;
bool catBlink = false;
unsigned long catBlinkTime = 0;
uint16_t catScore = 0;

// SNAKE
#define SNAKE_MAX 40
struct SnakePt { int8_t x, y; };
SnakePt snake[SNAKE_MAX];
uint8_t snakeLen, snakeDir;
int8_t snakeFoodX, snakeFoodY;
bool snakeOver, snakeEatFlash;
uint16_t snakeScore;
unsigned long snakeLast;

const char* quotes[] = {"KEEP GOING!", "UONG NUOC DI BAN", "THANG 1 VONG ROI!", "FOCUS!", "CHAM MA CHAC", "NGHI XUNG DANG"};
uint8_t qIdx = 0;
const char* modeNames[8] = {"GIO", "PHONG", "POMO", "HEN GIO", "STATS", "CAI DAT", "HE THONG", "THO 4-4"};
const char* gameNames[6] = {"SHOOT", "NYAN", "TETRIS", "FLAPPY", "CAT", "SNAKE"};

void gameInit();
void tetInit();
void flappyInit();
void catInit();
void snakeInit();

void loadSave() {
  EEPROM.get(0, sv);
  if (sv.magic != MAGIC) {
    sv.magic = MAGIC;
    sv.cfg = {25, 5, 22, true, 1};
    sv.cycles = 0;
    for (uint8_t i = 0; i < 7; i++) sv.weekly[i] = 0;
  }
}
void saveAll() { EEPROM.put(0, sv); EEPROM.commit(); }

struct Btn { uint8_t pin; bool down; bool longFired; uint32_t tPress, tRelease; };
Btn bLeft  = {BTN_LEFT,  false, false, 0, 0};
Btn bRight = {BTN_RIGHT, false, false, 0, 0};
Btn bSel   = {BTN_SEL,   false, false, 0, 0};

#define EV_NONE 0
#define EV_SHORT 1
#define EV_LONG 2

bool isPressed(const Btn& b) { return digitalRead(b.pin) == LOW; }
uint8_t pollBtn(Btn& b) {
  bool now = isPressed(b);
  uint8_t evt = EV_NONE;
  if (now && !b.down) {
    if (millis() - b.tRelease > 50) {
      b.down = true; b.longFired = false; b.tPress = millis();
      btnTotal++; lastBtn = millis(); evt = EV_SHORT;
    }
  } else if (now && b.down) {
    if (!b.longFired && millis() - b.tPress >= 600) { b.longFired = true; evt = EV_LONG; }
  } else if (!now && b.down) {
    b.down = false; b.tRelease = millis();
  }
  return evt;
}
bool isNight() { DateTime n = rtc.now(); return sv.cfg.autoDim && (n.hour() >= sv.cfg.dimHour || n.hour() < 5); }
void setInv(bool on) {
  if (on && !inverted) { disp.invertDisplay(); inverted = true; }
  else if (!on && inverted) { disp.normalDisplay(); inverted = false; }
}

uint8_t lastRot = 255;
void setRotation(uint8_t rot) {
  if (rot > 3) rot = 0;
  bool wantHWFlip = (rot == 1 || rot == 3);
  if (wantHWFlip != dispFlipped) {
    disp.flipScreenVertically();
    dispFlipped = wantHWFlip;
  }
  lastRot = rot;
  sv.cfg.rotation = rot;
}

// ===== POWER MANAGEMENT =====
void touchActivity() {
  lastActivity = millis();
  if (powerSave) {
    powerSave = false;
    disp.setContrast(0xFF);          // độ sáng gốc
    Wire.setClock(400000);           // I2C nhanh lại
    lastDht = 0;                     // đọc DHT ngay
    lastDraw = 0;                    // vẽ lại ngay
    Serial.println("[PWR] Exit power save");
  }
}

void enterPowerSave() {
  if (powerSave) return;
  powerSave = true;
  screen = SCR_MODE;
  currentMode = 0;
  cursorPos = 0;
  nyanRunning = false;
  blinkUntil = 0;
  bothHoldStart = 0;
  lastDraw = 0;
  disp.setContrast(0x30);            // giảm sáng OLED (~20%)
  Wire.setClock(100000);             // hạ tốc I2C
  Serial.println("[PWR] Enter power save");
}

// ==================== WEB UI ====================
const char HTML_MAIN[] PROGMEM = R"=====(
<!DOCTYPE html><html><head><meta charset="utf-8"><meta name=viewport content="width=device-width,initial-scale=1,maximum-scale=1">
<meta name="theme-color" content="#0f1a13">
<title>Hanhgrin</title>
<style>
  :root{
    --bg:#0f1a13;--panel:#16241b;--panel-2:#1b2c21;--border:#24382a;
    --text:#e8f0ea;--muted:#7a9182;--accent:#4ade80;--accent-dim:#22c55e;
    --danger:#f87171;--warn:#fbbf24;
  }
  *{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
  html,body{background:var(--bg)}
  body{
    font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,"Helvetica Neue",sans-serif;
    color:var(--text);padding:20px 16px 40px;max-width:520px;margin:0 auto;line-height:1.4;
  }
  header{display:flex;align-items:baseline;justify-content:space-between;margin-bottom:24px;padding-bottom:16px;border-bottom:1px solid var(--border)}
  header .brand{font-size:20px;font-weight:600;letter-spacing:-0.02em}
  header .brand span{color:var(--accent)}
  header .status{font-size:12px;color:var(--muted);display:flex;align-items:center;gap:6px}
  header .dot{width:6px;height:6px;border-radius:50%;background:var(--accent);box-shadow:0 0 8px var(--accent);animation:pulse 2s ease-in-out infinite}
  @keyframes pulse{0%,100%{opacity:1}50%{opacity:.5}}

  .section{background:var(--panel);border:1px solid var(--border);border-radius:14px;padding:18px;margin-bottom:14px}
  .section h2{font-size:11px;font-weight:600;letter-spacing:0.1em;text-transform:uppercase;color:var(--muted);margin-bottom:14px}

  .clock{font-size:52px;font-weight:200;letter-spacing:-0.04em;font-variant-numeric:tabular-nums;color:var(--text);line-height:1}
  .date{font-size:13px;color:var(--muted);margin-top:6px}
  .metrics{display:grid;grid-template-columns:1fr 1fr 1fr;gap:12px;margin-top:18px;padding-top:16px;border-top:1px solid var(--border)}
  .metric .label{font-size:11px;color:var(--muted);margin-bottom:4px}
  .metric .value{font-size:16px;font-weight:500;font-variant-numeric:tabular-nums}
  .metric .value.green{color:var(--accent)}

  .pomo-row{display:flex;justify-content:space-between;align-items:center;padding:10px 0;border-bottom:1px solid var(--border)}
  .pomo-row:last-of-type{border-bottom:0}
  .pomo-row .k{font-size:13px;color:var(--muted)}
  .pomo-row .v{font-size:15px;font-weight:500;font-variant-numeric:tabular-nums}
  .pomo-time{font-size:26px;font-weight:300;color:var(--accent);font-variant-numeric:tabular-nums}

  .btn-row{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-top:14px}
  .btn-row.two{grid-template-columns:1fr 1fr}
  .btn-row.six{grid-template-columns:repeat(3,1fr)}

  button{
    background:var(--panel-2);border:1px solid var(--border);color:var(--text);
    font-family:inherit;font-size:13px;font-weight:500;padding:11px 8px;
    border-radius:10px;cursor:pointer;transition:all .15s ease;
    display:flex;align-items:center;justify-content:center;gap:6px;min-height:42px;
  }
  button:hover{border-color:var(--accent-dim);background:#1e3226}
  button:active{transform:scale(.97)}
  button.primary{background:var(--accent-dim);border-color:var(--accent);color:#0a1610;font-weight:600}
  button.primary:hover{background:var(--accent)}
  button.danger{color:var(--danger);border-color:#3a1f1f}
  button.danger:hover{border-color:var(--danger);background:#2a1616}
  button.warn{color:var(--warn)}
  button.warn:hover{border-color:var(--warn)}
  button.on{background:var(--accent-dim);border-color:var(--accent);color:#0a1610}

  .slider-group{margin-top:4px}
  .slider-group + .slider-group{margin-top:16px}
  .slider-head{display:flex;justify-content:space-between;font-size:13px;margin-bottom:8px}
  .slider-head .val{color:var(--accent);font-weight:600;font-variant-numeric:tabular-nums}
  input[type=range]{-webkit-appearance:none;width:100%;height:4px;background:var(--border);border-radius:2px;outline:none}
  input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:18px;height:18px;border-radius:50%;background:var(--accent);cursor:pointer;box-shadow:0 0 0 4px #4ade8020;transition:box-shadow .15s}
  input[type=range]::-webkit-slider-thumb:hover{box-shadow:0 0 0 6px #4ade8030}
  input[type=range]::-moz-range-thumb{width:18px;height:18px;border-radius:50%;background:var(--accent);border:0;cursor:pointer}

  .toggle{display:flex;align-items:center;justify-content:space-between;padding:10px 0}
  .toggle-label{font-size:13px}
  .switch{position:relative;width:42px;height:24px;background:var(--border);border-radius:12px;cursor:pointer;transition:background .2s;flex-shrink:0}
  .switch.on{background:var(--accent-dim)}
  .switch::after{content:'';position:absolute;left:3px;top:3px;width:18px;height:18px;background:#fff;border-radius:50%;transition:transform .2s}
  .switch.on::after{transform:translateX(18px)}

  .pad-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-top:6px}
  .pad{background:var(--panel-2);border:1px solid var(--border);border-radius:10px;padding:12px 8px;text-align:center}
  .pad .pad-name{font-size:10px;color:var(--muted);letter-spacing:0.05em;margin-bottom:6px}
  .pad .pad-led{font-size:18px;color:#2a3d2e;line-height:1;transition:color .1s}
  .pad.active{border-color:var(--accent);background:#1e3226}
  .pad.active .pad-led{color:var(--accent)}

  footer{text-align:center;font-size:11px;color:var(--muted);margin-top:20px;padding-top:16px;border-top:1px solid var(--border);letter-spacing:0.05em}
  footer code{background:var(--panel);padding:2px 6px;border-radius:4px;color:var(--accent);font-size:10px}
</style></head>
<body>

<header>
  <div class="brand">Hanh<span>grin</span></div>
  <div class="status"><span class="dot"></span><span id="net">kết nối</span></div>
</header>

<div class="section">
  <div class="clock" id="clk">--:--</div>
  <div class="date" id="dt">Đang tải…</div>
  <div class="metrics">
    <div class="metric"><div class="label">Nhiệt độ</div><div class="value green" id="tp">--°</div></div>
    <div class="metric"><div class="label">Độ ẩm</div><div class="value" id="hm">--%</div></div>
    <div class="metric"><div class="label">RAM</div><div class="value" id="ram">--</div></div>
  </div>
</div>

<div class="section">
  <h2>Đồng hồ RTC</h2>
  <div class="pomo-row"><span class="k">Giờ trên máy</span><span class="v" id="rtcNow">--:--:--</span></div>
  <div class="pomo-row"><span class="k">Giờ điện thoại</span><span class="v" id="phNow">--:--:--</span></div>
  <div class="btn-row two" style="margin-top:12px">
    <button class="primary" onclick="syncTime(this)">Đồng bộ giờ</button>
    <button onclick="tick()">Làm mới</button>
  </div>
</div>

<div class="section">
  <h2>Pomodoro</h2>
  <div class="pomo-row"><span class="k">Trạng thái</span><span class="v" id="pm">—</span></div>
  <div class="pomo-row"><span class="k">Còn lại</span><span class="pomo-time" id="rem">--:--</span></div>
  <div class="pomo-row"><span class="k">Hôm nay</span><span class="v" id="cy">0 chu kỳ</span></div>
  <div class="btn-row">
    <button class="primary" onclick="send('pomo=start'); setMode(2)">Chạy</button>
    <button class="warn"    onclick="send('pomo=stop')">Dừng</button>
    <button class="danger"  onclick="send('pomo=reset'); setMode(2)">Đặt lại</button>
  </div>
</div>

<div class="section">
  <h2>Hẹn giờ</h2>
  <div class="pomo-row"><span class="k">Trạng thái</span><span class="v" id="qts">—</span></div>
  <div class="pomo-row"><span class="k">Còn lại</span><span class="pomo-time" id="qtr">--:--</span></div>
  <div class="slider-group" style="margin-top:12px">
    <div class="slider-head"><span>Số phút</span><span class="val" id="qtv">10</span></div>
    <input type="range" min="1" max="60" step="1" value="10" id="sqm" oninput="onQtSlider()">
  </div>
  <div class="btn-row">
    <button class="primary" onclick="send('qt=start&min='+$('sqm').value); setMode(3)">Chạy</button>
    <button class="warn" onclick="send('qt=stop')">Dừng</button>
    <button class="danger" onclick="send('qt=reset')">Đặt lại</button>
  </div>
</div>

<div class="section">
  <h2>Chuyển màn hình</h2>
  <div class="btn-row six">
    <button data-m="0" onclick="setMode(0)">Đồng hồ</button>
    <button data-m="1" onclick="setMode(1)">Nhiệt độ</button>
    <button data-m="2" onclick="setMode(2)">Pomodoro</button>
    <button data-m="3" onclick="setMode(3)">Hẹn giờ</button>
    <button data-m="4" onclick="setMode(4)">Thống kê</button>
    <button data-m="7" onclick="setMode(7)">Thở 4-4</button>
  </div>
</div>

<div class="section">
  <h2>Trò chơi</h2>
  <div class="btn-row six">
    <button data-g="0" onclick="setGame(0)">Bắn</button>
    <button data-g="2" onclick="setGame(2)">Xếp</button>
    <button data-g="3" onclick="setGame(3)">Bay</button>
    <button data-g="4" onclick="setGame(4)">Mèo</button>
    <button data-g="5" onclick="setGame(5)">Rắn</button>
    <button data-g="1" onclick="setGame(1)">Nyan</button>
  </div>
  <div class="btn-row two" style="margin-top:8px">
    <button class="primary" id="nyanBtn" onclick="toggleNyan()">Bật mèo Nyan</button>
    <button onclick="back()">Về menu</button>
  </div>
</div>

<div class="section">
  <h2>Hướng màn hình</h2>
  <div class="btn-row" style="grid-template-columns:repeat(2,1fr)">
    <button data-r="0" onclick="setRot(0)">Ngang</button>
    <button data-r="1" onclick="setRot(1)">Ngang 180°</button>
    <button data-r="2" onclick="setRot(2)">Dọc 90°↻</button>
    <button data-r="3" onclick="setRot(3)">Dọc 90°↺</button>
  </div>
  <div class="toggle" style="margin-top:12px">
    <span class="toggle-label">Tự tối ban đêm</span>
    <div class="switch" id="ads" onclick="toggleDim()"></div>
  </div>
</div>

<div class="section">
  <h2>Cài đặt</h2>
  <div class="slider-group">
    <div class="slider-head"><span>Phút tập trung</span><span class="val" id="vf">25</span></div>
    <input type="range" min="5" max="90" step="5" id="sf" oninput="onSlider()">
  </div>
  <div class="slider-group">
    <div class="slider-head"><span>Phút nghỉ</span><span class="val" id="vb">5</span></div>
    <input type="range" min="1" max="30" id="sb" oninput="onSlider()">
  </div>
  <div class="slider-group">
    <div class="slider-head"><span>Giờ tối đi</span><span class="val" id="vd">22h</span></div>
    <input type="range" min="18" max="23" id="sd" oninput="onSlider()">
  </div>
  <button class="danger" style="width:100%;margin-top:14px" onclick="clearStats()">Xóa thống kê</button>
</div>

<div class="section">
  <h2>Nút cứng</h2>
  <div class="pad-grid">
    <div class="pad" id="pL"><div class="pad-name">TRÁI · D3</div><div class="pad-led">●</div></div>
    <div class="pad" id="pS"><div class="pad-name">CHỌN · D5</div><div class="pad-led">●</div></div>
    <div class="pad" id="pR"><div class="pad-name">PHẢI · D7</div><div class="pad-led">●</div></div>
  </div>
  <div class="pomo-row" style="margin-top:12px">
    <span class="k">Tổng số lần bấm</span>
    <span class="v" id="btn">0</span>
  </div>
</div>

<footer>
  <div style="margin-bottom:4px">Hanhgrin · <code>vitoiyeuem</code></div>
  <div style="opacity:.6">TimEm · made with <span style="color:#f87171">♥</span></div>
</footer>

<script>
  let state = {n:false, m:null, g:null, ad:0, rot:0};
  const $ = id => document.getElementById(id);

  function send(q){
    fetch('/api/set?'+q, {cache:'no-store'}).catch(()=>{});
  }
  function setMode(m){
    state.m = m; state.n = false;
    send('mode='+m);
    paint();
  }
  function setGame(g){
    state.g = g; state.n = (g === 1);
    send('game='+g);
    paint();
  }
  function toggleNyan(){
    state.n = !state.n;
    send('nyan='+(state.n?1:0));
    paint();
  }
  function back(){ send('back=1'); state.n=false; state.m=null; state.g=null; paint(); }
  function clearStats(){ if(confirm('Xóa hết thống kê Pomodoro?')) send('clears=1'); }

  function syncTime(btn){
    const d = new Date();
    const q = 'settime=1'
      + '&y='  + d.getFullYear()
      + '&mo=' + (d.getMonth()+1)
      + '&d='  + d.getDate()
      + '&h='  + d.getHours()
      + '&mi=' + d.getMinutes()
      + '&s='  + d.getSeconds();
    fetch('/api/set?' + q, {cache:'no-store'})
      .then(r => r.text())
      .then(() => {
        if (btn){ btn.textContent = 'Đã đồng bộ ✓'; setTimeout(()=>btn.textContent='Đồng bộ giờ', 1500); }
        setTimeout(tick, 200);
      })
      .catch(()=>{ if (btn){ btn.textContent = 'Lỗi!'; setTimeout(()=>btn.textContent='Đồng bộ giờ', 1500); } });
  }

  let sliderTimer = null;
  function onSlider(){
    $('vf').textContent = $('sf').value;
    $('vb').textContent = $('sb').value;
    $('vd').textContent = $('sd').value + 'h';
    clearTimeout(sliderTimer);
    sliderTimer = setTimeout(() => {
      send('f='+$('sf').value+'&b='+$('sb').value+'&dh='+$('sd').value);
    }, 250);
  }
  function onQtSlider(){
    $('qtv').textContent = $('sqm').value;
  }
  function toggleDim(){
    state.ad = !state.ad;
    $('ads').classList.toggle('on', state.ad);
    send('ad='+(state.ad?1:0));
  }
  function setRot(r){
    state.rot = r;
    send('rot='+r);
    document.querySelectorAll('[data-r]').forEach(b=>{
      b.classList.toggle('on', parseInt(b.dataset.r)===r);
    });
  }
  function paint(){
    $('nyanBtn').textContent = state.n ? 'Tắt mèo Nyan' : 'Bật mèo Nyan';
    $('nyanBtn').classList.toggle('on', state.n);
    document.querySelectorAll('[data-m]').forEach(b=>{
      b.classList.toggle('on', !state.n && parseInt(b.dataset.m)===state.m);
    });
    document.querySelectorAll('[data-g]').forEach(b=>{
      b.classList.toggle('on', parseInt(b.dataset.g)===state.g);
    });
  }

  function pad2(x){ return String(x).padStart(2,'0'); }

  async function tick(){
    try{
      const r = await fetch('/api/state', {cache:'no-store'});
      const j = await r.json();
      $('clk').textContent = j.time.slice(0,5);
      $('dt').textContent = j.dow + ' · ' + j.date;
      $('tp').textContent = j.temp.toFixed(1) + '°';
      $('hm').textContent = j.hum + '%';
      $('ram').textContent = (j.ram/1024|0) + 'k';
      $('pm').textContent = j.pomo ? (j.phase ? 'Nghỉ' : 'Tập trung') : 'Đang tắt';
      $('rem').textContent = j.rem;
      $('cy').textContent = j.cycles + ' chu kỳ';

      $('qts').textContent = j.qt_on ? 'Đang chạy' : 'Đang tắt';
      $('qtr').textContent = j.qt_on ? j.qt_rem : '00:00';

      $('pL').classList.toggle('active', !!j.bl);
      $('pS').classList.toggle('active', !!j.bs);
      $('pR').classList.toggle('active', !!j.br);
      $('btn').textContent = j.btn;

      $('net').textContent = 'ổn định';

      $('rtcNow').textContent = j.time;
      const now = new Date();
      $('phNow').textContent = pad2(now.getHours())+':'+pad2(now.getMinutes())+':'+pad2(now.getSeconds());

      const newN = (j.scr === 3 && j.game === 1);
      const newM = (j.scr === 1) ? j.mode : null;
      const newG = (j.scr === 3) ? j.game : null;
      if (newN !== state.n || newM !== state.m || newG !== state.g) {
        state.n = newN; state.m = newM; state.g = newG;
        paint();
      }

      if(!window._init){
        window._init = 1;
        $('sf').value = j.focus; $('sb').value = j.brk; $('sd').value = j.dh;
        $('vf').textContent = j.focus; $('vb').textContent = j.brk; $('vd').textContent = j.dh + 'h';
        $('sqm').value = j.qt_min; $('qtv').textContent = j.qt_min;
        state.ad = j.ad === 1;
        $('ads').classList.toggle('on', state.ad);
        state.rot = j.rot;
        document.querySelectorAll('[data-r]').forEach(b=>{
          b.classList.toggle('on', parseInt(b.dataset.r)===j.rot);
        });
      }
    }catch(e){
      $('net').textContent = 'mất kết nối';
    }
  }
  tick();
  setInterval(tick, 1200);
  paint();
</script>
</body></html>
)=====";

// ==================== WEB HANDLERS ====================
void handleState() {
  DateTime n = rtc.now();
  const char* dow[] = {"Chủ nhật","Thứ hai","Thứ ba","Thứ tư","Thứ năm","Thứ sáu","Thứ bảy"};
  unsigned long rem = pomoOn ? ((pomoEnd > millis()) ? (pomoEnd - millis()) / 1000 : 0) : sv.cfg.focusMin * 60UL;
  unsigned long qtRem = qtOn ? ((qtEnd > millis()) ? (qtEnd - millis()) / 1000 : 0) : 0;
  char b[600];
  snprintf(b, sizeof(b),
    "{\"time\":\"%02d:%02d:%02d\",\"date\":\"%02d/%02d/%04d\",\"dow\":\"%s\","
    "\"temp\":%.1f,\"hum\":%d,\"ram\":%u,"
    "\"pomo\":%d,\"phase\":%d,\"rem\":\"%02lu:%02lu\",\"cycles\":%u,"
    "\"focus\":%u,\"brk\":%u,\"dh\":%u,\"ad\":%d,\"rot\":%u,"
    "\"nyan\":%d,\"scr\":%d,\"mode\":%d,\"game\":%d,"
    "\"qt_on\":%d,\"qt_min\":%u,\"qt_rem\":\"%02lu:%02lu\","
    "\"bl\":%d,\"br\":%d,\"bs\":%d,\"btn\":%u}",
    n.hour(), n.minute(), n.second(),
    n.day(), n.month(), n.year(), dow[n.dayOfTheWeek()],
    dhtOK ? tempC : 0.0, dhtOK ? (int)hum : 0, (unsigned)ESP.getFreeHeap(),
    pomoOn ? 1 : 0, pomoPhase ? 1 : 0, rem / 60, rem % 60, (unsigned)sv.cycles,
    (unsigned)sv.cfg.focusMin, (unsigned)sv.cfg.breakMin, (unsigned)sv.cfg.dimHour,
    sv.cfg.autoDim ? 1 : 0,
    (unsigned)sv.cfg.rotation,
    nyanRunning ? 1 : 0, (int)screen, (int)currentMode, (int)currentGame,
    qtOn ? 1 : 0, (unsigned)qtMin, qtRem / 60, qtRem % 60,
    isPressed(bLeft) ? 1 : 0, isPressed(bRight) ? 1 : 0, isPressed(bSel) ? 1 : 0,
    (unsigned)btnTotal);
  server.send(200, "application/json", b);
}

void setupWeb() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  dnsServer.start(53, "*", WiFi.softAPIP());

  server.on("/", []() { server.send_P(200, "text/html", HTML_MAIN); });
  server.on("/api/state", handleState);

  server.on("/api/set", []() {
    touchActivity();   // mọi lệnh web đều là activity
    bool needSave = false;

    if (server.hasArg("settime")) {
      int y  = server.arg("y").toInt();
      int mo = server.arg("mo").toInt();
      int d  = server.arg("d").toInt();
      int h  = server.arg("h").toInt();
      int mi = server.arg("mi").toInt();
      int s  = server.arg("s").toInt();
      if (y >= 2024 && y <= 2099 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31
          && h >= 0 && h <= 23 && mi >= 0 && mi <= 59 && s >= 0 && s <= 59) {
        rtc.adjust(DateTime(y, mo, d, h, mi, s));
        Serial.printf("[RTC] Set: %04d-%02d-%02d %02d:%02d:%02d\n", y, mo, d, h, mi, s);
      } else {
        Serial.println("[RTC] Set: tham so khong hop le");
      }
    }

    if (server.hasArg("f"))  { sv.cfg.focusMin = constrain(server.arg("f").toInt(), 5, 90); needSave = true; }
    if (server.hasArg("b"))  { sv.cfg.breakMin = constrain(server.arg("b").toInt(), 1, 30); needSave = true; }
    if (server.hasArg("dh")) { sv.cfg.dimHour  = constrain(server.arg("dh").toInt(), 18, 23); needSave = true; }
    if (server.hasArg("ad")) { sv.cfg.autoDim  = server.arg("ad").toInt() == 1; needSave = true; }

    if (server.hasArg("rot")) {
      uint8_t r = constrain(server.arg("rot").toInt(), 0, 3);
      setRotation(r);
      needSave = true;
    }

    if (server.hasArg("clears")) {
      for (uint8_t i = 0; i < 7; i++) sv.weekly[i] = 0;
      sv.cycles = 0; needSave = true;
    }

    if (server.hasArg("pomo")) {
      String a = server.arg("pomo");
      if (a == "start") {
        if (!pomoOn) {
          pomoOn = true;
          pomoEnd = millis() + (pomoPhase ? sv.cfg.breakMin : sv.cfg.focusMin) * 60000UL;
        }
        screen = SCR_MODE;
        currentMode = 2;
        cursorPos = 2;
        nyanRunning = false;
        blinkUntil = 0;
        lastDraw = 0;
      }
      else if (a == "stop") {
        pomoOn = false;
      }
      else if (a == "reset") {
        pomoOn = false;
        pomoPhase = false;
        blinkUntil = 0;
        screen = SCR_MODE;
        currentMode = 2;
        cursorPos = 2;
        nyanRunning = false;
        lastDraw = 0;
      }
    }

    if (server.hasArg("qt")) {
      String a = server.arg("qt");
      if (a == "start") {
        uint8_t m = server.hasArg("min") ? constrain(server.arg("min").toInt(), 1, 60) : qtMin;
        qtMin = m;
        qtOn = true;
        qtEnd = millis() + (unsigned long)qtMin * 60000UL;
        screen = SCR_MODE; currentMode = 3; cursorPos = 3; nyanRunning = false; lastDraw = 0;
      }
      else if (a == "stop") { qtOn = false; }
      else if (a == "reset") { qtOn = false; }
    }

    if (server.hasArg("mode")) {
      currentMode = constrain(server.arg("mode").toInt(), 0, 7);
      screen = SCR_MODE;
      cursorPos = currentMode;
      nyanRunning = false;
      lastDraw = 0;
    }

    if (server.hasArg("game")) {
      int g = server.arg("game").toInt();
      if (g >= 0 && g < GAMES_COUNT) {
        currentGame = g;
        screen = SCR_PLAYING;
        gameStartTime = millis();
        bothHoldStart = 0;
        nyanRunning = (currentGame == 1);
        if (currentGame == 0) gameInit();
        else if (currentGame == 1) nyanUntil = millis() + 10000;
        else if (currentGame == 2) tetInit();
        else if (currentGame == 3) flappyInit();
        else if (currentGame == 4) catInit();
        else if (currentGame == 5) snakeInit();
        lastDraw = 0;
      }
    }

    if (server.hasArg("nyan")) {
      if (server.arg("nyan").toInt() == 1) {
        currentGame = 1; screen = SCR_PLAYING;
        gameStartTime = millis(); bothHoldStart = 0;
        nyanUntil = millis() + 10000; nyanRunning = true;
        lastDraw = 0;
      } else {
        nyanRunning = false;
        if (screen == SCR_PLAYING && currentGame == 1) { screen = SCR_GAMES; lastDraw = 0; }
      }
    }

    if (server.hasArg("back")) {
      nyanRunning = false;
      screen = SCR_GAMES;
      gameIdx = 0;
      lastDraw = 0;
    }

    if (needSave) saveAll();
    server.send(200, "text/plain", "ok");
  });

  server.on("/api/pomo", []() {
    touchActivity();
    String a = server.arg("a");
    if (a == "start") {
      if (!pomoOn) {
        pomoOn = true;
        pomoEnd = millis() + (pomoPhase ? sv.cfg.breakMin : sv.cfg.focusMin) * 60000UL;
      }
      screen = SCR_MODE; currentMode = 2; cursorPos = 2;
      nyanRunning = false; blinkUntil = 0; lastDraw = 0;
    }
    else if (a == "stop") pomoOn = false;
    else if (a == "reset") {
      pomoOn = false; pomoPhase = false; blinkUntil = 0;
      screen = SCR_MODE; currentMode = 2; cursorPos = 2; lastDraw = 0;
    }
    server.send(200, "text/plain", "ok");
  });
  server.on("/api/mode", []() {
    touchActivity();
    if (server.hasArg("m")) {
      currentMode = constrain(server.arg("m").toInt(), 0, 7);
      screen = SCR_MODE; cursorPos = currentMode;
      nyanRunning = false; lastDraw = 0;
    }
    server.send(200, "text/plain", "ok");
  });
  server.on("/api/fx", []() {
    touchActivity();
    currentGame = 1; screen = SCR_PLAYING;
    gameStartTime = millis(); bothHoldStart = 0;
    nyanUntil = millis() + 10000; nyanRunning = true;
    lastDraw = 0;
    server.send(200, "text/plain", "ok");
  });

  server.onNotFound([]() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });
  server.begin();
}

// ==================== DRAW HELPERS ====================
const uint16_t CAT[10] = {0x0022,0x0077,0x7FFF,0x40FF,0x52ED,0x49F7,0x52FF,0x7FBE,0x3300,0x3300};
void setPx(int x, int y) { if (x >= 0 && x < 128 && y >= 0 && y < 64) disp.setPixel(x, y); }

void drawRainbow(int x0, int x1, int y, uint8_t frame) {
  for (uint8_t s = 0; s < 6; s++) {
    int yy = y + 2 + s;
    if (yy < 0 || yy >= 62) continue;
    for (int x = x0; x < x1; x++) {
      if (x < 0 || x >= 128) continue;
      bool on; uint8_t kind = s % 3;
      if (kind == 0) on = true;
      else if (kind == 1) on = ((x + frame) % 4) < 2;
      else on = ((x + frame) % 2) == 0;
      if (on) setPx(x, yy);
    }
  }
}
void drawNyan(int x, int y, uint8_t frame) {
  if (x > 2) drawRainbow(2, x, y, frame);
  for (uint8_t row = 0; row < 10; row++)
    for (uint8_t col = 0; col < 16; col++)
      if ((CAT[row] >> (15 - col)) & 1) setPx(x + col, y + row);
}

void drawBoot() {
  disp.clear();
  uint32_t el = millis() - bootStart;
  uint8_t frame = (el / 100) % 16;
  disp.setTextAlignment(TEXT_ALIGN_CENTER);
  disp.setFont(ArialMT_Plain_16);
  disp.drawString(64, 0, "HANHGRIN");
  disp.setFont(ArialMT_Plain_10);
  disp.drawString(64, 17, FW_VER);
  disp.setTextAlignment(TEXT_ALIGN_LEFT);
  uint32_t prog = (el > 3000) ? 100 : (uint32_t)(el * 100 / 3000);
  drawNyan(2 + (int)(96L * prog / 100), 30, frame);
  disp.drawRect(14, 43, 100, 9);
  uint8_t fill = (uint8_t)(98L * prog / 100);
  if (fill > 0) disp.fillRect(15, 44, fill, 7);
  disp.setTextAlignment(TEXT_ALIGN_CENTER);
  disp.setFont(ArialMT_Plain_10);
  char b[16];
  if (el > 3000) snprintf(b, sizeof(b), "anhquenminhyeu");
  else snprintf(b, sizeof(b), "%u%%", (unsigned)prog);
  disp.drawString(64, 53, b);
  disp.setTextAlignment(TEXT_ALIGN_LEFT);
  disp.displayRot(sv.cfg.rotation);
  if (el > 3500) booting = false;
}

void draw7(uint8_t x, uint8_t y, uint8_t d) {
  const uint8_t wd = 14, hd = 28, td = 3;
  if (d == 1) x -= 6;
  bool s[7] = {false,false,false,false,false,false,false};
  switch (d) {
    case 0: s[0]=s[1]=s[2]=s[3]=s[4]=s[5]=true; break;
    case 1: s[1]=s[2]=true; break;
    case 2: s[0]=s[1]=s[6]=s[4]=s[3]=true; break;
    case 3: s[0]=s[1]=s[6]=s[2]=s[3]=true; break;
    case 4: s[5]=s[6]=s[1]=s[2]=true; break;
    case 5: s[0]=s[5]=s[6]=s[2]=s[3]=true; break;
    case 6: s[0]=s[5]=s[4]=s[3]=s[2]=s[6]=true; break;
    case 7: s[0]=s[1]=s[2]=true; break;
    case 8: for (uint8_t i=0;i<7;i++) s[i]=true; break;
    case 9: s[0]=s[1]=s[2]=s[3]=s[5]=s[6]=true; break;
  }
  if (s[0]) disp.fillRect(x+2, y, wd-4, td);
  if (s[1]) disp.fillRect(x+wd-td, y+2, td, hd/2-3);
  if (s[2]) disp.fillRect(x+wd-td, y+hd/2+1, td, hd/2-3);
  if (s[3]) disp.fillRect(x+2, y+hd-td, wd-4, td);
  if (s[4]) disp.fillRect(x, y+hd/2+1, td, hd/2-3);
  if (s[5]) disp.fillRect(x, y+2, td, hd/2-3);
  if (s[6]) disp.fillRect(x+2, y+hd/2-td/2, wd-4, td);
}

void drawTime7(uint8_t x, uint8_t y, uint8_t h, uint8_t m, bool colon) {
  disp.setColor(WHITE);
  draw7(x, y, h / 10);
  draw7(x + 16, y, h % 10);
  if (colon) {
    disp.setColor(WHITE);
    disp.fillRect(x + 32, y + 8, 3, 3);
    disp.fillRect(x + 32, y + 18, 3, 3);
  } else {
    disp.setColor(BLACK);
    disp.fillRect(x + 32, y + 8, 3, 3);
    disp.fillRect(x + 32, y + 18, 3, 3);
    disp.setColor(WHITE);
  }
  draw7(x + 38, y, m / 10);
  draw7(x + 54, y, m % 10);
  disp.setColor(WHITE);
}

const char* preview(uint8_t idx) {
  static char b[20];
  switch (idx) {
    case 0: { DateTime n = rtc.now(); snprintf(b, sizeof(b), "%02d:%02d", n.hour(), n.minute()); } break;
    case 1: snprintf(b, sizeof(b), "%.1fC", tempC); break;
    case 2: { unsigned long rem = pomoOn ? ((pomoEnd > millis()) ? (pomoEnd - millis()) / 1000 : 0) : sv.cfg.focusMin * 60UL; snprintf(b, sizeof(b), "%02lu:%02lu", rem / 60, rem % 60); } break;
    case 3: snprintf(b, sizeof(b), "%u phut", (unsigned)qtMin); break;
    case 4: snprintf(b, sizeof(b), "tong %u", (unsigned)sv.cycles); break;
    case 5: snprintf(b, sizeof(b), "%u/%u", (unsigned)sv.cfg.focusMin, (unsigned)sv.cfg.breakMin); break;
    case 6: snprintf(b, sizeof(b), "%ukB", (unsigned)(ESP.getFreeHeap() / 1024)); break;
    case 7: snprintf(b, sizeof(b), "4-4"); break;
    case 8: snprintf(b, sizeof(b), "6 game"); break;
    default: b[0] = 0;
  }
  return b;
}

// ============ MENU CHÍNH ============
void drawMainMenu() {
  disp.clear();
  disp.setColor(WHITE);
  disp.setFont(ArialMT_Plain_10);
  disp.drawString(0, 0, "HANHGRIN");
  disp.setTextAlignment(TEXT_ALIGN_RIGHT);
  disp.drawString(127, 0, "MEO");
  disp.setTextAlignment(TEXT_ALIGN_LEFT);
  disp.drawLine(0, 10, 127, 10);
  uint8_t pageStart = (cursorPos / 4) * 4;
  uint8_t pulse = (millis() / 300) % 2;
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t idx = pageStart + i;
    if (idx >= MAIN_COUNT) continue;
    uint8_t x = (i % 2) * 64;
    uint8_t y = 12 + (i / 2) * 26;
    bool sel = (i == (cursorPos % 4));
    disp.setColor(WHITE);
    disp.drawRect(x + 1, y, 62, 25);
    if (sel && pulse) disp.drawRect(x + 2, y + 1, 60, 23);
    if (sel) { disp.setColor(WHITE); disp.fillRect(x + 2, y + 1, 60, 11); disp.setColor(BLACK); }
    disp.setFont(ArialMT_Plain_10);
    disp.setTextAlignment(TEXT_ALIGN_CENTER);
    if (idx == 8) disp.drawString(x + 32, y + 2, "GAMES");
    else disp.drawString(x + 32, y + 2, modeNames[idx]);
    disp.setColor(WHITE);
    disp.drawString(x + 32, y + 14, preview(idx));
    disp.setTextAlignment(TEXT_ALIGN_LEFT);
  }
  disp.setFont(ArialMT_Plain_10);
  disp.setTextAlignment(TEXT_ALIGN_CENTER);
  uint8_t page = cursorPos / 4 + 1;
  char b[8]; snprintf(b, sizeof(b), "%u/3", (unsigned)page);
  disp.drawString(64, 61, b);
  disp.setTextAlignment(TEXT_ALIGN_LEFT);
  disp.displayRot(sv.cfg.rotation);
}

// ============ MENU GAMES ============
void drawGamesMenu() {
  disp.clear();
  disp.setColor(WHITE);
  disp.setFont(ArialMT_Plain_10);
  disp.drawString(0, 0, "GAMES");
  disp.setTextAlignment(TEXT_ALIGN_RIGHT);
  disp.drawString(127, 0, "yolo");
  disp.setTextAlignment(TEXT_ALIGN_LEFT);
  disp.drawLine(0, 10, 127, 10);
  uint8_t pageStart = (gameIdx / 4) * 4;
  uint8_t pulse = (millis() / 300) % 2;
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t idx = pageStart + i;
    if (idx >= GAMES_COUNT) continue;
    uint8_t x = (i % 2) * 64;
    uint8_t y = 12 + (i / 2) * 26;
    bool sel = (i == (gameIdx % 4));
    disp.setColor(WHITE);
    disp.drawRect(x + 1, y, 62, 25);
    if (sel && pulse) disp.drawRect(x + 2, y + 1, 60, 23);
    if (sel) { disp.setColor(WHITE); disp.fillRect(x + 2, y + 1, 60, 11); disp.setColor(BLACK); }
    disp.setFont(ArialMT_Plain_10);
    disp.setTextAlignment(TEXT_ALIGN_CENTER);
    disp.drawString(x + 32, y + 2, gameNames[idx]);
    const char* pv = "";
    switch (idx) {
      case 0: pv = "ban"; break;
      case 1: pv = "chay"; break;
      case 2: pv = "xep"; break;
      case 3: pv = "bay"; break;
      case 4: pv = "meo"; break;
      case 5: pv = "ran"; break;
    }
    disp.setColor(WHITE);
    disp.drawString(x + 32, y + 14, pv);
    disp.setTextAlignment(TEXT_ALIGN_LEFT);
  }
  disp.setFont(ArialMT_Plain_10);
  disp.setTextAlignment(TEXT_ALIGN_CENTER);
  char b[8];
  uint8_t page = gameIdx / 4 + 1;
  snprintf(b, sizeof(b), "%u/2", (unsigned)page);
  disp.drawString(64, 61, b);
  disp.setTextAlignment(TEXT_ALIGN_LEFT);
  disp.displayRot(sv.cfg.rotation);
}

// ============ CÁC MODE ============
void drawClock() {
  DateTime n = rtc.now();
  char b[24];
  drawTime7(29, 13, n.hour(), n.minute(), (millis() / 500) % 2);
  disp.setColor(WHITE);
  disp.setFont(ArialMT_Plain_10);
  disp.setTextAlignment(TEXT_ALIGN_CENTER);
  snprintf(b, sizeof(b), "%02d/%02d/%04d", n.day(), n.month(), n.year());
  disp.drawString(64, 44, b);
  const char* dow[] = {"CN","T2","T3","T4","T5","T6","T7"};
  snprintf(b, sizeof(b), "%s  :%02d", dow[n.dayOfTheWeek()], n.second());
  disp.drawString(64, 52, b);
  disp.setTextAlignment(TEXT_ALIGN_LEFT);
}
void drawEnv() {
  char b[24];
  disp.setColor(WHITE);
  disp.setFont(ArialMT_Plain_16);
  snprintf(b, sizeof(b), "%.1fC", tempC); disp.drawString(0, 13, b);
  snprintf(b, sizeof(b), "%.0f%%", hum); disp.drawString(70, 13, b);
  disp.setFont(ArialMT_Plain_10);
  const char* cm = (tempC < 20) ? "LANH" : (tempC < 26) ? "DE CHIU" : (tempC < 30) ? "HOI NONG" : "NONG";
  snprintf(b, sizeof(b), "Cam giac: %s", dhtOK ? cm : "--"); disp.drawString(0, 32, b);
  snprintf(b, sizeof(b), "Min %.1f  Max %.1f", tMin, tMax); disp.drawString(0, 42, b);
  disp.drawRect(0, 52, 128, 10);
  for (uint8_t i = 0; i < 24; i++) {
    int hgt = (int)((hist[(histIdx + i) % 24] - 18.0) * 7.0 / 17.0);
    if (hgt < 1) hgt = 1;
    if (hgt > 7) hgt = 7;
    for (int yy = 0; yy < hgt; yy++) { setPx(2 + i * 5, 61 - yy); setPx(3 + i * 5, 61 - yy); }
  }
}
void drawPomo() {
  if (millis() < blinkUntil) {
    disp.clear();
    disp.setColor(WHITE);
    disp.setTextAlignment(TEXT_ALIGN_CENTER);
    disp.setFont(ArialMT_Plain_16);
    disp.drawString(64, 20, "XONG!");
    disp.setFont(ArialMT_Plain_10);
    disp.drawString(64, 42, quotes[qIdx]);
    disp.setTextAlignment(TEXT_ALIGN_LEFT);
    return;
  }
  char b[24];
  unsigned long total = (pomoPhase ? sv.cfg.breakMin : sv.cfg.focusMin) * 60UL;
  unsigned long rem = pomoOn ? ((pomoEnd > millis()) ? (pomoEnd - millis()) / 1000 : 0) : total;
  uint8_t prog = (uint8_t)((total - rem) * 100 / total);
  disp.setColor(WHITE);
  disp.setFont(ArialMT_Plain_10);
  disp.setTextAlignment(TEXT_ALIGN_CENTER);
  disp.drawString(64, 12, pomoPhase ? "NGHI NGOI" : "TAP TRUNG");
  disp.setFont(ArialMT_Plain_24);
  snprintf(b, sizeof(b), "%02lu:%02lu", rem / 60, rem % 60);
  disp.drawString(64, 24, b);
  disp.setFont(ArialMT_Plain_10);
  snprintf(b, sizeof(b), "%s  %up/%up  x%u", pomoOn ? "RUN" : "PAUSE",
           (unsigned)sv.cfg.focusMin, (unsigned)sv.cfg.breakMin, (unsigned)sv.cycles);
  disp.drawString(64, 49, b);
  disp.setTextAlignment(TEXT_ALIGN_LEFT);
  disp.drawProgressBar(14, 58, 100, 4, prog);
}
void drawQuick() {
  char b[24];
  unsigned long total = qtMin * 60UL;
  unsigned long rem = qtOn ? ((qtEnd > millis()) ? (qtEnd - millis()) / 1000 : 0) : total;
  drawTime7(29, 14, (uint8_t)(rem / 60), (uint8_t)(rem % 60), qtOn && (millis() / 500) % 2);
  disp.setColor(WHITE);
  disp.setFont(ArialMT_Plain_10);
  disp.setTextAlignment(TEXT_ALIGN_CENTER);
  snprintf(b, sizeof(b), "%s  %u PHUT", qtOn ? "RUN" : "PAUSE", (unsigned)qtMin);
  disp.drawString(64, 48, b);
  disp.setTextAlignment(TEXT_ALIGN_LEFT);
  disp.drawProgressBar(10, 58, 108, 4, (uint8_t)(total ? (total - rem) * 100 / total : 0));
}
void drawStats() {
  char b[48];
  uint16_t mx = 1, tot = 0;
  for (uint8_t i = 0; i < 7; i++) { if (sv.weekly[i] > mx) mx = sv.weekly[i]; tot += sv.weekly[i]; }
  disp.setColor(WHITE);
  snprintf(b, sizeof(b), "TUAN NAY  tong:%u", (unsigned)tot);
  disp.setFont(ArialMT_Plain_10);
  disp.drawString(0, 0, b);
  disp.drawLine(0, 10, 127, 10);
  const char* dow[] = {"T2","T3","T4","T5","T6","T7","CN"};
  for (uint8_t i = 0; i < 7; i++) {
    int hgt = (int)(24L * sv.weekly[i] / mx);
    uint8_t bx = i * 18 + 2;
    disp.drawRect(bx, 13, 14, 26);
    if (hgt > 0) for (int y = 0; y < hgt; y++) disp.drawLine(bx+2, 38-y, bx+11, 38-y);
    disp.setTextAlignment(TEXT_ALIGN_CENTER);
    disp.drawString(bx + 7, 41, dow[i]);
    snprintf(b, sizeof(b), "%u", (unsigned)sv.weekly[i]);
    disp.drawString(bx + 7, 51, b);
    disp.setTextAlignment(TEXT_ALIGN_LEFT);
  }
}
void drawSet() {
  char b[32];
  const char* names[4] = {"Phut tap", "Phut nghi", "Dim dem", "Gio dim"};
  int vals[4] = {(int)sv.cfg.focusMin, (int)sv.cfg.breakMin, sv.cfg.autoDim ? 1 : 0, (int)sv.cfg.dimHour};
  disp.setColor(WHITE);
  disp.setFont(ArialMT_Plain_10);
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t y = 12 + i * 11;
    if (i == setIdx) { disp.setColor(WHITE); disp.fillRect(0, y, 128, 11); disp.setColor(BLACK); }
    if (i == setIdx) disp.drawString(2, y + 1, ">");
    snprintf(b, sizeof(b), "%s", names[i]); disp.drawString(10, y + 1, b);
    if (i == 2) snprintf(b, sizeof(b), "%s", vals[i] ? "ON" : "OFF");
    else snprintf(b, sizeof(b), "%d", vals[i]);
    disp.setTextAlignment(TEXT_ALIGN_RIGHT);
    disp.drawString(126, y + 1, b);
    disp.setTextAlignment(TEXT_ALIGN_LEFT);
    if (i == setIdx) disp.setColor(WHITE);
  }
}
void drawSys() {
  char b[40];
  uint32_t up = millis() / 1000;
  disp.setColor(WHITE);
  disp.setFont(ArialMT_Plain_10);
  snprintf(b, sizeof(b), "FW: %s", FW_VER); disp.drawString(0, 12, b);
  snprintf(b, sizeof(b), "Up: %luh%lum", (unsigned long)(up / 3600), (unsigned long)((up / 60) % 60)); disp.drawString(0, 24, b);
  snprintf(b, sizeof(b), "RAM: %u B", (unsigned)ESP.getFreeHeap()); disp.drawString(0, 36, b);
  snprintf(b, sizeof(b), "RTC:%s DHT:%s", rtcOK ? "OK" : "NO", dhtOK ? "OK" : "NO"); disp.drawString(0, 48, b);
}
void drawBreath() {
  uint32_t t = millis() % 8000;
  bool inhale = t < 4000;
  uint32_t ph = inhale ? t : t - 4000;
  uint8_t r = inhale ? (uint8_t)(6 + 10 * ph / 4000) : (uint8_t)(16 - 10 * ph / 4000);
  char b[24];
  disp.setColor(WHITE);
  for (uint8_t i = 0; i < 8; i++) {
    float a = (millis() / 500.0f) + i * 0.785f;
    setPx(64 + (int)(cos(a) * 24), 33 + (int)(sin(a) * 19));
  }
  disp.drawCircle(64, 33, 15);
  disp.fillCircle(64, 33, r);
  disp.setColor(BLACK);
  disp.setTextAlignment(TEXT_ALIGN_CENTER);
  disp.setFont(ArialMT_Plain_10);
  disp.drawString(64, 29, inhale ? "HIT" : "THO");
  disp.setColor(WHITE);
  snprintf(b, sizeof(b), "Nhip: %lu", (unsigned long)((millis() / 8000) % 1000));
  disp.drawString(64, 51, b);
  disp.setTextAlignment(TEXT_ALIGN_LEFT);
}

// ============ GAME: SHOOT ============
void gameInit() { shipX = 60; for (uint8_t i = 0; i < 3; i++) bulOn[i] = false; for (uint8_t i = 0; i < 5; i++) enOn[i] = false; gameScore = 0; gameOver = false; lastShot = lastSpawn = lastStep = millis(); }
void drawGame() {
  char b[16];
  disp.setColor(WHITE);
  disp.drawLine(0, 55, 127, 55);
  disp.fillRect(shipX - 4, 50, 8, 4); disp.fillRect(shipX - 1, 48, 2, 2);
  for (uint8_t i = 0; i < 3; i++) if (bulOn[i]) disp.fillRect(bulX[i], bulY[i], 2, 4);
  for (uint8_t i = 0; i < 5; i++) if (enOn[i]) { disp.fillRect(enX[i] - 3, enY[i] - 2, 6, 4); disp.fillRect(enX[i] - 1, enY[i] - 3, 2, 1); }
  snprintf(b, sizeof(b), "S:%u", (unsigned)gameScore);
  disp.setTextAlignment(TEXT_ALIGN_RIGHT); disp.drawString(127, 12, b); disp.setTextAlignment(TEXT_ALIGN_LEFT);
  if (gameOver) {
    disp.setColor(BLACK); disp.fillRect(28, 24, 72, 18);
    disp.setColor(WHITE); disp.drawRect(28, 24, 72, 18);
    disp.setTextAlignment(TEXT_ALIGN_CENTER);
    disp.setFont(ArialMT_Plain_10);
    disp.drawString(64, 27, "GAME OVER");
    disp.setTextAlignment(TEXT_ALIGN_LEFT);
  }
}
void gameStep() {
  if (gameOver) return;
  if (millis() - lastStep < 30) return;
  lastStep = millis();
  for (uint8_t i = 0; i < 3; i++) if (bulOn[i]) { bulY[i] -= 4; if (bulY[i] < 12) bulOn[i] = false; }
  for (uint8_t i = 0; i < 5; i++) if (enOn[i]) {
    enY[i] += 1;
    if (enY[i] > 60) { enOn[i] = false; continue; }
    bool hit = false;
    for (uint8_t j = 0; j < 3; j++) if (bulOn[j]) {
      if (abs(bulX[j] - enX[i]) < 5 && abs(bulY[j] - enY[i]) < 4) {
        enOn[i] = false; bulOn[j] = false; gameScore += 10; hit = true; break;
      }
    }
    if (!hit && enY[i] > 48 && abs(enX[i] - shipX) < 6) gameOver = true;
  }
}
void gameSpawn() {
  if (gameOver) return;
  if (millis() - lastSpawn < 1200) return;
  lastSpawn = millis();
  for (uint8_t i = 0; i < 5; i++) if (!enOn[i]) { enOn[i] = true; enX[i] = random(8, 120); enY[i] = 14; break; }
}

// ============ GAME: NYAN ============
void drawNyanRun() {
  disp.clear();
  uint8_t frame = (millis() / 100) % 16;
  int catX = (millis() / 20) % 150; if (catX > 130) catX = 130;
  drawNyan(catX, 25, frame);
  for (uint8_t i = 0; i < 12; i++) setPx((i * 17 + (millis() / 60)) % 128, 14 + (i * 5) % 40);
  disp.setColor(WHITE);
  disp.setTextAlignment(TEXT_ALIGN_CENTER);
  disp.setFont(ArialMT_Plain_10);
  disp.drawString(64, 55, "NYAN RUN");
  disp.setTextAlignment(TEXT_ALIGN_LEFT);
  disp.displayRot(sv.cfg.rotation);
}

// ============ GAME: TETRIS ============
bool tetCollide(int8_t px, int8_t py, uint8_t type, uint8_t rot) {
  for (uint8_t i = 0; i < 4; i++) {
    int8_t x = px + PIECES[type][rot][i*2]; int8_t y = py + PIECES[type][rot][i*2+1];
    if (x < 0 || x >= TW || y >= TH) return true;
    if (y >= 0 && (tetRows[y] & (1 << x))) return true;
  }
  return false;
}
void tetSpawn() { tetPiece = tetNext; tetNext = random(0, 7); tetRot = 0; tetPX = 3; tetPY = -1; if (tetCollide(tetPX, tetPY, tetPiece, tetRot)) tetOver = true; }
void tetLock() {
  for (uint8_t i = 0; i < 4; i++) {
    int8_t x = tetPX + PIECES[tetPiece][tetRot][i*2]; int8_t y = tetPY + PIECES[tetPiece][tetRot][i*2+1];
    if (y >= 0 && y < TH && x >= 0 && x < TW) tetRows[y] |= (1 << x);
  }
  uint8_t cleared = 0;
  for (int8_t y = TH - 1; y >= 0; y--) if (tetRows[y] == 0x3FF) { for (int8_t k = y; k > 0; k--) tetRows[k] = tetRows[k-1]; tetRows[0] = 0; cleared++; y++; }
  tetLines += cleared;
  if (cleared == 1) tetScore += 100; else if (cleared == 2) tetScore += 300; else if (cleared == 3) tetScore += 600; else if (cleared == 4) tetScore += 1000;
  tetSpawn();
}
void tetInit() { for (uint8_t i = 0; i < TH; i++) tetRows[i] = 0; tetScore = 0; tetLines = 0; tetOver = false; tetNext = random(0, 7); tetSpawn(); tetDropTime = millis(); }
void tetMove(int8_t dx) { if (!tetCollide(tetPX + dx, tetPY, tetPiece, tetRot)) tetPX += dx; }
void tetRotate() {
  uint8_t newRot = (tetRot + 1) % 4;
  if (!tetCollide(tetPX, tetPY, tetPiece, newRot)) tetRot = newRot;
  else if (!tetCollide(tetPX - 1, tetPY, tetPiece, newRot)) { tetPX--; tetRot = newRot; }
  else if (!tetCollide(tetPX + 1, tetPY, tetPiece, newRot)) { tetPX++; tetRot = newRot; }
}
void tetStep() {
  if (tetOver) return;
  uint32_t interval = 600 - (tetLines / 5) * 60; if (interval < 100) interval = 100;
  if (millis() - tetDropTime > interval) { tetDropTime = millis(); if (!tetCollide(tetPX, tetPY + 1, tetPiece, tetRot)) tetPY++; else tetLock(); }
}
void tetHardDrop() { while (!tetCollide(tetPX, tetPY + 1, tetPiece, tetRot)) tetPY++; tetLock(); }
void drawTetris() {
  char b[20];
  disp.setColor(WHITE);
  disp.drawRect(1, 11, TW*TS + 2, TH*TS + 2);
  for (uint8_t y = 0; y < TH; y++) for (uint8_t x = 0; x < TW; x++) if (tetRows[y] & (1 << x)) disp.fillRect(2 + x * TS, 12 + y * TS, TS - 1, TS - 1);
  if (!tetOver) for (uint8_t i = 0; i < 4; i++) {
    int8_t x = tetPX + PIECES[tetPiece][tetRot][i*2]; int8_t y = tetPY + PIECES[tetPiece][tetRot][i*2+1];
    if (y >= 0 && y < TH && x >= 0 && x < TW) disp.fillRect(2 + x * TS, 12 + y * TS, TS - 1, TS - 1);
  }
  disp.setFont(ArialMT_Plain_10);
  disp.drawString(38, 12, "TETRIS");
  snprintf(b, sizeof(b), "Diem:%u", (unsigned)tetScore); disp.drawString(38, 24, b);
  snprintf(b, sizeof(b), "Dong:%u", (unsigned)tetLines); disp.drawString(38, 36, b);
  disp.drawString(38, 48, "Next:");
  for (uint8_t i = 0; i < 4; i++) {
    int8_t x = PIECES[tetNext][0][i*2]; int8_t y = PIECES[tetNext][0][i*2+1];
    disp.fillRect(80 + x * 5, 48 + y * 5, 4, 4);
  }
  if (tetOver) {
    disp.setColor(BLACK); disp.fillRect(20, 26, 88, 18);
    disp.setColor(WHITE); disp.drawRect(20, 26, 88, 18);
    disp.setTextAlignment(TEXT_ALIGN_CENTER);
    disp.drawString(64, 29, "GAME OVER");
    disp.setTextAlignment(TEXT_ALIGN_LEFT);
  }
}

// ============ GAME: FLAPPY ============
void flappyInit() {
  flappyY = 30; flappyV = 0; flappyScore = 0; flappyOver = false;
  for (uint8_t i = 0; i < 3; i++) {
    flappyPipes[i] = 100 + i * 60;
    flappyGapY[i] = random(18, 36);
    flappyPassed[i] = false;
  }
  flappyLast = millis();
  flappyFlapTime = 0;
}
void flappyStep() {
  if (flappyOver) return;
  if (millis() - flappyLast < 50) return;
  flappyLast = millis();
  flappyV += 0.2;
  if (flappyV > 2) flappyV = 2;
  flappyY += flappyV;
  if (flappyY < 12) { flappyY = 12; flappyV = 0; }
  if (flappyY > 52) { flappyOver = true; return; }
  for (uint8_t i = 0; i < 3; i++) {
    flappyPipes[i] -= 1;
    if (flappyPipes[i] < 4) {
      flappyPipes[i] = 140;
      flappyGapY[i] = random(18, 36);
      flappyPassed[i] = false;
    }
    if (flappyPipes[i] + 10 > 20 && flappyPipes[i] < 25) {
      if (flappyY < flappyGapY[i] || flappyY + 5 > flappyGapY[i] + 16) flappyOver = true;
    }
    if (!flappyPassed[i] && flappyPipes[i] + 10 < 20) { flappyPassed[i] = true; flappyScore++; }
  }
}
void flappyFlap() {
  if (flappyOver) return;
  if (millis() - flappyFlapTime < 250) return;
  flappyV = -1.6;
  flappyFlapTime = millis();
}
void drawFlappy() {
  char b[16];
  disp.setColor(WHITE);
  for (uint8_t i = 0; i < 3; i++) {
    int px = flappyPipes[i]; if (px < 0 || px > 124) continue;
    disp.fillRect(px, 12, 10, flappyGapY[i] - 12);
    disp.fillRect(px, flappyGapY[i] + 16, 10, 52 - flappyGapY[i] - 16);
  }
  disp.drawLine(0, 56, 127, 56);
  disp.drawLine(0, 60, 127, 60);
  int by = (int)flappyY;
  disp.fillRect(20, by, 5, 4);
  disp.setPixel(25, by + 1);
  disp.setPixel(25, by + 2);
  disp.setPixel(21, by - 1);
  snprintf(b, sizeof(b), "%u", (unsigned)flappyScore);
  disp.setFont(ArialMT_Plain_16); disp.setTextAlignment(TEXT_ALIGN_CENTER); disp.drawString(64, 14, b); disp.setTextAlignment(TEXT_ALIGN_LEFT);
  if (flappyOver) {
    disp.setColor(BLACK); disp.fillRect(20, 26, 88, 18);
    disp.setColor(WHITE); disp.drawRect(20, 26, 88, 18);
    disp.setTextAlignment(TEXT_ALIGN_CENTER); disp.setFont(ArialMT_Plain_10);
    disp.drawString(64, 29, "GAME OVER");
    disp.setTextAlignment(TEXT_ALIGN_LEFT);
  }
}

// ============ GAME: CAT ============
void catInit() {
  catHunger = 100; catHappy = 100; catAnimType = 0;
  catLastDecay = millis(); catBlinkTime = millis(); catBlink = false; catScore = 0;
}
void catStep() {
  if (millis() - catLastDecay > 1000) {
    catLastDecay = millis();
    if (catHunger > 0) catHunger--;
    if (catHappy > 0) catHappy--;
  }
  if (!catBlink && millis() - catBlinkTime > 2500) { catBlink = true; catBlinkTime = millis(); }
  if (catBlink && millis() - catBlinkTime > 150) catBlink = false;
  if (catAnimType && millis() - catAnimTime > 800) catAnimType = 0;
}
void catFeed() {
  if (catHunger < 100) { catHunger += 20; if (catHunger > 100) catHunger = 100; catScore += 5; }
  catAnimType = 2; catAnimTime = millis(); catAnimX = random(30, 98); catAnimY = 20;
}
void catPet() {
  if (catHappy < 100) { catHappy += 20; if (catHappy > 100) catHappy = 100; catScore += 5; }
  catAnimType = 1; catAnimTime = millis(); catAnimX = random(30, 98); catAnimY = 20;
}
void drawCat() {
  disp.setColor(WHITE);
  disp.setFont(ArialMT_Plain_10);
  disp.drawString(0, 12, "Doi");
  disp.drawRect(0, 24, 60, 5);
  disp.fillRect(1, 25, catHunger * 58 / 100, 3);
  disp.drawString(70, 12, "Vui");
  disp.drawRect(68, 24, 60, 5);
  disp.fillRect(69, 25, catHappy * 58 / 100, 3);

  disp.drawLine(48, 38, 52, 32); disp.drawLine(52, 32, 56, 38);
  disp.drawLine(72, 38, 76, 32); disp.drawLine(76, 32, 80, 38);
  disp.drawRect(48, 38, 32, 20);
  if (catBlink) { disp.drawLine(56, 47, 60, 47); disp.drawLine(68, 47, 72, 47); }
  else {
    if (catHappy < 30) {
      disp.setPixel(58, 46); disp.setPixel(59, 47); disp.setPixel(60, 48);
      disp.setPixel(70, 46); disp.setPixel(69, 47); disp.setPixel(68, 48);
    } else if (catHappy > 70 && catHunger > 70) {
      disp.drawLine(56, 48, 58, 46); disp.drawLine(58, 46, 60, 48);
      disp.drawLine(68, 48, 70, 46); disp.drawLine(70, 46, 72, 48);
    } else { disp.fillRect(57, 45, 3, 4); disp.fillRect(69, 45, 3, 4); }
  }
  if (catHappy > 60) {
    disp.drawLine(60, 53, 62, 55); disp.drawLine(62, 55, 66, 55); disp.drawLine(66, 55, 68, 53);
  } else if (catHappy < 30) {
    disp.drawLine(60, 55, 62, 53); disp.drawLine(62, 53, 66, 53); disp.drawLine(66, 53, 68, 55);
  } else { disp.drawLine(62, 54, 66, 54); }
  disp.drawLine(42, 46, 47, 47); disp.drawLine(42, 50, 47, 50);
  disp.drawLine(86, 46, 81, 47); disp.drawLine(86, 50, 81, 50);

  if (catAnimType == 1) {
    int x = catAnimX, y = catAnimY - (millis() - catAnimTime) / 40;
    if (y > 10) {
      disp.fillCircle(x - 2, y, 2); disp.fillCircle(x + 2, y, 2);
      disp.fillRect(x - 4, y, 8, 2);
      disp.setPixel(x - 1, y + 2); disp.setPixel(x, y + 2); disp.setPixel(x + 1, y + 2); disp.setPixel(x, y + 3);
    }
  } else if (catAnimType == 2) {
    int x = catAnimX, y = catAnimY;
    disp.fillRect(x - 3, y - 1, 5, 3);
    disp.setPixel(x + 2, y - 1); disp.setPixel(x + 2, y + 1); disp.setPixel(x + 3, y);
    disp.setPixel(x - 1, y);
  }
}

// ============ GAME: SNAKE ============
void snakeInit() {
  snakeLen = 3; snake[0] = {30, 30}; snake[1] = {29, 30}; snake[2] = {28, 30};
  snakeDir = 1; snakeFoodX = random(4, 60); snakeFoodY = random(14, 58);
  snakeOver = false; snakeEatFlash = false; snakeScore = 0; snakeLast = millis();
}
void snakeStep() {
  if (snakeOver) return;
  if (millis() - snakeLast < 180) return;
  snakeLast = millis();
  SnakePt head = snake[0];
  switch (snakeDir) { case 0: head.y--; break; case 1: head.x++; break; case 2: head.y++; break; case 3: head.x--; break; }
  if (head.x < 2 || head.x > 62 || head.y < 14 || head.y > 60) { snakeOver = true; return; }
  for (uint8_t i = 1; i < snakeLen; i++) if (snake[i].x == head.x && snake[i].y == head.y) { snakeOver = true; return; }
  bool eat = (head.x == snakeFoodX && head.y == snakeFoodY);
  if (eat) { if (snakeLen < SNAKE_MAX) snakeLen++; snakeScore += 10; snakeFoodX = random(3, 61); snakeFoodY = random(15, 59); snakeEatFlash = true; }
  for (int i = snakeLen - 1; i > 0; i--) snake[i] = snake[i - 1];
  snake[0] = head;
}
void drawSnake() {
  char b[16];
  disp.setColor(WHITE);
  disp.drawRect(0, 12, 128, 50);
  if (!snakeEatFlash || (millis() / 200) % 2) disp.fillRect(snakeFoodX * 2, snakeFoodY, 2, 2);
  for (uint8_t i = 0; i < snakeLen; i++) disp.fillRect(snake[i].x * 2, snake[i].y, 2, 1);
  snprintf(b, sizeof(b), "S:%u L:%u", (unsigned)snakeScore, (unsigned)snakeLen);
  disp.setFont(ArialMT_Plain_10); disp.setTextAlignment(TEXT_ALIGN_RIGHT); disp.drawString(127, 1, b); disp.setTextAlignment(TEXT_ALIGN_LEFT);
  if (snakeOver) {
    disp.setColor(BLACK); disp.fillRect(20, 26, 88, 18);
    disp.setColor(WHITE); disp.drawRect(20, 26, 88, 18);
    disp.setTextAlignment(TEXT_ALIGN_CENTER);
    disp.drawString(64, 29, "GAME OVER");
    disp.setTextAlignment(TEXT_ALIGN_LEFT);
  }
}

// ============ DRAW DISPATCH ============
void drawModeScreen(uint8_t m) {
  disp.clear();
  disp.setColor(WHITE);
  disp.setFont(ArialMT_Plain_10);
  disp.drawString(0, 0, modeNames[m]);
  DateTime n = rtc.now();
  char b[12];
  snprintf(b, sizeof(b), "%02d:%02d", n.hour(), n.minute());
  disp.setTextAlignment(TEXT_ALIGN_RIGHT); disp.drawString(127, 0, b); disp.setTextAlignment(TEXT_ALIGN_LEFT);
  disp.drawLine(0, 10, 127, 10);
  switch (m) {
    case 0: drawClock(); break;
    case 1: drawEnv(); break;
    case 2: drawPomo(); break;
    case 3: drawQuick(); break;
    case 4: drawStats(); break;
    case 5: drawSet(); break;
    case 6: drawSys(); break;
    case 7: drawBreath(); break;
  }
  disp.displayRot(sv.cfg.rotation);
}

void drawPlayingGame(uint8_t g) {
  if (g == 1) { drawNyanRun(); return; }
  disp.clear();
  disp.setColor(WHITE);
  disp.setFont(ArialMT_Plain_10);
  disp.drawString(0, 0, gameNames[g]);
  DateTime n = rtc.now();
  char b[12]; snprintf(b, sizeof(b), "%02d:%02d", n.hour(), n.minute());
  disp.setTextAlignment(TEXT_ALIGN_RIGHT); disp.drawString(127, 0, b); disp.setTextAlignment(TEXT_ALIGN_LEFT);
  disp.drawLine(0, 10, 127, 10);
  switch (g) {
    case 0: drawGame(); break;
    case 2: drawTetris(); break;
    case 3: drawFlappy(); break;
    case 4: drawCat(); break;
    case 5: drawSnake(); break;
  }
  disp.displayRot(sv.cfg.rotation);
}

// ============ TICKS ============
void pomoTick() {
  if (pomoOn && (long)(millis() - pomoEnd) >= 0) {
    pomoPhase = !pomoPhase;
    if (!pomoPhase) { sv.cycles++; sv.weekly[rtc.now().dayOfTheWeek()]++; qIdx = (qIdx + 1) % 6; saveAll(); }
    pomoEnd = millis() + (pomoPhase ? sv.cfg.breakMin : sv.cfg.focusMin) * 60000UL;
    blinkUntil = millis() + 6000;
  }
}
void qtTick() { if (qtOn && (long)(millis() - qtEnd) >= 0) { qtOn = false; blinkUntil = millis() + 6000; } }

// ============ SETUP ============
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== BOOT " FW_VER " ===");
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_SEL, INPUT_PULLUP);
  EEPROM.begin(512);
  loadSave();
  Wire.begin(4, 5);
  Wire.setClock(400000);
  disp.init();
  dispFlipped = false;
  setRotation(sv.cfg.rotation);
  disp.setContrast(0xFF);
  rtcOK = rtc.begin();

  if (rtcOK) {
    DateTime now = rtc.now();
    Serial.printf("[RTC] Hien tai: %04d-%02d-%02d %02d:%02d:%02d\n",
                  now.year(), now.month(), now.day(),
                  now.hour(), now.minute(), now.second());
    if (now.year() < 2024) {
      Serial.println("[RTC] RTC mat gio / pin yeu -> vao web bam 'Dong bo gio'");
    }
  } else {
    Serial.println("[RTC] Khong tim thay DS3231!");
  }

  dht.begin();
  setupWeb();
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
  Serial.print("Free heap: "); Serial.println(ESP.getFreeHeap());
  lastBtn = millis();
  bootStart = millis();
  lastActivity = millis();
  randomSeed(micros());
  gameInit(); tetInit(); flappyInit(); catInit(); snakeInit();
}

// ============ LOOP ============
void loop() {
  server.handleClient();
  dnsServer.processNextRequest();

  if (booting) {
    drawBoot();
    if (!booting) {
      screen = SCR_MODE;
      currentMode = 0;
      cursorPos = 0;
      lastDraw = 0;
    }
    delay(20);
    return;
  }

  // Bất kỳ nút nào đang được giữ → activity (thoát power save ngay)
  if (isPressed(bLeft) || isPressed(bRight) || isPressed(bSel)) {
    touchActivity();
  }

  uint8_t eLeft  = pollBtn(bLeft);
  uint8_t eRight = pollBtn(bRight);
  uint8_t eSel   = pollBtn(bSel);
  bool anyBtn = (eLeft || eRight || eSel);

  bool gameReady = (millis() - gameStartTime > 300);

  switch (screen) {
    case SCR_MAIN:
      if (eRight == EV_SHORT) { cursorPos = (cursorPos + 1) % MAIN_COUNT; lastDraw = 0; }
      if (eLeft == EV_SHORT)  { cursorPos = (cursorPos == 0) ? MAIN_COUNT - 1 : cursorPos - 1; lastDraw = 0; }
      if (eSel == EV_SHORT) {
        if (cursorPos == 8) { screen = SCR_GAMES; gameIdx = 0; lastDraw = 0; }
        else {
          currentMode = cursorPos;
          screen = SCR_MODE;
          if (currentMode == 2) { pomoOn = false; pomoPhase = false; blinkUntil = 0; }
          lastDraw = 0;
        }
      }
      break;

    case SCR_MODE:
      if (currentMode == 5) {
        if (eLeft == EV_SHORT) { screen = SCR_MAIN; lastDraw = 0; }
        else if (eRight == EV_SHORT) { setIdx = (setIdx + 1) % 4; lastDraw = 0; }
        else if (eRight == EV_LONG)  { setIdx = (setIdx + 3) % 4; lastDraw = 0; }
        else if (eSel == EV_SHORT) {
          switch (setIdx) {
            case 0: sv.cfg.focusMin = (sv.cfg.focusMin >= 90) ? 5 : sv.cfg.focusMin + 5; break;
            case 1: sv.cfg.breakMin = (sv.cfg.breakMin >= 30) ? 1 : sv.cfg.breakMin + 1; break;
            case 2: sv.cfg.autoDim = !sv.cfg.autoDim; break;
            case 3: sv.cfg.dimHour = (sv.cfg.dimHour >= 23) ? 18 : sv.cfg.dimHour + 1; break;
          }
          saveAll();
          lastDraw = 0;
        }
        else if (eSel == EV_LONG) {
          switch (setIdx) {
            case 0: sv.cfg.focusMin = (sv.cfg.focusMin <= 5) ? 90 : sv.cfg.focusMin - 5; break;
            case 1: sv.cfg.breakMin = (sv.cfg.breakMin <= 1) ? 30 : sv.cfg.breakMin - 1; break;
            case 2: sv.cfg.autoDim = !sv.cfg.autoDim; break;
            case 3: sv.cfg.dimHour = (sv.cfg.dimHour <= 18) ? 23 : sv.cfg.dimHour - 1; break;
          }
          saveAll();
          lastDraw = 0;
        }
      }
      else {
        if (eLeft == EV_SHORT) { screen = SCR_MAIN; lastDraw = 0; }
        else if (currentMode == 2) {
          if (eSel == EV_SHORT) { if (!pomoOn) { pomoOn = true; pomoEnd = millis() + (pomoPhase ? sv.cfg.breakMin : sv.cfg.focusMin) * 60000UL; } else pomoOn = false; }
          if (eRight == EV_SHORT) { pomoOn = false; pomoPhase = false; blinkUntil = 0; }
        }
        else if (currentMode == 3) {
          if (eSel == EV_SHORT) { if (!qtOn) { qtOn = true; qtEnd = millis() + qtMin * 60000UL; } else qtOn = false; }
          const uint8_t st[6] = {5, 10, 15, 20, 25, 30};
          uint8_t k = 0;
          while (k < 5 && st[k] != qtMin) k++;
          if (eRight == EV_SHORT) {
            qtMin = st[(k + 1) % 6];
            if (qtOn) qtEnd = millis() + qtMin * 60000UL;
          }
        }
        else if (currentMode == 4) {
          if (eSel == EV_LONG) { for (uint8_t i = 0; i < 7; i++) sv.weekly[i] = 0; sv.cycles = 0; saveAll(); }
        }
      }
      break;

    case SCR_GAMES:
      {
        bool bothDown = isPressed(bLeft) && isPressed(bRight);
        if (bothDown) {
          if (bothHoldStart == 0) bothHoldStart = millis();
          else if (millis() - bothHoldStart > 600) {
            bothHoldStart = 0;
            screen = SCR_MAIN;
            cursorPos = 8;
            lastDraw = 0;
            break;
          }
        } else {
          bothHoldStart = 0;
        }
      }

      if (eRight == EV_SHORT) { gameIdx = (gameIdx + 1) % GAMES_COUNT; lastDraw = 0; }
      if (eLeft == EV_SHORT)  { gameIdx = (gameIdx == 0) ? GAMES_COUNT - 1 : gameIdx - 1; lastDraw = 0; }
      if (eSel == EV_LONG) { screen = SCR_MAIN; cursorPos = 8; lastDraw = 0; }
      else if (eSel == EV_SHORT) {
        currentGame = gameIdx;
        screen = SCR_PLAYING;
        gameStartTime = millis();
        bothHoldStart = 0;
        nyanRunning = (currentGame == 1);
        if (currentGame == 0) gameInit();
        else if (currentGame == 1) nyanUntil = millis() + 10000;
        else if (currentGame == 2) tetInit();
        else if (currentGame == 3) flappyInit();
        else if (currentGame == 4) catInit();
        else if (currentGame == 5) snakeInit();
        lastDraw = 0;
      }
      break;

    case SCR_PLAYING:
      {
        bool bothDown = isPressed(bLeft) && isPressed(bRight);
        if (bothDown) {
          if (bothHoldStart == 0) bothHoldStart = millis();
          else if (millis() - bothHoldStart > 600) {
            bothHoldStart = 0;
            nyanRunning = false;
            screen = SCR_GAMES;
            gameIdx = 0;
            lastDraw = 0;
            break;
          }
        } else {
          bothHoldStart = 0;
        }
      }

      if (!gameReady) {
        if (currentGame == 0) { gameSpawn(); gameStep(); }
        else if (currentGame == 3) flappyStep();
        else if (currentGame == 4) catStep();
        else if (currentGame == 5) snakeStep();
        else if (currentGame == 2) tetStep();
        break;
      }

      if (currentGame == 0) {
        if (gameOver) {
          if (eSel == EV_SHORT) { gameInit(); gameStartTime = millis(); }
          if (eLeft == EV_LONG || eRight == EV_LONG) { screen = SCR_GAMES; lastDraw = 0; }
        } else {
          if (eRight == EV_SHORT) { shipX += 6; if (shipX > 122) shipX = 122; }
          if (eLeft == EV_SHORT)  { shipX -= 6; if (shipX < 6) shipX = 6; }
          if (eSel == EV_SHORT && millis() - lastShot > 200) {
            for (uint8_t i = 0; i < 3; i++) if (!bulOn[i]) { bulOn[i] = true; bulX[i] = shipX; bulY[i] = 46; lastShot = millis(); break; }
          }
          if (eSel == EV_LONG) { screen = SCR_GAMES; lastDraw = 0; }
          else { gameSpawn(); gameStep(); }
        }
      }
      else if (currentGame == 1) {
        if (anyBtn || millis() > nyanUntil) { nyanRunning = false; screen = SCR_GAMES; lastDraw = 0; }
      }
      else if (currentGame == 2) {
        if (tetOver) {
          if (eSel == EV_SHORT) { tetInit(); gameStartTime = millis(); }
          if (eLeft == EV_LONG || eRight == EV_LONG) { screen = SCR_GAMES; lastDraw = 0; }
        } else {
          if (eLeft == EV_LONG) { screen = SCR_GAMES; lastDraw = 0; }
          else {
            if (eLeft  == EV_SHORT) { tetMove(-1); lastDraw = 0; }
            if (eRight == EV_SHORT) { tetMove(1);  lastDraw = 0; }
            if (eSel   == EV_SHORT) { tetRotate(); lastDraw = 0; }
            if (eSel   == EV_LONG)  { tetHardDrop(); lastDraw = 0; }
            tetStep();
          }
        }
      }
      else if (currentGame == 3) {
        if (flappyOver) {
          if (eSel == EV_SHORT) { flappyInit(); gameStartTime = millis(); }
          if (eLeft == EV_LONG || eRight == EV_LONG) { screen = SCR_GAMES; lastDraw = 0; }
        } else {
          if (isPressed(bSel)) flappyFlap();
          if (eLeft == EV_LONG) { screen = SCR_GAMES; lastDraw = 0; }
          else flappyStep();
        }
      }
      else if (currentGame == 4) {
        if (eLeft == EV_SHORT) catFeed();
        if (eRight == EV_SHORT) catPet();
        if (eSel == EV_LONG) { screen = SCR_GAMES; lastDraw = 0; }
        else catStep();
      }
      else if (currentGame == 5) {
        if (snakeOver) {
          if (eSel == EV_SHORT) { snakeInit(); gameStartTime = millis(); }
          if (eLeft == EV_LONG || eRight == EV_LONG) { screen = SCR_GAMES; lastDraw = 0; }
        } else {
          if (eLeft  == EV_SHORT) { snakeDir = (snakeDir + 3) % 4; lastDraw = 0; }
          if (eRight == EV_SHORT) { snakeDir = (snakeDir + 1) % 4; lastDraw = 0; }
          if (eSel   == EV_LONG) { screen = SCR_GAMES; lastDraw = 0; }
          else snakeStep();
        }
      }
      break;
  }

  pomoTick();
  qtTick();

  // Kiểm tra idle → vào power save
  if (!powerSave && (millis() - lastActivity) > IDLE_TIMEOUT_MS) {
    enterPowerSave();
  }

  // Đọc DHT - thưa hơn khi power save
  unsigned long dhtInt = powerSave ? 30000UL : 2000UL;
  if (screen != SCR_PLAYING && millis() - lastDht > dhtInt) {
    lastDht = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      dhtOK = true; tempC = t; hum = h;
      if (t < tMin) tMin = t;
      if (t > tMax) tMax = t;
      if (!histSeed) { for (uint8_t i = 0; i < 24; i++) hist[i] = t; histSeed = true; }
    } else dhtOK = false;
  }
  if (histSeed && millis() - lastHist > 600000UL) { lastHist = millis(); hist[histIdx] = tempC; histIdx = (histIdx + 1) % 24; }

  // Tính interval vẽ
  uint32_t iv = isNight() ? 1000 : 250;
  if (powerSave) iv = 1000;                       // power save: 1Hz
  if (screen == SCR_MODE && currentMode == 7) iv = 50;
  if (screen == SCR_PLAYING) {
    switch (currentGame) {
      case 0: iv = 40; break;
      case 1: iv = 60; break;
      case 2: iv = 40; break;
      case 3: iv = 40; break;
      case 4: iv = 100; break;
      case 5: iv = 40; break;
    }
  }

  if (millis() - lastDraw > iv) {
    lastDraw = millis();
    switch (screen) {
      case SCR_MAIN: drawMainMenu(); break;
      case SCR_MODE: drawModeScreen(currentMode); break;
      case SCR_GAMES: drawGamesMenu(); break;
      case SCR_PLAYING: drawPlayingGame(currentGame); break;
    }
  }
  setInv(millis() < blinkUntil && (millis() / 400) % 2);

  // Nghỉ CPU: dài hơn khi tiết kiệm điện → mát hơn
  delay(powerSave ? 50 : 2);
}