#pragma once

// Настройки сети. Значения ниже — для симулятора Wokwi.
// Для настоящей платы скопируйте файл в fluger_config.h (он не попадает в git) и впишите свои.
#define WIFI_SSID     "Wokwi-GUEST"
#define WIFI_PASSWORD ""
// host.wokwi.internal — это компьютер, на котором запущен Wokwi (нужен [net] gateway = true в wokwi.toml)
#define SERVER_URL    "http://host.wokwi.internal:8000/api/rates"
