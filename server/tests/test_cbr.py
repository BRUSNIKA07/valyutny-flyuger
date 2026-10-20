from datetime import date
from pathlib import Path

import pytest

from app import cbr

DATA = Path(__file__).parent / "data"


def read_sample(name):
    return (DATA / name).read_bytes()


def test_parse_daily():
    day, valutes = cbr.parse_daily(read_sample("xml_daily.xml"))
    assert day == date(2026, 10, 20)
    assert valutes["USD"] == {"id": "R01235", "nominal": 1, "rate": 81.9876}
    assert valutes["KZT"] == {"id": "R01335", "nominal": 100, "rate": 16.1234}
    assert valutes["MNT"]["nominal"] == 100
    assert set(cbr.CODES) <= set(valutes)


def test_parse_dynamic():
    records = cbr.parse_dynamic(read_sample("xml_dynamic_usd.xml"))
    assert len(records) == 15
    assert records[0] == (date(2026, 9, 30), 1, 83.1)
    assert records[-1] == (date(2026, 10, 20), 1, 81.9876)


def test_change_percent():
    assert cbr.change_percent([82.39, 82.10]) == -0.35
    assert cbr.change_percent([100.0, 101.0]) == 1.0
    assert cbr.change_percent([82.10]) == 0.0


def test_make_rate_takes_last_8():
    day, valutes = cbr.parse_daily(read_sample("xml_daily.xml"))
    records = cbr.parse_dynamic(read_sample("xml_dynamic_usd.xml"))
    rate = cbr.make_rate("USD", valutes["USD"], records, day)
    assert rate["history"] == [82.55, 82.5, 82.4, 82.35, 82.3, 82.25, 82.39, 81.9876]
    assert rate["rate"] == rate["history"][-1] == 81.9876
    assert rate["change"] == -0.49
    assert rate["nominal"] == 1


def test_make_rate_adds_today_if_missing():
    valute = {"id": "R01335", "nominal": 100, "rate": 16.2}
    records = [(date(2026, 10, 16), 100, 16.0), (date(2026, 10, 17), 100, 16.1)]
    rate = cbr.make_rate("KZT", valute, records, date(2026, 10, 20))
    assert rate["history"] == [16.0, 16.1, 16.2]
    assert rate["change"] == 0.62


def test_load_rates_without_network(monkeypatch):
    urls = []

    def fake_download(url):
        urls.append(url)
        if url == cbr.DAILY_URL:
            return read_sample("xml_daily.xml")
        return read_sample("xml_dynamic_usd.xml")

    monkeypatch.setattr(cbr, "download", fake_download)
    result = cbr.load_rates()
    assert result["date"] == "2026-10-20"
    assert [r["code"] for r in result["rates"]] == cbr.CODES
    assert "date_req1=30/09/2026&date_req2=20/10/2026&VAL_NM_RQ=R01235" in urls[2]
    usd = result["rates"][1]
    assert usd["rate"] == 81.9876
    assert len(usd["history"]) == 8


def test_load_rates_without_codes(monkeypatch):
    empty = '<?xml version="1.0" encoding="windows-1251"?><ValCurs Date="20.10.2026"></ValCurs>'
    monkeypatch.setattr(cbr, "download", lambda url: empty.encode("cp1251"))
    with pytest.raises(ValueError):
        cbr.load_rates()
