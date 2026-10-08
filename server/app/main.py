from fastapi import FastAPI

app = FastAPI(title="Валютный флюгер")

# Заглушка. Позже: курсы из API ЦБ РФ, кэш, расчёт изменения за сутки.
DEMO = {
    "date": "2026-10-05",
    "rates": [
        {"code": "CNY", "rate": 11.50, "change": 0.42},
        {"code": "USD", "rate": 82.10, "change": -0.35},
        {"code": "EUR", "rate": 95.30, "change": 1.25},
        {"code": "KZT", "rate": 16.20, "change": -0.10},
        {"code": "MNT", "rate": 2.40, "change": -1.60},
    ],
    "settings": {"carousel_s": 10, "alert_pct": 1.0},
}


@app.get("/health")
def health():
    return {"status": "ok"}


@app.get("/api/rates")
def rates():
    return DEMO
