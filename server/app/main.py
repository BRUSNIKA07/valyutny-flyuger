import threading
import time
from contextlib import asynccontextmanager

from fastapi import FastAPI

from app import cbr

UPDATE_S = 3600
RETRY_S = 300
SETTINGS = {"carousel_s": 10, "alert_pct": 1.0}


def demo_rate(code, nominal, history):
    return {
        "code": code,
        "nominal": nominal,
        "rate": history[-1],
        "change": cbr.change_percent(history),
        "history": history,
    }


DEMO_RATES = {
    "date": "2026-10-05",
    "rates": [
        demo_rate("CNY", 1, [11.38, 11.40, 11.42, 11.41, 11.44, 11.46, 11.45, 11.50]),
        demo_rate("USD", 1, [83.40, 83.10, 82.90, 82.70, 82.60, 82.50, 82.39, 82.10]),
        demo_rate("EUR", 1, [93.80, 94.00, 94.20, 94.10, 94.30, 94.60, 94.12, 95.30]),
        demo_rate("KZT", 100, [16.40, 16.38, 16.35, 16.30, 16.28, 16.25, 16.22, 16.20]),
        demo_rate("MNT", 100, [2.48, 2.47, 2.47, 2.46, 2.45, 2.45, 2.44, 2.40]),
    ],
}

cbr_rates = None


def update_rates():
    global cbr_rates
    try:
        cbr_rates = cbr.load_rates()
    except Exception as error:
        print(f"[cbr] ошибка загрузки: {error}", flush=True)
        return False
    print(f"[cbr] курсы на {cbr_rates['date']} загружены", flush=True)
    return True


def update_loop():
    while True:
        if update_rates():
            time.sleep(UPDATE_S)
        else:
            time.sleep(RETRY_S)


@asynccontextmanager
async def lifespan(app):
    threading.Thread(target=update_loop, daemon=True).start()
    yield


app = FastAPI(title="Валютный флюгер", lifespan=lifespan)


def current_rates():
    if cbr_rates is None:
        return {"date": DEMO_RATES["date"], "source": "demo", "rates": DEMO_RATES["rates"], "settings": SETTINGS}
    return {"date": cbr_rates["date"], "source": "cbr", "rates": cbr_rates["rates"], "settings": SETTINGS}


@app.get("/health")
def health():
    return {"status": "ok"}


@app.get("/api/rates")
def rates():
    return current_rates()
