#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

#if __has_include("fluger_config.h")
#include "fluger_config.h"
#else
#include "fluger_config.example.h"
#endif

const int SERVO_PIN = 13;
const int LED_UP_PIN = 26;
const int BUZZER_PIN = 25;
const int BUTTON_PIN = 14;
const int MATRIX_CS_PIN = 5;

const int MATRIX_MODULES = 8;
const int MATRIX_WIDTH = MATRIX_MODULES * 8;

const float SCALE_PERCENT = 3.0;
const unsigned long LONG_PRESS_MS = 800;
const unsigned long DEBOUNCE_MS = 30;
const unsigned long POLL_INTERVAL_MS = 60000;
const unsigned long MAX_FRAME_PAUSE_MS = 60000;
const unsigned long MIN_CAROUSEL_MS = 2000;
const uint16_t HTTP_TIMEOUT_MS = 3000;
const uint16_t MODE_MESSAGE_MS = 1500;
const unsigned long SERVO_STEP_MS = 15;
const unsigned long BLINK_MS = 300;
const unsigned long BEEP_MS = 120;
const int BEEP_COUNT = 3;
const int BUZZER_FREQ = 2000;
const int BUZZER_DUTY = 512;

const int HISTORY_SIZE = 8;
const int BAR_LEVELS = 8;
const int BAR_WIDTH = 6;
const char BAR_FIRST = 16;

const int MAX_FRAMES = 4;
const int FRAME_SIZE = 32;

const char ARROW_UP = 1;
const char ARROW_DOWN = 2;

const uint8_t arrowUpShape[] = {5, 0x20, 0x38, 0x3E, 0x38, 0x20};
const uint8_t arrowDownShape[] = {5, 0x02, 0x0E, 0x3E, 0x0E, 0x02};
const uint8_t dotShape[] = {1, 0x40};
const uint8_t slashShape[] = {3, 0x60, 0x1C, 0x03};
const uint8_t digitShapes[10][4] = {
  {3, 0x7F, 0x41, 0x7F},
  {3, 0x42, 0x7F, 0x40},
  {3, 0x79, 0x49, 0x4F},
  {3, 0x49, 0x49, 0x7F},
  {3, 0x0F, 0x08, 0x7F},
  {3, 0x4F, 0x49, 0x79},
  {3, 0x7F, 0x49, 0x79},
  {3, 0x01, 0x01, 0x7F},
  {3, 0x7F, 0x49, 0x7F},
  {3, 0x4F, 0x49, 0x7F},
};
uint8_t barShapes[BAR_LEVELS][BAR_WIDTH + 1];

struct Currency {
  char code[8];
  int nominal;
  float rate;
  float changePercent;
  float history[HISTORY_SIZE];
  int historyCount;
};

const int MAX_CURRENCIES = 10;
Currency currencies[MAX_CURRENCIES] = {
  {"CNY", 1, 11.50, 0.44, {11.38, 11.40, 11.42, 11.41, 11.44, 11.46, 11.45, 11.50}, 8},
  {"USD", 1, 82.10, -0.35, {83.40, 83.10, 82.90, 82.70, 82.60, 82.50, 82.39, 82.10}, 8},
  {"EUR", 1, 95.30, 1.25, {93.80, 94.00, 94.20, 94.10, 94.30, 94.60, 94.12, 95.30}, 8},
  {"KZT", 100, 16.20, -0.12, {16.40, 16.38, 16.35, 16.30, 16.28, 16.25, 16.22, 16.20}, 8},
  {"MNT", 100, 2.40, -1.64, {2.48, 2.47, 2.47, 2.46, 2.45, 2.45, 2.44, 2.40}, 8},
};
int currencyCount = 5;

float alertPercent = 1.0;
unsigned long carouselMs = 10000;

MD_Parola matrix(MD_MAX72XX::FC16_HW, MATRIX_CS_PIN, MATRIX_MODULES);
Servo servo;
ESP32PWM buzzer;

int currentIndex = 0;
bool autoMode = true;
unsigned long lastSwitchTime = 0;

int servoAngle = 90;
int servoTarget = 90;
unsigned long lastServoStepTime = 0;

int beepStep = BEEP_COUNT * 2;
unsigned long beepStepTime = 0;

char frames[MAX_FRAMES][FRAME_SIZE];
int frameCount = 0;
int frameIndex = 0;
char shownText[FRAME_SIZE];
unsigned long framePause = 2000;
bool modeMessageShown = false;

