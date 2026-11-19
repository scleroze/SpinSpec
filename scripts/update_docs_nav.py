#!/usr/bin/env python3
"""Install multilingual documentation/navigation on test SpinSpec only.
Fetches the three product pages and shared CSS from repository; does not copy
old index.html or app.py from GitHub, so local calculation changes survive.
"""
import re
import sys
import urllib.request
from pathlib import Path
from datetime import datetime

BASE=Path("/opt/spinspec-v1.1-test")
ROOT="https://raw.githubusercontent.com/scleroze/SpinSpec/main/"
MARK="SPINSPEC_DOCS_NAV_V11"
def download(path):
    with urllib.request.urlopen(ROOT+path,timeout=25) as r:
        return r.read().decode("utf-8")
def save(path,s):
    old=path.read_text(encoding="utf-8") if path.exists() else None
    if old==s:
        print("Unchanged:",path)
        return
    if old is not None:
        stamp=datetime.now().strftime("%Y%m%d-%H%M%S")
        backup=path.with_name(path.name+".before-doc-nav-"+stamp+".bak")
        backup.write_text(old,encoding="utf-8")
        print("Backup:",backup)
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(s,encoding="utf-8")
    print("Updated:",path)
def once(s,a,b,description):
    if s.count(a)!=1: raise ValueError(description+": expected one anchor, got "+str(s.count(a)))
    return s.replace(a,b)
def main(path):
    if path.resolve()!=BASE:raise SystemExit("Safety: only test root /opt/spinspec-v1.1-test")
    pages={p:download(p) for p in ["static/about.css","templates/about_en.html","templates/about_tr.html","templates/ru.html"]}
    for n in ("templates/about_en.html","templates/about_tr.html","templates/ru.html"):
        if 'class="sitebar"' not in pages[n] or 'href="/static/about.css"' not in pages[n]:
            raise ValueError("Unexpected remote document "+n)
    app_path=BASE/"app/app.py"
    index_path=BASE/"templates/index.html"
    tutorial_path=BASE/"static/tutorial.html"
    app=app_path.read_text(encoding="utf-8")
    index=index_path.read_text(encoding="utf-8")
    tutorial=tutorial_path.read_text(encoding="utf-8")
    if "BASE_DIR = Path(\"/opt/spinspec-v1.1-test\")" not in app or "8001" not in app:
        raise ValueError("Test server config missing; refusing to patch app.py")
    if MARK not in app:
        anchor='@app.get("/dev-spectrum", response_class=HTMLResponse)'
        block='''# SPINSPEC_DOCS_NAV_V11
@app.get("/about", response_class=HTMLResponse, include_in_schema=False)
@app.get("/about/", response_class=HTMLResponse, include_in_schema=False)
def about_en_page() -> HTMLResponse:
    return HTMLResponse((TEMPLATES_DIR / "about_en.html").read_text(encoding="utf-8"))

@app.get("/tr", response_class=HTMLResponse, include_in_schema=False)
@app.get("/tr/", response_class=HTMLResponse, include_in_schema=False)
def about_tr_page() -> HTMLResponse:
    return HTMLResponse((TEMPLATES_DIR / "about_tr.html").read_text(encoding="utf-8"))


'''
        app=once(app,anchor,block+anchor,"Python app route")
    if MARK not in index:
        # EN/RU at calculator top formerly mixed language UI and document links.
        pat=r'<nav aria-label="Language"[\s\S]*?</nav>'
        match=re.search(pat,index)
        if not match:raise ValueError("Calculator language bar not found")
        navigation='''<!-- SPINSPEC_DOCS_NAV_V11: documentation links, not UI language selector -->
      <nav aria-label="SpinSpec documentation" style="display:flex;flex-wrap:wrap;align-items:center;gap:9px 13px;margin-bottom:12px;font-size:13px;padding:8px 10px;background:#edf4ff;border:1px solid #d8dee6;border-radius:9px">
        <strong style="font-weight:700;color:#1f2937">About:</strong>
        <a href="/about" title="Software description in English">EN</a>
        <a href="/ru" lang="ru" title="Описание программы по-русски">RU</a>
        <a href="/tr" lang="tr" title="Türkçe yazılım tanıtımı">TR</a>
        <span style="color:#94a3b8" aria-hidden="true">|</span>
        <a href="/tutorial" title="Illustrated guide in English">Tutorial</a>
      </nav>'''
        index=index[:match.start()]+navigation+index[match.end():]
        # Relative links make local tunnel work; public URLs still work on deployment.
        index=index.replace('href="https://nmrspec.org/tutorial"','href="/tutorial"')
        anchor='<h2>Online NMR Spectrum Simulation</h2>'
        update='''<div style="border:1px solid #c7d8f5;background:#eff6ff;border-radius:12px;padding:14px 18px;margin-bottom:20px;text-align:left">
        <strong style="color:#1d4ed8">SpinSpec 1.1 · November 2026</strong>
        <p style="margin:7px 0 6px">New: spin-group spectra with signed interference; automatic group windows; downloadable transition tables (CSV); organized molecular examples and improved layout.</p>
        <a href="/about">What's new and how it works</a> ·
        <a href="/ru">Описание по-русски</a> ·
        <a href="/tr">Türkçe açıklama</a>
      </div>
      '''
        index=once(index,anchor,update+anchor,"welcome panel")
    if MARK not in tutorial:
        # Keep all original tutorial figures, sections, content, and export documentation.
        # Replace only old isolated CSS and top bar.
        tutorial,n=re.subn(r'<style>[\s\S]*?</style>', '<link rel="stylesheet" href="/static/about.css">',tutorial,count=1)
        if n!=1:raise ValueError("Tutorial stylesheet not located")
        tutorial,n=re.subn(r'<body>\s*<header>[\s\S]*?</header>', '''<body>
<!-- SPINSPEC_DOCS_NAV_V11 -->
<header class="sitebar"><div class="site-container sitebar-inner">
<a class="site-logo" href="/">Spin<span>Spec</span></a>
<nav class="site-nav" aria-label="Site navigation">
<a href="/">Calculator</a><a href="/about">About (EN)</a><a href="/ru">Описание (RU)</a><a href="/tr">Tanıtım (TR)</a>
</nav></div></header>
<div class="site-container">
<div class="guide-header"><h1>SpinSpec Web Tutorial</h1>
<p class="subtitle">Illustrated guide to spin-system parameters, calculations, group-resolved spectra and numerical data export.</p></div>
</div>''',tutorial,count=1)
        if n!=1:raise ValueError("Tutorial page header not located")
        tutorial=once(tutorial,'<main>','<main class="site-container guide-content">',"tutorial main")
        # Existing footer gets standard semantics, without removing links/images.
        tutorial=tutorial.replace('<footer>','<footer class="site-footer"><div class="site-container">',1)
        tutorial=tutorial.replace('</footer>','</div></footer>',1)
    compile(app,str(app_path),"exec")
    if not all('</html>' in x for x in (index,tutorial)):raise ValueError("Invalid output")
    # Only now mutate files, following validation and remote downloads.
    for p,s in pages.items():save(BASE/p,s)
    save(app_path,app)
    save(index_path,index)
    save(tutorial_path,tutorial)
    print("Multilingual docs installed in TEST; restart only test FastAPI (8001).")
    print("Verify GET /about, /ru, /tr, /tutorial, / and browser nav.")
if __name__=="__main__":
    if len(sys.argv)!=2:raise SystemExit("Usage: python3 update_docs_nav.py /opt/spinspec-v1.1-test")
    main(Path(sys.argv[1]))
