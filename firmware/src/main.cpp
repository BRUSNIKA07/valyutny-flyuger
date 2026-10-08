#include <Arduino.h>

// ===== Валютный флюгер =====
// ESP32 + сервопривод (флюгер) + матрица MAX7219 8x32 + 2 светодиода + кнопка.
// Курсы и настройки раз в 60 с берутся с сервера (GET /api/rates) по Wi-Fi.
// Пока ответа нет или связь пропала — показываем последние данные (сначала демо) и кадр "no link".

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

// Wi-Fi и адрес сервера: свой fluger_config.h (не в git), иначе значения для Wokwi из fluger_config.example.h
// (имя не config.h: файл с таким именем уже есть в toolchain ESP32)
#if __has_include("fluger_config.h")
#include "fluger_config.h"
#else
#include "fluger_config.example.h"
#endif

// ---------- Пины ----------
#define SERVO_PIN     13
#define LED_UP_PIN    26   // зелёный: рост
#define LED_DOWN_PIN  27   // красный: падение
#define BUTTON_PIN    14   // кнопка на GND, внутренняя подтяжка
#define CS_PIN         5   // матрица: DIN=23, CLK=18 (аппаратный SPI), CS=5

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES   8    // 8 модулей 8x8 = 8x64
#define MATRIX_COLS   (MAX_DEVICES * 8)

// ---------- Матрица ----------
// Вся строка сразу, по центру, без прокрутки: "USD 82.10 ▼0.35".
// Для nominal = 100: "KZT 16.20/100", изменение — следующим кадром (вместе не влезает).
const uint16_t MODE_MSG_PAUSE = 1500;

// Свои символы. Столбцы слева направо, бит 0 — верхняя строка.
#define CH_UP   '\x01'
#define CH_DOWN '\x02'
const uint8_t arrowUp[]   = {5, 0x20, 0x38, 0x3E, 0x38, 0x20};  // ▲
const uint8_t arrowDown[] = {5, 0x02, 0x0E, 0x3E, 0x0E, 0x02};  // ▼

// Узкие цифры (3 точки вместо 5) — иначе строка не влезает в 64 точки
const uint8_t narrowDigits[10][4] = {
  {3, 0x7F, 0x41, 0x7F},  // 0
  {3, 0x42, 0x7F, 0x40},  // 1
  {3, 0x79, 0x49, 0x4F},  // 2
  {3, 0x49, 0x49, 0x7F},  // 3
  {3, 0x0F, 0x08, 0x7F},  // 4
  {3, 0x4F, 0x49, 0x79},  // 5
  {3, 0x7F, 0x49, 0x79},  // 6
  {3, 0x01, 0x01, 0x7F},  // 7
  {3, 0x7F, 0x49, 0x7F},  // 8
  {3, 0x4F, 0x49, 0x7F},  // 9
};
const uint8_t narrowDot[]   = {1, 0x40};              // .
const uint8_t narrowSlash[] = {3, 0x60, 0x1C, 0x03};  // /

// ---------- Настройки ----------
const float SCALE_PCT          = 2.0;    // шкала флюгера: от -2% до +2%
const unsigned long LONG_PRESS_MS = 800; // удержание кнопки = смена режима
const unsigned long POLL_MS     = 60000; // опрос сервера
const uint16_t HTTP_TIMEOUT_MS  = 3000;
// Приходят в settings ответа сервера (alert_pct, carousel_s); до первого ответа — по умолчанию
float alertPct          = 1.0;    // порог мигания светодиода, %
unsigned long carouselMs = 10000; // смена валюты в авторежиме, мс

// ---------- Данные ----------
struct Currency {
  char code[8];
  int nominal;      // за сколько единиц валюты дана цена (1 или 100, как у ЦБ)
  float rate;       // рублей за nominal единиц
  float changePct;  // изменение за сутки, %
};

// Список валют приходит с сервера. До первого ответа — демо-значения, не реальные курсы.
const int MAX_CURRENCIES = 10;
Currency currencies[MAX_CURRENCIES] = {
  {"CNY",   1, 11.50,  0.42},
  {"USD",   1, 82.10, -0.35},
  {"EUR",   1, 95.30,  1.25},
  {"KZT", 100, 16.20, -0.10},
  {"MNT", 100,  2.40, -1.60},
};
int currencyCount = 5;

// ---------- Связь ----------
bool linkOk = false;             // последний опрос сервера удался
unsigned long lastPoll = 0;
bool polledOnce = false;
wl_status_t wifiPrev = WL_IDLE_STATUS;

