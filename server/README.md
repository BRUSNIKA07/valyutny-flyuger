# server

Сервер на Python и FastAPI. Отдаёт устройству и сайту курсы валют и настройки по адресу `/api/rates`. Курсы и историю за 8 последних дней сервер берёт у ЦБ РФ по HTTPS раз в час и хранит в памяти. Если ЦБ недоступен, отдаёт последние данные, а если их ещё нет — демо (`"source": "demo"`).

## Папки

```
app/main.py         сам сервер
app/cbr.py          загрузка и разбор курсов ЦБ
requirements.txt    нужные библиотеки
tests/              тесты (pytest, без выхода в сеть)
```

## Как запустить на Windows

Первый раз:

```
cd server
python -m venv .venv
.venv\Scripts\python -m pip install -r requirements.txt
```

Потом каждый раз:

```
cd server
.venv\Scripts\python -m uvicorn app.main:app --reload --host 0.0.0.0
```

`--host 0.0.0.0` нужен, чтобы сервер был виден из симуляции Wokwi и с настоящей платы в той же сети.

Тесты:

```
cd server
.venv\Scripts\python -m pytest
```

Проверить, что работает: открыть http://127.0.0.1:8000/api/rates. Все запросы с описанием — на http://127.0.0.1:8000/docs.

Симуляция устройства запускается из папки `firmware`, как написано в `firmware/README.md`.