bool serverOk = false;
bool pollDone = false;
unsigned long lastPollTime = 0;
wl_status_t lastWifiStatus = WL_IDLE_STATUS;

bool buttonWasDown = false;
bool longPressDone = false;
unsigned long pressStartTime = 0;

const char* modeName() {
  if (autoMode) {
    return "AUTO";
  }
  return "MANUAL";
}

float historyAverage(const Currency& currency) {
  if (currency.historyCount == 0) {
    return currency.rate;
  }
  float sum = 0;
  for (int i = 0; i < currency.historyCount; i++) {
    sum += currency.history[i];
  }
  return sum / currency.historyCount;
}

int angleForRate(const Currency& currency) {
  float average = historyAverage(currency);
  float percent = 0;
  if (average > 0) {
    percent = (currency.rate - average) / average * 100.0;
  }
  if (percent > SCALE_PERCENT) {
    percent = SCALE_PERCENT;
  }
  if (percent < -SCALE_PERCENT) {
    percent = -SCALE_PERCENT;
  }
  return 90 + (int)round(percent / SCALE_PERCENT * 90.0);
}

bool isRising(const Currency& currency) {
  return currency.changePercent >= alertPercent;
}

bool isFalling(const Currency& currency) {
  return currency.changePercent <= -alertPercent;
}

char arrowForChange(float percent) {
  if (percent > 0) {
    return ARROW_UP;
  }
  if (percent < 0) {
    return ARROW_DOWN;
  }
  return ' ';
}

void makeBarShapes() {
  for (int level = 1; level <= BAR_LEVELS; level++) {
    uint8_t column = (uint8_t)(0xFF << (8 - level));
    barShapes[level - 1][0] = BAR_WIDTH;
    for (int i = 1; i <= BAR_WIDTH; i++) {
      barShapes[level - 1][i] = column;
    }
  }
}

void addCustomChars() {
  makeBarShapes();
  for (int level = 0; level < BAR_LEVELS; level++) {
    matrix.addChar(BAR_FIRST + level, barShapes[level]);
  }
  matrix.addChar(ARROW_UP, arrowUpShape);
  matrix.addChar(ARROW_DOWN, arrowDownShape);
  for (int digit = 0; digit < 10; digit++) {
    matrix.addChar('0' + digit, digitShapes[digit]);
  }
  matrix.addChar('.', dotShape);
  matrix.addChar('/', slashShape);
}

void showText(const char* text, uint16_t pauseMs) {
  strlcpy(shownText, text, sizeof(shownText));
  matrix.displayClear();
  matrix.displayText(shownText, PA_CENTER, 0, pauseMs, PA_PRINT, PA_NO_EFFECT);
}

void makeRateText(const Currency& currency, char* text, int size) {
  if (currency.nominal == 1) {
    snprintf(text, size, "%s %.2f", currency.code, currency.rate);
  } else {
    snprintf(text, size, "%s %.2f/%d", currency.code, currency.rate, currency.nominal);
  }
}

void makeChangeText(const Currency& currency, char* text, int size) {
  char arrow = arrowForChange(currency.changePercent);
  snprintf(text, size, "%c%.2f", arrow, fabs(currency.changePercent));
}

void makeChartText(const Currency& currency, char* text) {
  float low = currency.history[0];
  float high = currency.history[0];
  for (int i = 1; i < currency.historyCount; i++) {
    low = min(low, currency.history[i]);
    high = max(high, currency.history[i]);
  }

  for (int i = 0; i < currency.historyCount; i++) {
    int level = BAR_LEVELS / 2;
    if (high > low) {
      level = 1 + (int)round((currency.history[i] - low) / (high - low) * (BAR_LEVELS - 1));
    }
    text[i] = BAR_FIRST + level - 1;
  }
  text[currency.historyCount] = '\0';
}

