import json
from datetime import date, timedelta
from urllib.error import HTTPError

from app import cbr

DAILY_URL = "https://www.cbr-xml-daily.ru/daily_json.js"
ARCHIVE_URL = "https://www.cbr-xml-daily.ru/archive/{:%Y/%m/%d}/daily_json.js"


def parse_day(data):
    root = json.loads(data)
    day = date.fromisoformat(root["Date"][:10])
    valutes = {}
    for code, item in root["Valute"].items():
        valutes[code] = {"nominal": int(item["Nominal"]), "rate": float(item["Value"])}
    return day, valutes


def load_rates():
    day, valutes = parse_day(cbr.download(DAILY_URL))
    records = {code: [] for code in cbr.CODES}
    seen = set()
    for back in range(1, cbr.RANGE_DAYS + 1):
        try:
            past_day, past = parse_day(cbr.download(ARCHIVE_URL.format(day - timedelta(days=back))))
        except HTTPError:
            continue
        if past_day in seen or past_day >= day:
            continue
        seen.add(past_day)
        for code in cbr.CODES:
            if code in past:
                records[code].append((past_day, past[code]["nominal"], past[code]["rate"]))
        if len(seen) >= cbr.LONG_SIZE:
            break
    rates = []
    for code in cbr.CODES:
        if code in valutes:
            rates.append(cbr.make_rate(code, valutes[code], sorted(records[code]), day))
    if not rates:
        raise ValueError("в ответе зеркала нет нужных валют")
    return {"date": day.isoformat(), "rates": rates}
