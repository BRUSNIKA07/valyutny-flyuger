import json
import sys
from datetime import datetime, timezone
from pathlib import Path

from app import cbr, main, mirror

WEB_DIR = Path(__file__).resolve().parent.parent.parent / "web"
FLAG = "<script>window.SHOWCASE = true;</script>\n"


def load():
    for source, loader in (("cbr", cbr.load_rates), ("cbr-mirror", mirror.load_rates)):
        try:
            data = loader()
            print(f"[showcase] курсы из {source} на {data['date']}", flush=True)
            return source, data
        except Exception as error:
            print(f"[showcase] {source} недоступен: {error}", flush=True)
    return "demo", main.DEMO_RATES


def build(out_dir):
    out = Path(out_dir)
    (out / "data").mkdir(parents=True, exist_ok=True)
    source, data = load()
    rates = {
        "date": data["date"],
        "source": source,
        "updated": datetime.now(timezone.utc).isoformat(timespec="minutes"),
        "rates": data["rates"],
        "settings": {"carousel_s": main.DEFAULT_SETTINGS["carousel_s"], "alert_pct": main.DEFAULT_SETTINGS["alert_pct"]},
    }
    (out / "data" / "rates.json").write_text(json.dumps(rates, ensure_ascii=False), encoding="utf-8")
    (out / "data" / "settings.json").write_text(json.dumps(main.DEFAULT_SETTINGS, ensure_ascii=False), encoding="utf-8")
    page = (WEB_DIR / "index.html").read_text(encoding="utf-8")
    page = page.replace("<script>\n", FLAG + "<script>\n", 1)
    (out / "index.html").write_text(page, encoding="utf-8")
    (out / ".nojekyll").write_text("", encoding="utf-8")
    return source


if __name__ == "__main__":
    build(sys.argv[1] if len(sys.argv) > 1 else "../_site")
