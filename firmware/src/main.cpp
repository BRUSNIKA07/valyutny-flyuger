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
const int LED_DOWN_PIN = 27;
const int BUTTON_PIN = 14;
const int MATRIX_CS_PIN = 5;

const int MATRIX_MODULES = 8;
const int MATRIX_WIDTH = MATRIX_MODULES * 8;

const float SCALE_PERCENT = 2.0;
const unsigned long LONG_PRESS_MS = 800;
const unsigned long DEBOUNCE_MS = 30;
const unsigned long POLL_INTERVAL_MS = 60000;
const unsigned long MAX_FRAME_PAUSE_MS = 60000;
const unsigned long MIN_CAROUSEL_MS = 2000;
const uint16_t HTTP_TIMEOUT_MS = 3000;
const uint16_t MODE_MESSAGE_MS = 1500;
const unsigned long SERVO_STEP_MS = 15;
const unsigned long BLINK_MS = 300;

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

struct Currency {
  char code[8];
  int nominal;
  float rate;
  float changePercent;
};

const int MAX_CURRENCIES = 10;
Currency currencies[MAX_CURRENCIES] = {
  {"CNY", 1, 11.50, 0.42},
  {"USD", 1, 82.10, -0.35},
  {"EUR", 1, 95.30, 1.25},
  {"KZT", 100, 16.20, -0.10},
  {"MNT", 100, 2.40, -1.60},
};
int currencyCount = 5;

float alertPercent = 1.0;
unsigned long carouselMs = 10000;

MD_Parola matrix(MD_MAX72XX::FC16_HW, MATRIX_CS_PIN, MATRIX_MODULES);
Servo servo;

int currentIndex = 0;
bool autoMode = true;
unsigned long lastSwitchTime = 0;

int servoAngle = 90;
int servoTarget = 90;
unsigned long lastServoStepTime = 0;

char frames[3][28];
int frameCount = 0;
int frameIndex = 0;
char shownText[28];
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

int angleForChange(float percent) {
  if (percent > SCALE_PERCENT) {
    percent = SCALE_PERCENT;
  }
  if (percent < -SCALE_PERCENT) {
    percent = -SCALE_PERCENT;
  }
  return 90 + (int)(percent / SCALE_PERCENT * 90.0);
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

void addCustomChars() {
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

void makeFrames(const Currency& currency) {
  char rateText[24];
  char changeText[12];
  makeRateText(currency, rateText, sizeof(rateText));
  makeChangeText(currency, changeText, sizeof(changeText));

  snprintf(frames[0], sizeof(frames[0]), "%s %s", rateText, changeText);
  if (matrix.getTextColumns(frames[0]) <= MATRIX_WIDTH) {
    frameCount = 1;
  } else {
    strlcpy(frames[0], rateText, sizeof(frames[0]));
    strlcpy(frames[1], changeText, sizeof(frames[1]));
    frameCount = 2;
  }

  if (!serverOk) {
    strlcpy(frames[frameCount], "no link", sizeof(frames[0]));
    frameCount++;
  }

  framePause = carouselMs / frameCount;
  if (framePause > MAX_FRAME_PAUSE_MS) {
    framePause = MAX_FRAME_PAUSE_MS;
  }
}

void printCurrency(const Currency& currency) {
  Serial.printf("[%s] %s x%d %.2f %+.2f%%  ->  angle %d\r\n", modeName(), currency.code,
                currency.nominal, currency.rate, currency.changePercent, servoTarget);
}

void showCurrency(int index) {
  Currency& currency = currencies[index];
  makeFrames(currency);
  frameIndex = 0;
  showText(frames[0], framePause);
  servoTarget = angleForChange(currency.changePercent);
  printCurrency(currency);
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
  servoTarget = angleForChange(currencies[currentIndex].changePercent);
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
  float percent = currencies[currentIndex].changePercent;
  bool lightOn = true;
  if (fabs(percent) >= alertPercent) {
    lightOn = (millis() / BLINK_MS) % 2;
  }
  digitalWrite(LED_UP_PIN, (percent > 0 && lightOn) ? HIGH : LOW);
  digitalWrite(LED_DOWN_PIN, (percent < 0 && lightOn) ? HIGH : LOW);
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
  pinMode(LED_DOWN_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  servo.attach(SERVO_PIN);
  servo.write(servoAngle);

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
}
