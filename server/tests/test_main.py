from fastapi.testclient import TestClient

from app import cbr, main

client = TestClient(main.app)

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