// ---------- Объекты ----------
MD_Parola matrix(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
Servo needle;

// ---------- Состояние ----------
int current = 0;
bool autoMode = true;
unsigned long lastSwitch = 0;

int needleAngle = 90;   // текущий угол
int targetAngle = 90;   // куда едем
unsigned long lastServoStep = 0;

char text[48];           // строка для лога в Serial
bool modeMsgPending = false;

char frames[3][28];      // кадры текущей валюты (+ "no link")
int frameCount = 0;
int frameIdx = 0;
char frameText[28];      // то, что сейчас на матрице (MD_Parola хранит указатель)
long framePause = 2000;

bool btnPrev = HIGH;
unsigned long pressStart = 0;
bool longHandled = false;

// Перевод изменения в процентах в угол сервопривода (0..180, 90 = ноль)
int angleFor(float pct) {
  if (pct >  SCALE_PCT) pct =  SCALE_PCT;
  if (pct < -SCALE_PCT) pct = -SCALE_PCT;
  return 90 + (int)(pct / SCALE_PCT * 90.0);
}

// Показать кадр целиком по центру и подержать pause мс
void showFrame(const char* msg, uint16_t pause) {
  strlcpy(frameText, msg, sizeof(frameText));
  matrix.displayClear();
  matrix.displayText(frameText, PA_CENTER, 0, pause, PA_PRINT, PA_NO_EFFECT);
}

// Кадры валюты: "USD 82.10 ▼0.35"; если не влезает — "KZT 16.20/100" и "▼0.10" отдельно
void buildFrames(const Currency &c) {
  char rate[24], change[12];
  if (c.nominal == 1) snprintf(rate, sizeof(rate), "%s %.2f", c.code, c.rate);
  else snprintf(rate, sizeof(rate), "%s %.2f/%d", c.code, c.rate, c.nominal);
  char arrow = c.changePct > 0 ? CH_UP : (c.changePct < 0 ? CH_DOWN : ' ');
  snprintf(change, sizeof(change), "%c%.2f", arrow, fabs(c.changePct));

  frameCount = 0;
  snprintf(frames[0], sizeof(frames[0]), "%s %s", rate, change);
  if (matrix.getTextColumns(frames[0]) <= MATRIX_COLS) {
    frameCount = 1;
  } else {
    strlcpy(frames[0], rate, sizeof(frames[0]));
    strlcpy(frames[1], change, sizeof(frames[1]));
    frameCount = 2;
  }
  if (!linkOk) strlcpy(frames[frameCount++], "no link", sizeof(frames[0]));

  // Весь цикл кадров укладываем в carousel_s
  framePause = min(carouselMs / frameCount, 60000UL);
}

void showCurrency(int i) {
  Currency &c = currencies[i];
  buildFrames(c);
  frameIdx = 0;
  showFrame(frames[0], framePause);
  snprintf(text, sizeof(text), "%s x%d %.2f %+.2f%%", c.code, c.nominal, c.rate, c.changePct);
  targetAngle = angleFor(c.changePct);
  Serial.printf("[%s] %s  ->  angle %d\r\n", autoMode ? "AUTO" : "MANUAL", text, targetAngle);
}

void nextCurrency() {
  current = (current + 1) % currencyCount;
  lastSwitch = millis();
  showCurrency(current);
}

// Один запрос к серверу: курсы и настройки. true — если ответ получен и разобран.
bool fetchRates() {
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(SERVER_URL)) return false;
  int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("[net] HTTP %d\r\n", status);
    http.end();
    return false;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) {
    Serial.printf("[net] JSON: %s\r\n", err.c_str());
    return false;
  }

  JsonArray rates = doc["rates"];
  if (rates.size() == 0) return false;
  int n = 0;
  for (JsonObject r : rates) {
    if (n >= MAX_CURRENCIES) break;
    Currency &c = currencies[n++];
    strlcpy(c.code, r["code"] | "???", sizeof(c.code));
    c.nominal   = r["nominal"] | 1;
    c.rate      = r["rate"] | 0.0f;
    c.changePct = r["change"] | 0.0f;
  }
  currencyCount = n;

  JsonObject s = doc["settings"];
  if (s["alert_pct"].is<float>())  alertPct = s["alert_pct"].as<float>();
  if (s["carousel_s"].is<float>()) carouselMs = max(2000UL, (unsigned long)(s["carousel_s"].as<float>() * 1000));
  return true;
}

