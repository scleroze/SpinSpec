#!/usr/bin/env python3
"""Install SpinSpec example catalog and set files in TEST deployment only.

Usage: python3 update_example_catalog.py /opt/spinspec-v1.1-test
No service restart performed. Backups of overwritten files are retained.
"""
import json
import re
import sys
import urllib.parse
import urllib.request
from datetime import datetime
from pathlib import Path

ROOT = "https://raw.githubusercontent.com/scleroze/SpinSpec/main/examples/"
MARKER = "# SPINSPEC_CLASSIFIED_EXAMPLE_CATALOG"
JS_MARKER = "// SPINSPEC_CLASSIFIED_EXAMPLE_CATALOG"

def get(rel):
    with urllib.request.urlopen(ROOT + urllib.parse.quote(rel), timeout=25) as r:
        return r.read().decode("utf-8")

def backup_write(path, content):
    original = path.read_text(encoding="utf-8") if path.exists() else None
    if original == content:
        print("Unchanged:", path)
        return
    if original is not None:
        suffix = datetime.now().strftime("%Y%m%d-%H%M%S")
        b = path.with_name(path.name + ".before-catalog-" + suffix + ".bak")
        b.write_text(original, encoding="utf-8")
        print("Backup:", b)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    print("Updated:", path)

def main(root):
    root = root.resolve()
    if root != Path("/opt/spinspec-v1.1-test"):
        raise SystemExit("Safety check: only /opt/spinspec-v1.1-test allowed")
    catalog_raw = get("catalog.json")
    catalog = json.loads(catalog_raw)
    files = [item["filename"] for group in catalog["groups"] for item in group["items"]]
    if len(files) != 17 or len(set(files)) != 17:
        raise RuntimeError("Unexpected catalog manifest; refusing update")
    downloaded = {name: get(name) for name in files}
    for name, raw in downloaded.items():
        if not raw.startswith("#") or "\nGraph " not in raw:
            raise RuntimeError("Malformed .set: " + name)

    app_path = root / "app/app.py"
    html_path = root / "templates/index.html"
    app = app_path.read_text(encoding="utf-8")
    html = html_path.read_text(encoding="utf-8")

    if MARKER not in app:
        start = app.index('@app.get("/api/examples")')
        end = app.index('@app.get("/api/examples/{filename}")', start)
        replacement = '''@app.get("/api/examples")
def list_examples() -> dict:
    ''' + MARKER + '''
    import json as _catalog_json
    catalog_file = EXAMPLES_DIR / "catalog.json"
    known = {}
    groups = []
    if catalog_file.exists():
        catalog = _catalog_json.loads(catalog_file.read_text(encoding="utf-8"))
        for group in catalog.get("groups", []):
            group_name = str(group.get("category", "Other"))
            groups.append(group_name)
            for entry in group.get("items", []):
                known[entry["filename"]] = {
                    "filename": entry["filename"],
                    "label": entry["label"],
                    "category": group_name,
                    "model": entry.get("model", "reference"),
                }
    examples = [known[name] for name in known
                if (EXAMPLES_DIR / name).is_file()]
    for path in sorted(EXAMPLES_DIR.glob("*.set")):
        if path.name not in known:
            examples.append({
                "filename": path.name,
                "label": path.stem.replace("_", " ").title(),
                "category": "Other examples",
                "model": "reference",
            })
    return {"examples": examples}


'''
        app = app[:start] + replacement + app[end:]

    if JS_MARKER not in html:
        rx = re.compile(
            r'(?P<indent>^[ \t]*)for \(const example of data\.examples \|\| \[\]\) \{\s*'
            r'const option = document\.createElement\([\'"]option[\'"]\);\s*'
            r'option\.value = example\.filename;\s*'
            r'option\.textContent = example\.label;\s*'
            r'exampleSelect\.appendChild\(option\);\s*\}',
            re.MULTILINE
        )
        match = rx.search(html)
        if not match:
            raise RuntimeError("Could not locate the loadExamples() loop; no files updated")
        indent = match.group("indent")
        lines = [
            JS_MARKER,
            "const sections = new Map();",
            "for (const example of data.examples || []) {",
            "  const category = example.category || 'Other examples';",
            "  let section = sections.get(category);",
            "  if (!section) {",
            "    section = document.createElement('optgroup');",
            "    section.label = category;",
            "    sections.set(category, section);",
            "    exampleSelect.appendChild(section);",
            "  }",
            "  const option = document.createElement('option');",
            "  option.value = example.filename;",
            "  option.textContent = example.label + (example.model === 'illustrative' ? ' [illustrative]' : (example.model === 'reduced' ? ' [reduced]' : ''));",
            "  if (example.model === 'illustrative' || example.model === 'reduced') {",
            "    option.title = 'Simplified / illustrative J-coupling model';",
            "  }",
            "  section.appendChild(option);",
            "}",
        ]
        html = html[:match.start()] + ("\n" + indent).join(lines) + html[match.end():]

    compile(app, str(app_path), "exec")
    # All network retrieval and source validation completed before changing files.
    backup_write(root / "examples/catalog.json", catalog_raw)
    for filename, raw in downloaded.items():
        backup_write(root / "examples" / filename, raw)
    backup_write(app_path, app)
    backup_write(html_path, html)
    print("Catalog installed in TEST; restart test FastAPI on 8001 to load new /api/examples")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python3 update_example_catalog.py /opt/spinspec-v1.1-test")
    main(Path(sys.argv[1]))
