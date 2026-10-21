import pytest
from fastapi.testclient import TestClient

from app import cbr, main

client = TestClient(main.app)


@pytest.fixture(autouse=True)
def settings_file(tmp_path, monkeypatch):
    monkeypatch.setattr(main, "SETTINGS_FILE", tmp_path / "settings.json")

CBR_DATA = {
    "date": "2026-10-20",
    "rates": [{"code": "USD", "nominal": 1, "rate": 82.1, "change": -0.35, "history": [82.39, 82.1]}],
}


def broken_load():
    raise OSError("нет сети")


def test_health():
    assert client.get("/health").json() == {"status": "ok"}


def test_demo_when_cbr_unavailable(monkeypatch):
    monkeypatch.setattr(main, "cbr_rates", None)
    monkeypatch.setattr(cbr, "load_rates", broken_load)
    assert main.update_rates() is False
    data = client.get("/api/rates").json()
    assert data["source"] == "demo"
    assert [r["code"] for r in data["rates"]] == ["CNY", "USD", "EUR", "KZT", "MNT"]
    assert data["settings"] == {"carousel_s": 10, "alert_pct": 1.0}


def test_demo_history_matches_rate():
    for rate in main.DEMO_RATES["rates"]:
        assert len(rate["history"]) == 8
        assert rate["history"][-1] == rate["rate"]
    usd = main.DEMO_RATES["rates"][1]
    assert usd["change"] == -0.35


def test_cbr_rates(monkeypatch):
    monkeypatch.setattr(main, "cbr_rates", None)
    monkeypatch.setattr(cbr, "load_rates", lambda: CBR_DATA)
    assert main.update_rates() is True
    data = client.get("/api/rates").json()
    assert data["source"] == "cbr"
    assert data["date"] == "2026-10-20"
    assert data["rates"] == CBR_DATA["rates"]


def test_keeps_last_good_data(monkeypatch):
    monkeypatch.setattr(main, "cbr_rates", None)
    monkeypatch.setattr(cbr, "load_rates", lambda: CBR_DATA)
    main.update_rates()
    monkeypatch.setattr(cbr, "load_rates", broken_load)
    assert main.update_rates() is False
    data = client.get("/api/rates").json()
    assert data["source"] == "cbr"
    assert data["rates"] == CBR_DATA["rates"]


def test_default_settings():
    assert client.get("/api/settings").json() == {"carousel_s": 10, "alert_pct": 1.0, "currencies": cbr.CODES}


def test_save_settings():
    new = {"carousel_s": 15, "alert_pct": 0.5, "currencies": ["USD", "CNY"]}
    response = client.put("/api/settings", json=new)
    assert response.status_code == 200
    assert response.json()["currencies"] == ["CNY", "USD"]
    assert client.get("/api/settings").json()["carousel_s"] == 15
    data = client.get("/api/rates").json()
    assert data["settings"] == {"carousel_s": 15, "alert_pct": 0.5}
    assert [r["code"] for r in data["rates"]] == ["CNY", "USD"]
    assert len(client.get("/api/rates?all=true").json()["rates"]) == 5


def test_bad_settings():
    assert client.put("/api/settings", json={"carousel_s": 1, "alert_pct": 1.0, "currencies": ["USD"]}).status_code == 422
    assert client.put("/api/settings", json={"carousel_s": 10, "alert_pct": 1.0, "currencies": []}).status_code == 422
    assert client.put("/api/settings", json={"carousel_s": 10, "alert_pct": 1.0, "currencies": ["XXX"]}).status_code == 422


def test_demo_has_dates():
    for rate in main.DEMO_RATES["rates"]:
        assert len(rate["dates"]) == len(rate["history"])
        assert rate["dates"][-1] == "2026-10-05"


def test_device_status(monkeypatch):
    monkeypatch.setattr(main, "device_seen_at", None)
    assert client.get("/api/status").json()["device_online"] is False
    client.get("/api/rates", headers={"User-Agent": "ESP32HTTPClient"})
    status = client.get("/api/status").json()
    assert status["device_online"] is True
    assert status["device_seen_ago_s"] == 0


def test_index_page():
    response = client.get("/")
    assert response.status_code == 200
    assert "Валютный флюгер" in response.text


def test_long_history_only_for_site():
    device = client.get("/api/rates").json()["rates"][0]
    assert "history_long" not in device
    site = client.get("/api/rates?all=true").json()["rates"][0]
    assert len(site["history_long"]) == cbr.LONG_SIZE
    assert len(site["dates_long"]) == cbr.LONG_SIZE
    assert site["history_long"][-8:] == site["history"]
