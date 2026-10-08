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
#define MATRIX_COLS   (MAX_DEVICES * 8)

// ---------- Матрица: кадры ----------
// Валюта показывается короткими кадрами по центру: "USD" -> "x100" (если nominal = 100) -> "82.10" -> "▲0.42%".
const uint16_t FRAME_SPEED     = 40;   // мс на шаг анимации; въезд и выезд по 8 шагов
const uint16_t FRAME_ANIM_MS   = 2 * 8 * FRAME_SPEED;
const long     FRAME_PAUSE_MIN = 800;  // сколько кадр стоит на месте, мс
const long     FRAME_PAUSE_MAX = 3000;
const uint16_t MODE_MSG_PAUSE  = 1000;

// Свои символы-стрелки. Столбцы слева направо, бит 0 — верхняя строка.
#define CH_UP   '\x01'
#define CH_DOWN '\x02'
const uint8_t arrowUp[]   = {5, 0x20, 0x38, 0x3E, 0x38, 0x20};  // ▲
const uint8_t arrowDown[] = {5, 0x02, 0x0E, 0x3E, 0x0E, 0x02};  // ▼

// Узкий шрифт (цифры шириной 3 точки) для чисел, которые не влезают в 32 точки.
// Формат MD_MAX72XX v2: 'F', 2, первый символ, последний символ, высота; затем ширина и столбцы.
MD_MAX72XX::fontType_t narrowFont[] PROGMEM = {
  'F', 2, 0, '%', 0, '9', 8,
  4, 0x41, 0x30, 0x0C, 0x43,  // %
  0, 0, 0, 0, 0,              // & ' ( ) * — не нужны
  3, 0x08, 0x1C, 0x08,        // +
  0,                          // ,
  3, 0x08, 0x08, 0x08,        // -
  1, 0x40,                    // .
  0,                          // /
  3, 0x7F, 0x41, 0x7F,        // 0
  3, 0x42, 0x7F, 0x40,        // 1
  3, 0x79, 0x49, 0x4F,        // 2
  3, 0x49, 0x49, 0x7F,        // 3
  3, 0x0F, 0x08, 0x7F,        // 4
  3, 0x4F, 0x49, 0x79,        // 5
  3, 0x7F, 0x49, 0x79,        // 6
  3, 0x01, 0x01, 0x7F,        // 7
  3, 0x7F, 0x49, 0x7F,        // 8
  3, 0x4F, 0x49, 0x7F,        // 9
};

// ---------- Настройки (позже придут с сервера) ----------
const float SCALE_PCT          = 2.0;    // шкала флюгера: от -2% до +2%
const unsigned long LONG_PRESS_MS = 800; // удержание кнопки = смена режима
// Приходят в settings ответа сервера (alert_pct, carousel_s); пока значения по умолчанию
float alertPct          = 1.0;    // порог мигания светодиода, %
unsigned long carouselMs = 10000; // смена валюты в авторежиме, мс

// ---------- Данные ----------
struct Currency {
  const char* code;
  int nominal;      // за сколько единиц валюты дана цена (1 или 100, как у ЦБ)
  float rate;       // рублей за nominal единиц
  float changePct;  // изменение за сутки, %
};

// Демо-значения для MVP, не реальные курсы
Currency currencies[] = {
  {"CNY",   1, 11.50,  0.42},
  {"USD",   1, 82.10, -0.35},
  {"EUR",   1, 95.30,  1.25},
  {"KZT", 100, 16.20, -0.10},
  {"MNT", 100,  2.40, -1.60},
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

char text[48];           // строка для лога в Serial
bool modeMsgPending = false;

char frames[4][16];      // кадры текущей валюты
int frameCount = 0;
int frameIdx = 0;
char frameText[16];      // то, что сейчас на матрице (MD_Parola хранит указатель)
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

// Показать один кадр по центру. Если не влезает в 32 точки: сначала убираем "%",
// потом берём узкий шрифт. Если не влезло и так — бегущая строка, но число не обрезаем.
void showFrame(const char* msg, uint16_t pause) {
  strncpy(frameText, msg, sizeof(frameText) - 1);
  frameText[sizeof(frameText) - 1] = '\0';

  matrix.setFont(nullptr);
  bool fits = matrix.getTextColumns(frameText) <= MATRIX_COLS;
  if (!fits) {
    char* pct = strchr(frameText, '%');
    if (pct) *pct = '\0';
    fits = matrix.getTextColumns(frameText) <= MATRIX_COLS;
  }
  if (!fits && strpbrk(frameText, "0123456789")) {
    matrix.setFont(narrowFont);
    fits = matrix.getTextColumns(frameText) <= MATRIX_COLS;
  }

  matrix.displayClear();
  if (fits) matrix.displayText(frameText, PA_CENTER, FRAME_SPEED, pause, PA_SCROLL_UP, PA_SCROLL_UP);
  else      matrix.displayText(frameText, PA_LEFT, FRAME_SPEED, pause, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
}

void buildFrames(const Currency &c) {
  frameCount = 0;
  snprintf(frames[frameCount++], sizeof(frames[0]), "%s", c.code);
  if (c.nominal != 1) snprintf(frames[frameCount++], sizeof(frames[0]), "x%d", c.nominal);
  snprintf(frames[frameCount++], sizeof(frames[0]), "%.2f", c.rate);
  if (c.changePct > 0)      snprintf(frames[frameCount++], sizeof(frames[0]), "%c%.2f%%", CH_UP, c.changePct);
  else if (c.changePct < 0) snprintf(frames[frameCount++], sizeof(frames[0]), "%c%.2f%%", CH_DOWN, -c.changePct);
  else                      snprintf(frames[frameCount++], sizeof(frames[0]), "0.00%%");

  // Весь цикл кадров укладываем в carousel_s
  framePause = constrain((long)(carouselMs / frameCount) - FRAME_ANIM_MS, FRAME_PAUSE_MIN, FRAME_PAUSE_MAX);
}

void showCurrency(int i) {
  Currency &c = currencies[i];
  buildFrames(c);
  frameIdx = 0;
  showFrame(frames[0], framePause);
  snprintf(text, sizeof(text), "%s x%d %.2f %+.2f%%", c.code, c.nominal, c.rate, c.changePct);
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
    showFrame(autoMode ? "Auto" : "Manual", MODE_MSG_PAUSE);  // "MANUAL" заглавными не влезает в 32 точки
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

  Serial.println("Valutny fluger: MVP started");
  showCurrency(current);
  lastSwitch = millis();
}

void loop() {
  updateButton();

  if (matrix.displayAnimate()) {     // кадр въехал, постоял и уехал
    unsigned long cycleMs = frameCount * (framePause + FRAME_ANIM_MS);
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
