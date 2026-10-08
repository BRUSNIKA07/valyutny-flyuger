#include <Arduino.h>

// ===== Валютный флюгер — MVP (Работа №1) =====
// ESP32 + сервопривод (флюгер) + матрица MAX7219 8x32 + 2 светодиода + кнопка.
// Сети пока нет: курсы — демо-значения, зашитые в прошивку.
// На следующих этапах массив currencies будет заполняться ответом сервера.

#include <ESP32Servo.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

// ---------- Пины ----------
#define SERVO_PIN     13
#define LED_UP_PIN    26   // зелёный: рост
#define LED_DOWN_PIN  27   // красный: падение
#define BUTTON_PIN    14   // кнопка на GND, внутренняя подтяжка
#define CS_PIN         5   // матрица: DIN=23, CLK=18 (аппаратный SPI), CS=5

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES   4    // 4 модуля 8x8 = 8x32

// ---------- Настройки (позже придут с сервера) ----------
const float SCALE_PCT          = 2.0;    // шкала флюгера: от -2% до +2%
const float ALERT_PCT          = 1.0;    // порог мигания светодиода
const unsigned long CAROUSEL_MS = 10000; // смена валюты в авторежиме
const unsigned long LONG_PRESS_MS = 800; // удержание кнопки = смена режима

// ---------- Данные ----------
struct Currency {
  const char* code;
  float rate;       // рублей за 1 единицу (KZT, MNT — за 100 единиц)
  float changePct;  // изменение за сутки, %
};

// Демо-значения для MVP, не реальные курсы
Currency currencies[] = {
  {"CNY", 11.50,  0.42},
  {"USD", 82.10, -0.35},
  {"EUR", 95.30,  1.25},
  {"KZT", 16.20, -0.10},
  {"MNT",  2.40, -1.60},
};
const int COUNT = sizeof(currencies) / sizeof(currencies[0]);

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

char text[48];
bool modeMsgPending = false;

bool btnPrev = HIGH;
unsigned long pressStart = 0;
bool longHandled = false;

// Перевод изменения в процентах в угол сервопривода (0..180, 90 = ноль)
int angleFor(float pct) {
  if (pct >  SCALE_PCT) pct =  SCALE_PCT;
  if (pct < -SCALE_PCT) pct = -SCALE_PCT;
  return 90 + (int)(pct / SCALE_PCT * 90.0);
}

void showText(const char* msg) {
  matrix.displayClear();
  matrix.displayText(msg, PA_CENTER, 40, 800, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
}

void showCurrency(int i) {
  Currency &c = currencies[i];
  snprintf(text, sizeof(text), "%s %.2f %+.2f%%", c.code, c.rate, c.changePct);
  showText(text);
  targetAngle = angleFor(c.changePct);
  Serial.printf("[%s] %s  ->  angle %d\n", autoMode ? "AUTO" : "MANUAL", text, targetAngle);
}

void nextCurrency() {
  current = (current + 1) % COUNT;
  lastSwitch = millis();
  showCurrency(current);
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
  if (fabs(pct) >= ALERT_PCT) on = (millis() / 300) % 2;
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
    showText(autoMode ? "AUTO" : "MANUAL");
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

  Serial.println("Valutny fluger: MVP started");
  showCurrency(current);
  lastSwitch = millis();
}

void loop() {
  updateButton();

  if (autoMode && millis() - lastSwitch >= CAROUSEL_MS) nextCurrency();

  if (matrix.displayAnimate()) {     // строка доехала до конца
    if (modeMsgPending) {            // после AUTO/MANUAL вернуть валюту
      modeMsgPending = false;
      showCurrency(current);
    } else {
      matrix.displayReset();         // повторить бегущую строку
    }
  }

  updateNeedle();
  updateLeds();
}
