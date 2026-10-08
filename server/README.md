# server — Python + FastAPI

Забирает курсы у ЦБ РФ, считает изменение за сутки в %, отдаёт устройству и сайту.

## Запуск
cd server
python -m venv .venv
.venv\Scripts\activate        (Linux/Mac: source .venv/bin/activate)
pip install -r requirements.txt
uvicorn app.main:app --reload

Документация API: http://127.0.0.1:8000/docs