void makeFrames(const Currency& currency) {
  char rateText[20];
  char changeText[11];
  makeRateText(currency, rateText, sizeof(rateText));
  makeChangeText(currency, changeText, sizeof(changeText));

  snprintf(frames[0], FRAME_SIZE, "%s %s", rateText, changeText);
  if (matrix.getTextColumns(frames[0]) <= MATRIX_WIDTH) {
    frameCount = 1;
  } else {
    strlcpy(frames[0], rateText, FRAME_SIZE);
    strlcpy(frames[1], changeText, FRAME_SIZE);
    frameCount = 2;
  }

  if (currency.historyCount >= 2) {
    makeChartText(currency, frames[frameCount]);
    frameCount++;
  }

  if (!serverOk) {
    strlcpy(frames[frameCount], "no link", FRAME_SIZE);
    frameCount++;
  }

  framePause = carouselMs / frameCount;
  if (framePause > MAX_FRAME_PAUSE_MS) {
    framePause = MAX_FRAME_PAUSE_MS;
  }
}

void printCurrency(const Currency& currency) {
  Serial.printf("%s x%d %.2f %+.2f%%  avg %.2f  ->  angle %d\r\n", currency.code, currency.nominal,
                currency.rate, currency.changePercent, historyAverage(currency), angleForRate(currency));
}

void printAllCurrencies() {
  for (int i = 0; i < currencyCount; i++) {
    Serial.print("[data] ");
    printCurrency(currencies[i]);
  }
}

void setBuzzer(bool on) {
  buzzer.write(on ? BUZZER_DUTY : 0);
}

void startBeeps() {
  beepStep = 0;
  beepStepTime = millis();
  setBuzzer(true);
}

void stopBeeps() {
  beepStep = BEEP_COUNT * 2;
  setBuzzer(false);
}

void showCurrency(int index) {
  Currency& currency = currencies[index];
  makeFrames(currency);
  frameIndex = 0;
  showText(frames[0], framePause);
  servoTarget = angleForRate(currency);
  Serial.printf("[%s] ", modeName());
  printCurrency(currency);
  if (isFalling(currency)) {
    startBeeps();
  } else {
    stopBeeps();
  }
}

void showNextCurrency() {
  currentIndex = (currentIndex + 1) % currencyCount;
  lastSwitchTime = millis();
  showCurrency(currentIndex);
}

void readCurrency(JsonObject item, Currency& currency) {
  const char* code = item["code"];
  if (code == nullptr) {
    code = "???";
  }
  strlcpy(currency.code, code, sizeof(currency.code));

  currency.nominal = 1;
  if (item["nominal"].is<int>()) {
    currency.nominal = item["nominal"].as<int>();
  }

  currency.rate = 0.0;
  if (item["rate"].is<float>()) {
    currency.rate = item["rate"].as<float>();
  }

  currency.changePercent = 0.0;
  if (item["change"].is<float>()) {
    currency.changePercent = item["change"].as<float>();
  }

  currency.historyCount = 0;
  JsonArray history = item["history"];
  int skip = 0;
  if ((int)history.size() > HISTORY_SIZE) {
    skip = history.size() - HISTORY_SIZE;
  }
  for (JsonVariant value : history) {
    if (skip > 0) {
      skip--;
    } else if (value.is<float>()) {
      currency.history[currency.historyCount] = value.as<float>();
      currency.historyCount++;
    }
  }
}

void readSettings(JsonObject settings) {
  if (settings["alert_pct"].is<float>()) {
    alertPercent = settings["alert_pct"].as<float>();
  }
  if (settings["carousel_s"].is<float>()) {
    carouselMs = (unsigned long)(settings["carousel_s"].as<float>() * 1000);
    if (carouselMs < MIN_CAROUSEL_MS) {
      carouselMs = MIN_CAROUSEL_MS;
    }
  }
}

bool readRates(JsonDocument& doc) {
  JsonArray items = doc["rates"];
  if (items.size() == 0) {
    return false;
  }

  int count = 0;
  for (JsonObject item : items) {
    if (count >= MAX_CURRENCIES) {
      break;
    }
    readCurrency(item, currencies[count]);
    count++;
  }
  currencyCount = count;

  readSettings(doc["settings"]);
  return true;
}

bool downloadRates() {
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(SERVER_URL)) {
    return false;
  }

  int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("[net] HTTP %d\r\n", status);
    http.end();
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, http.getString());
  http.end();
  if (error) {
    Serial.printf("[net] JSON: %s\r\n", error.c_str());
    return false;
  }

  return readRates(doc);
}

void refreshCurrentCurrency() {
  if (currentIndex >= currencyCount) {
    currentIndex = 0;
  }
  makeFrames(currencies[currentIndex]);
  servoTarget = angleForRate(currencies[currentIndex]);
}

