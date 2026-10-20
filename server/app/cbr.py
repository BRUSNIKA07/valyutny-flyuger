import xml.etree.ElementTree as ET
from datetime import datetime, timedelta
from urllib.request import Request, urlopen

DAILY_URL = "https://www.cbr.ru/scripts/XML_daily.asp"
DYNAMIC_URL = "https://www.cbr.ru/scripts/XML_dynamic.asp?date_req1={}&date_req2={}&VAL_NM_RQ={}"
CODES = ["CNY", "USD", "EUR", "KZT", "MNT"]
HISTORY_SIZE = 8
RANGE_DAYS = 20
TIMEOUT_S = 10
USER_AGENT = "valyutny-flyuger/1.0"


def download(url):
    request = Request(url, headers={"User-Agent": USER_AGENT})
    with urlopen(request, timeout=TIMEOUT_S) as response:
        return response.read()


def to_float(text):
    return float(text.strip().replace(",", "."))


def to_date(text):
    return datetime.strptime(text, "%d.%m.%Y").date()


def parse_daily(data):
    root = ET.fromstring(data)
    day = to_date(root.get("Date"))
    valutes = {}
    for valute in root.findall("Valute"):
        code = valute.findtext("CharCode")
        valutes[code] = {
            "id": valute.get("ID"),
            "nominal": int(valute.findtext("Nominal")),
            "rate": to_float(valute.findtext("Value")),
        }
    return day, valutes


def parse_dynamic(data):
    root = ET.fromstring(data)
    records = []
    for record in root.findall("Record"):
        day = to_date(record.get("Date"))
        nominal = int(record.findtext("Nominal"))
        value = to_float(record.findtext("Value"))
        records.append((day, nominal, value))
    records.sort()
    return records


def change_percent(history):
    if len(history) < 2 or history[-2] == 0:
        return 0.0
    return round((history[-1] - history[-2]) / history[-2] * 100, 2)


def make_rate(code, valute, records, day):
    values = {}
    for record_day, nominal, value in records:
        values[record_day] = value * valute["nominal"] / nominal
    values[day] = valute["rate"]
    history = [round(values[d], 4) for d in sorted(values)][-HISTORY_SIZE:]
    return {
        "code": code,
        "nominal": valute["nominal"],
        "rate": history[-1],
        "change": change_percent(history),
        "history": history,
    }


def dynamic_url(valute_id, day):
    start = day - timedelta(days=RANGE_DAYS)
    return DYNAMIC_URL.format(start.strftime("%d/%m/%Y"), day.strftime("%d/%m/%Y"), valute_id)


def load_rates():
    day, valutes = parse_daily(download(DAILY_URL))
    rates = []
    for code in CODES:
        if code not in valutes:
            continue
        valute = valutes[code]
        records = parse_dynamic(download(dynamic_url(valute["id"], day)))
        rates.append(make_rate(code, valute, records, day))
    if not rates:
        raise ValueError("в ответе ЦБ нет нужных валют")
    return {"date": day.isoformat(), "rates": rates}
