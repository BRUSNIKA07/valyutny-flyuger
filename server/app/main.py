from fastapi import FastAPI

app = FastAPI(title="Валютный флюгер")

# Заглушка. Позже: курсы из API ЦБ РФ, кэш, расчёт изменения за сутки.
# rate — цена за nominal единиц валюты в рублях, как публикует ЦБ (у KZT и MNT nominal = 100).
DEMO = {
    "date": "2026-10-05",
    "rates": [
        {"code": "CNY", "nominal": 1, "rate": 11.50, "change": 0.42},
        {"code": "USD", "nominal": 1, "rate": 82.10, "change": -0.35},
        {"code": "EUR", "nominal": 1, "rate": 95.30, "change": 1.25},
        {"code": "KZT", "nominal": 100, "rate": 16.20, "change": -0.10},
        {"code": "MNT", "nominal": 100, "rate": 2.40, "change": -1.60},
    ],
    "settings": {"carousel_s": 10, "alert_pct": 1.0},
}


@app.get("/health")
def health():
    return {"status": "ok"}


@app.get("/api/rates")
def rates():
    return DEMO
