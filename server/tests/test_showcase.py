import json
from datetime import date

from app import cbr, mirror, showcase

MIRROR_DAY = json.dumps({
    "Date": "2026-10-20T11:30:00+03:00",
    "Valute": {
        "USD": {"CharCode": "USD", "Nominal": 1, "Value": 82.1},
        "KZT": {"CharCode": "KZT", "Nominal": 100, "Value": 16.2},
    },
})


def broken():
    raise OSError("нет сети")


def test_mirror_parse_day():
    day, valutes = mirror.parse_day(MIRROR_DAY.encode())
    assert day == date(2026, 10, 20)
    assert valutes["USD"] == {"nominal": 1, "rate": 82.1}
    assert valutes["KZT"]["nominal"] == 100


def test_build_with_demo(tmp_path, monkeypatch):
    monkeypatch.setattr(cbr, "load_rates", broken)
    monkeypatch.setattr(mirror, "load_rates", broken)
    assert showcase.build(tmp_path) == "demo"
    data = json.loads((tmp_path / "data" / "rates.json").read_text(encoding="utf-8"))
    assert data["source"] == "demo"
    assert len(data["rates"]) == 5
    assert "history_long" in data["rates"][0]
    assert "window.SHOWCASE = true" in (tmp_path / "index.html").read_text(encoding="utf-8")
    assert json.loads((tmp_path / "data" / "settings.json").read_text(encoding="utf-8"))["alert_pct"] == 1.0


def test_build_uses_mirror_when_cbr_fails(tmp_path, monkeypatch):
    monkeypatch.setattr(cbr, "load_rates", broken)
    monkeypatch.setattr(mirror, "load_rates", lambda: {"date": "2026-10-20", "rates": [{"code": "USD"}]})
    assert showcase.build(tmp_path) == "cbr-mirror"
