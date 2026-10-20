# Валютный флюгер

Учебный проект по проектной деятельности, ДВФУ. Устройство на ESP32 раз в минуту берёт курсы валют с нашего сервера и показывает, насколько курс изменился за сутки: стрелка на сервоприводе поворачивается в одну сторону при росте и в другую при падении. Курс и изменение видны на светодиодной матрице, а при сильном скачке мигает светодиод.
 
## Папки

```
firmware/   прошивка ESP32 (C++, PlatformIO) и схема для Wokwi
server/     сервер на Python (FastAPI), отдаёт курсы устройству
web/        страница для пользователя (пока заготовка)
docs/       описание архитектуры, правила работы с git, презентации
```

## Как запустить на Windows

Нужны Python 3 и VS Code с расширениями PlatformIO IDE и Wokwi Simulator.

Сервер (первый раз):

```
cd server
python -m venv .venv
.venv\Scripts\python -m pip install -r requirements.txt
```

Запуск сервера:

```
cd server
.venv\Scripts\python -m uvicorn app.main:app --reload --host 0.0.0.0
```

Симуляция: в VS Code открыть папку `firmware`, собрать проект (PlatformIO: Build), потом F1 → «Wokwi: Start Simulator». Сервер в это время должен работать.

Подробнее про устройство — `firmware/README.md`, про сервер — `server/README.md`, как всё связано — `docs/architecture.md`.
