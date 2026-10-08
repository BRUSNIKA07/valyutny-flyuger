# docs

Документы по проекту «Валютный флюгер»: как устроена система, как мы работаем с git, и презентации к сдаче работ. Сам проект — это ESP32 со стрелкой на сервоприводе, которая показывает изменение курса валют за сутки.

## Папки

```
architecture.md    как связаны устройство, сервер и сайт, формат ответа сервера
CONTRIBUTING.md    как работаем с git
presentations/     презентации к каждой работе (rabota-1.pptx или PDF)
```

## Как запустить на Windows

Сервер:

```
cd server
.venv\Scripts\python -m uvicorn app.main:app --reload --host 0.0.0.0
```

Симуляция: в VS Code открыть папку `firmware`, собрать проект и запустить F1 → «Wokwi: Start Simulator». Подробнее в `server/README.md` и `firmware/README.md`.
