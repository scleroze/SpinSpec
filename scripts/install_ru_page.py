#!/usr/bin/env python3
"""Deploy SpinSpec Russian landing page without overwriting production app.py."""
from __future__ import annotations

import argparse
import datetime
import pathlib
import shutil
import urllib.request

ROOT = pathlib.Path("/opt/nmr-web")
BASE = "https://raw.githubusercontent.com/scleroze/SpinSpec/main"
APP = ROOT / "app/app.py"
INDEX = ROOT / "templates/index.html"
RU = ROOT / "templates/ru.html"
SITEMAP = ROOT / "static/sitemap.xml"

RUSSIAN_ROUTE = '''@app.get("/ru", response_class=HTMLResponse, include_in_schema=False)
@app.get("/ru/", response_class=HTMLResponse, include_in_schema=False)
def read_russian_page(request: Request) -> HTMLResponse:
    ip = get_client_ip(request)
    audit_event("page_open_ru", ip, {})
    return HTMLResponse((TEMPLATES_DIR / "ru.html").read_text(encoding="utf-8"))


'''

LANG_NAV = '''      <nav aria-label="Language" style="display:flex;justify-content:flex-end;gap:8px;margin-bottom:10px;font-size:13px;align-items:center;">
        <span lang="en" aria-current="page" style="font-weight:700;color:#1d4ed8;">EN</span>
        <span aria-hidden="true" style="color:#94a3b8;">|</span>
        <a href="/ru" lang="ru" hreflang="ru" style="font-weight:700;">RU · Русский</a>
      </nav>
'''

def get_file(path: str) -> str:
    with urllib.request.urlopen(f"{BASE}/{path}", timeout=25) as response:
        return response.read().decode("utf-8")

def replace_once(original: str, old: str, new: str, label: str) -> str:
    if original.count(old) != 1:
        raise RuntimeError(f"Cannot safely identify {label}; expected one match, got {original.count(old)}. Nothing changed.")
    return original.replace(old, new, 1)

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true", help="Validate only, do not change files")
    args = parser.parse_args()
    app = APP.read_text(encoding="utf-8")
    index = INDEX.read_text(encoding="utf-8")
    ru = get_file("templates/ru.html")
    sitemap = get_file("static/sitemap.xml")
    if 'lang="ru"' not in ru or "10.5281/zenodo.23696822" not in ru or "</html>" not in ru:
        raise RuntimeError("Downloaded Russian page failed basic validation")
    if 'https://nmrspec.org/ru' not in sitemap:
        raise RuntimeError("Downloaded sitemap lacks Russian page")
    if 'def read_russian_page(' not in app:
        app = replace_once(app, 'def get_client_ip(request: Request) -> str:', RUSSIAN_ROUTE + 'def get_client_ip(request: Request) -> str:', 'FastAPI insertion point')
    if '<html lang="ru">' in index:
        index = index.replace('<html lang="ru">', '<html lang="en">', 1)
    if 'hreflang="ru" href="https://nmrspec.org/ru"' not in index:
        canonical = '<link rel="canonical" href="https://nmrspec.org/" />'
        links = ('<link rel="alternate" hreflang="en" href="https://nmrspec.org/" />\n'
                 '  <link rel="alternate" hreflang="ru" href="https://nmrspec.org/ru" />\n'
                 '  <link rel="alternate" hreflang="x-default" href="https://nmrspec.org/" />')
        index = replace_once(index, canonical, canonical + '\n  ' + links, 'English canonical')
    if 'aria-label="Language"' not in index:
        anchor = '      <h1>SpinSpec</h1>'
        index = replace_once(index, anchor, LANG_NAV + anchor, 'language toggle')
    compile(app, str(APP), "exec")
    if index.count('href="/ru"') < 1 or not index.endswith("</html>\n"):
        raise RuntimeError("English page validation failed")
    updates = [(APP, app), (INDEX, index), (RU, ru), (SITEMAP, sitemap)]
    changed = [(path, data) for path, data in updates if not path.exists() or path.read_text(encoding="utf-8") != data]
    print(f"Validation succeeded. {len(changed)} file(s) need updating:")
    for path, _ in changed:
        print(" -", path)
    if args.dry_run:
        print("Dry run: nothing written.")
        return
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    for path, _ in changed:
        if path.exists():
            backup = path.with_name(path.name + f".bak-ru-{stamp}")
            shutil.copy2(path, backup)
            print("Backup:", backup)
    for path, data in changed:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(data, encoding="utf-8")
    compile(APP.read_text(encoding="utf-8"), str(APP), "exec")
    print("Files updated. Restart manually: systemctl restart nmr-web")

if __name__ == "__main__":
    main()