// Новые данные или смена состояния связи: пересобрать кадры, не прерывая текущий
void applyData() {
  if (current >= currencyCount) current = 0;
  buildFrames(currencies[current]);
  targetAngle = angleFor(currencies[current].changePct);
}

// Wi-Fi подключается сам в фоне; сервер опрашиваем сразу после подключения и затем раз в POLL_MS
void updateNetwork() {
  wl_status_t st = WiFi.status();
  if (st != wifiPrev) {
    wifiPrev = st;
    if (st == WL_CONNECTED) Serial.printf("[net] Wi-Fi ok, IP %s\r\n", WiFi.localIP().toString().c_str());
    else Serial.printf("[net] Wi-Fi status %d\r\n", st);
  }

  if (st != WL_CONNECTED) {
    polledOnce = false;                // после переподключения опросить сразу
    if (linkOk) { linkOk = false; applyData(); }
    return;
  }
  if (polledOnce && millis() - lastPoll < POLL_MS) return;

  lastPoll = millis();
  polledOnce = true;
  bool ok = fetchRates();
  Serial.printf("[net] %s: %s\r\n", SERVER_URL, ok ? "ok" : "no link");
  if (ok || linkOk) {                  // новые данные или связь пропала
    linkOk = ok;
    applyData();
  }
}

// Плавный поворот флюгера: 1 градус каждые 15 мс
void updateNeedle() {
  if (needleAngle == targetAngle) return;
  if (millis() - lastServoStep < 15) return;
  lastServoStep = millis();
  needleAngle += (targetAngle > needleAngle) ? 1 : -1;
  needle.write(needleAngle);
}

// Светодиоды тренда; при скачке >= ALERT_PCT — мигание
void updateLeds() {
  float pct = currencies[current].changePct;
  bool on = true;
  if (fabs(pct) >= alertPct) on = (millis() / 300) % 2;
  digitalWrite(LED_UP_PIN,   (pct > 0 && on) ? HIGH : LOW);
  digitalWrite(LED_DOWN_PIN, (pct < 0 && on) ? HIGH : LOW);
}

// Короткое нажатие — следующая валюта (и ручной режим), удержание — авто/ручной
void updateButton() {
  bool btn = digitalRead(BUTTON_PIN);
  unsigned long now = millis();

  if (btnPrev == HIGH && btn == LOW) {          // нажали
    pressStart = now;
    longHandled = false;
  }
  if (btn == LOW && !longHandled && now - pressStart >= LONG_PRESS_MS) {
    longHandled = true;                           // удержание
    autoMode = !autoMode;
    lastSwitch = now;
    showFrame(autoMode ? "AUTO" : "MANUAL", MODE_MSG_PAUSE);
    modeMsgPending = true;
  }
  if (btnPrev == LOW && btn == HIGH) {          // отпустили
    if (!longHandled && now - pressStart > 30) { // короткое (с антидребезгом)
      autoMode = false;
      nextCurrency();
    }
  }
  btnPrev = btn;
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_UP_PIN, OUTPUT);
  pinMode(LED_DOWN_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  needle.attach(SERVO_PIN);
  needle.write(needleAngle);

  matrix.begin();
  matrix.setIntensity(4);
  matrix.addChar(CH_UP, arrowUp);
  matrix.addChar(CH_DOWN, arrowDown);
  for (int d = 0; d < 10; d++) matrix.addChar('0' + d, narrowDigits[d]);
  matrix.addChar('.', narrowDot);
  matrix.addChar('/', narrowSlash);

  Serial.println("Valutny fluger: started");   // println сам добавляет \r\n
  showCurrency(current);
  lastSwitch = millis();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);   // один раз; не ждём — подключение идёт в фоне
  Serial.printf("[net] Wi-Fi: connecting to %s\r\n", WIFI_SSID);
}

void loop() {
  updateButton();
  updateNetwork();

  if (matrix.displayAnimate()) {     // кадр въехал, постоял и уехал
    unsigned long cycleMs = frameCount * framePause;
    if (modeMsgPending) {            // после Auto/Manual вернуть валюту
      modeMsgPending = false;
      showCurrency(current);
    } else if (++frameIdx < frameCount) {
      showFrame(frames[frameIdx], framePause);
    } else if (autoMode && millis() - lastSwitch + cycleMs / 2 >= carouselMs) {
      nextCurrency();                // валюту меняем только между циклами кадров
    } else {
      frameIdx = 0;                  // ручной режим или carousel_s длинный — цикл заново
      showFrame(frames[0], framePause);
    }
  }

  updateNeedle();
  updateLeds();
}