void printWifiStatus(wl_status_t status) {
  if (status == WL_CONNECTED) {
    Serial.printf("[net] Wi-Fi ok, IP %s\r\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.printf("[net] Wi-Fi status %d\r\n", status);
  }
}

void pollServer() {
  lastPollTime = millis();
  pollDone = true;

  bool ok = downloadRates();
  if (ok) {
    Serial.printf("[net] %s: ok\r\n", SERVER_URL);
    printAllCurrencies();
  } else {
    Serial.printf("[net] %s: no link\r\n", SERVER_URL);
  }

  if (ok || serverOk) {
    serverOk = ok;
    refreshCurrentCurrency();
  }
}

void updateNetwork() {
  wl_status_t status = WiFi.status();
  if (status != lastWifiStatus) {
    lastWifiStatus = status;
    printWifiStatus(status);
  }

  if (status != WL_CONNECTED) {
    pollDone = false;
    if (serverOk) {
      serverOk = false;
      refreshCurrentCurrency();
    }
    return;
  }

  if (pollDone && millis() - lastPollTime < POLL_INTERVAL_MS) {
    return;
  }
  pollServer();
}

void updateServo() {
  if (servoAngle == servoTarget) {
    return;
  }
  if (millis() - lastServoStepTime < SERVO_STEP_MS) {
    return;
  }
  lastServoStepTime = millis();
  if (servoTarget > servoAngle) {
    servoAngle++;
  } else {
    servoAngle--;
  }
  servo.write(servoAngle);
}

void updateLeds() {
  Currency& currency = currencies[currentIndex];
  bool blinkOn = (millis() / BLINK_MS) % 2;
  digitalWrite(LED_UP_PIN, (isRising(currency) && blinkOn) ? HIGH : LOW);
}

void updateBuzzer() {
  if (beepStep >= BEEP_COUNT * 2) {
    return;
  }
  if (millis() - beepStepTime < BEEP_MS) {
    return;
  }
  beepStepTime = millis();
  beepStep++;
  setBuzzer(beepStep < BEEP_COUNT * 2 && beepStep % 2 == 0);
}

void onLongPress(unsigned long now) {
  longPressDone = true;
  autoMode = !autoMode;
  lastSwitchTime = now;
  showText(modeName(), MODE_MESSAGE_MS);
  modeMessageShown = true;
}

void onShortPress() {
  autoMode = false;
  showNextCurrency();
}

void updateButton() {
  bool buttonDown = digitalRead(BUTTON_PIN) == LOW;
  unsigned long now = millis();

  if (buttonDown && !buttonWasDown) {
    pressStartTime = now;
    longPressDone = false;
  }

  if (buttonDown && !longPressDone && now - pressStartTime >= LONG_PRESS_MS) {
    onLongPress(now);
  }

  if (!buttonDown && buttonWasDown) {
    if (!longPressDone && now - pressStartTime > DEBOUNCE_MS) {
      onShortPress();
    }
  }

  buttonWasDown = buttonDown;
}

bool timeForNextCurrency() {
  unsigned long cycleMs = frameCount * framePause;
  unsigned long shownMs = millis() - lastSwitchTime;
  return autoMode && shownMs + cycleMs / 2 >= carouselMs;
}

void updateMatrix() {
  if (!matrix.displayAnimate()) {
    return;
  }

  if (modeMessageShown) {
    modeMessageShown = false;
    showCurrency(currentIndex);
    return;
  }

  frameIndex++;
  if (frameIndex < frameCount) {
    showText(frames[frameIndex], framePause);
  } else if (timeForNextCurrency()) {
    showNextCurrency();
  } else {
    frameIndex = 0;
    showText(frames[0], framePause);
  }
}

void startWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);
  Serial.printf("[net] Wi-Fi: connecting to %s\r\n", WIFI_SSID);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_UP_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  servo.attach(SERVO_PIN);
  servo.write(servoAngle);
  buzzer.attachPin(BUZZER_PIN, BUZZER_FREQ, 10);
  setBuzzer(false);

  matrix.begin();
  matrix.setIntensity(4);
  addCustomChars();

  Serial.println("Valutny fluger: started");
  showCurrency(currentIndex);
  lastSwitchTime = millis();

  startWifi();
}

void loop() {
  updateButton();
  updateNetwork();
  updateMatrix();
  updateServo();
  updateLeds();
  updateBuzzer();
}
