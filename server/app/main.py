import json
import threading
import time
from contextlib import asynccontextmanager
from datetime import date, timedelta
from pathlib import Path

from fastapi import FastAPI, Request
from fastapi.responses import FileResponse
from pydantic import BaseModel, Field, field_validator

from app import cbr

UPDATE_S = 3600
RETRY_S = 300
DEVICE_ONLINE_S = 180
SETTINGS_FILE = Path(__file__).resolve().parent.parent / "settings.json"
WEB_PAGE = Path(__file__).resolve().parent.parent.parent / "web" / "index.html"
DEFAULT_SETTINGS = {"carousel_s": 10, "alert_pct": 1.0, "currencies": list(cbr.CODES)}


class Settings(BaseModel):
    carousel_s: int = Field(ge=3, le=60)
    alert_pct: float = Field(ge=0.1, le=10)
    currencies: list[str] = Field(min_length=1)

    @field_validator("currencies")
    @classmethod
    def check_currencies(cls, codes):
        for code in codes:
            if code not in cbr.CODES:
                raise ValueError(f"неизвестная валюта {code}")
        return [code for code in cbr.CODES if code in codes]


def demo_dates(count):
    days = []
    day = date(2026, 10, 5)
    while len(days) < count:
        if day.weekday() < 5:
            days.append(day.isoformat())
        day -= timedelta(days=1)
    return list(reversed(days))


def demo_rate(code, nominal, history):
    return {
        "code": code,
        "nominal": nominal,
        "rate": history[-1],
        "change": cbr.change_percent(history),
        "history": history,
        "dates": demo_dates(len(history)),
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
cbr_loaded_at = None
device_seen_at = None


def load_settings():
    try:
        data = json.loads(SETTINGS_FILE.read_text(encoding="utf-8"))
        return Settings(**data).model_dump()
    except Exception:
        return dict(DEFAULT_SETTINGS)


def save_settings(settings):
    SETTINGS_FILE.write_text(json.dumps(settings, ensure_ascii=False, indent=2), encoding="utf-8")


def update_rates():
    global cbr_rates, cbr_loaded_at
    try:
        cbr_rates = cbr.load_rates()
    except Exception as error:
        print(f"[cbr] ошибка загрузки: {error}", flush=True)
        return False
    cbr_loaded_at = time.time()
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


def current_rates(all_currencies=False):
    settings = load_settings()
    if cbr_rates is None:
        data, source = DEMO_RATES, "demo"
    else:
        data, source = cbr_rates, "cbr"
    rates = data["rates"]
    if not all_currencies:
        rates = [rate for rate in rates if rate["code"] in settings["currencies"]]
    return {
        "date": data["date"],
        "source": source,
        "rates": rates,
        "settings": {"carousel_s": settings["carousel_s"], "alert_pct": settings["alert_pct"]},
    }


def seconds_ago(moment):
    if moment is None:
        return None
    return int(time.time() - moment)


@app.get("/")
def index():
    return FileResponse(WEB_PAGE)


@app.get("/health")
def health():
    return {"status": "ok"}


@app.get("/api/rates")
def rates(request: Request, all: bool = False):
    global device_seen_at
    if "ESP32" in request.headers.get("user-agent", ""):
        device_seen_at = time.time()
    return current_rates(all)


@app.get("/api/settings")
def get_settings():
    return load_settings()


@app.put("/api/settings")
def put_settings(settings: Settings):
    data = settings.model_dump()
    save_settings(data)
    return data


@app.get("/api/status")
def status():
    device_ago = seconds_ago(device_seen_at)
    return {
        "source": "demo" if cbr_rates is None else "cbr",
        "cbr_loaded_ago_s": seconds_ago(cbr_loaded_at),
        "device_seen_ago_s": device_ago,
        "device_online": device_ago is not None and device_ago <= DEVICE_ONLINE_S,
    }
