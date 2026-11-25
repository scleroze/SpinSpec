#!/usr/bin/env python3
"""Guard the single-button automatic replot UI and check inline JS syntax."""
from html.parser import HTMLParser
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

class Scripts(HTMLParser):
    def __init__(self):
        super().__init__()
        self.in_script = False
        self.contents = []
    def handle_starttag(self, tag, attrs):
        if tag == "script" and "src" not in dict(attrs):
            self.in_script = True
            self.contents.append("")
    def handle_endtag(self, tag):
        if tag == "script":
            self.in_script = False
    def handle_data(self, data):
        if self.in_script:
            self.contents[-1] += data

page = (ROOT / "templates/index.html").read_text()
assert page.count('id="calculateBtn"') == 1
assert 'id="replotBtn"' not in page
assert 'id="calculationMode"' in page
assert "function physicalSignature(data)" in page
assert "function calculateOrReuse(data)" in page
assert "return await redrawFromTransitions(data)" in page
assert "return sendRequest('/api/calculate', data)" in page
assert "const result = await calculateOrReuse(calculationData)" in page
assert "const result = await calculateOrReuse(data)" in page
assert "form.addEventListener('input'" in page
assert "updateCalculationMode();" in page
assert "fillFormFromSet(text);" in page

parsed = Scripts()
parsed.feed(page)
for index, source in enumerate(parsed.contents):
    if not source.strip():
        continue
    with tempfile.NamedTemporaryFile(mode="w", suffix=".js") as script:
        script.write(source)
        script.flush()
        result = subprocess.run(["node", "--check", script.name],
                                capture_output=True, text=True)
        assert result.returncode == 0, (index, result.stderr)

print("PASS: single-button auto-replot UI, .set loading and JavaScript syntax")
