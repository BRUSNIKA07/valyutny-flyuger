# Как работаем с git

В репозитории пока работает один человек, поэтому без pull request и ревью: сделал задачу, закоммитил, запушил.

## Папки

```
firmware/   прошивка ESP32
server/     сервер на FastAPI
web/        страница
docs/       документы
```

Ветки называем по папке: `firmware/...`, `server/...`, `web/...`, `docs/...`. Мелочи можно править сразу в `main`.

## Порядок работы

1. `git checkout -b server/что-делаю`
2. Делаю задачу, проверяю, что `pio run` собирается и сервер запускается.
3. Коммит с понятным сообщением, например `server: добавлен /api/rates`, и `git push origin server/что-делаю`.
4. `git checkout main`, `git merge server/что-делаю`, `git push origin main`.

## Как запустить на Windows

```
cd server
.venv\Scripts\python -m uvicorn app.main:app --reload --host 0.0.0.0
```

Симуляция: в VS Code открыть папку `firmware`, собрать проект, F1 → «Wokwi: Start Simulator».
