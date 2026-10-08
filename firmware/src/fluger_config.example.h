#pragma once

// Настройки сети. Значения ниже — для симулятора Wokwi.
// Для настоящей платы скопируйте файл в fluger_config.h (он не попадает в git) и впишите свои.
#define WIFI_SSID     "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define WIFI_CHANNEL  6      // у Wokwi-GUEST канал 6: без сканирования подключение быстрее; для своей сети — 0 (автопоиск)
// host.wokwi.internal — это компьютер, на котором запущен Wokwi (в Wokwi для VS Code работает без настроек)
#define SERVER_URL    "http://host.wokwi.internal:8000/api/rates"
