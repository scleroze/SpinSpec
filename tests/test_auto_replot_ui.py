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
assert 'id="lineWidth"' in page
assert 'id="linewidths"' not in page
assert "linewidths: getLinewidthsForEngine()" in page
assert "return `${fwhm} 0`;" in page
assert "function decodeSetDisplayWidth(widthLine, xRangeLine = '')" in page
assert "const display = decodeSetDisplayWidth(lines[i + 2] || '', xLine);" in page
assert "document.getElementById('lineWidth').value = display.fwhm;" in page

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

# Execute the actual parser in Node, not a duplicate Python implementation.
import json
import re
source = parsed.contents[0]
begin = source.index("    function decodeSetDisplayWidth(")
end = source.index("\n    function fillFormFromSet(", begin)
function_src = source[begin:end]
cases = [
    ("1 0", "", {"fwhm": "1", "left": "", "right": ""}),
    ("0.2 20", "", {"fwhm": "0.2", "left": "-20", "right": "20"}),
    ("1 220", "XRange auto 100",
     {"fwhm": "1", "left": "-220", "right": "100"}),
    ("1 0", "XRange 0 120",
     {"fwhm": "1", "left": "0", "right": "120"}),
    ("0.5 0", "XRange auto -10",
     {"fwhm": "0.5", "left": "", "right": "-10"}),
]
node_js = function_src + "\n" + "\n".join(
    "console.log(JSON.stringify(decodeSetDisplayWidth(" +
    json.dumps(width) + "," + json.dumps(xrange) + ")));"
    for width, xrange, _ in cases
)
with tempfile.NamedTemporaryFile(mode="w", suffix=".js") as source_file:
    source_file.write(node_js)
    source_file.flush()
    result = subprocess.run(["node", source_file.name], text=True,
                            capture_output=True)
    assert result.returncode == 0, result.stderr
    got = [json.loads(line) for line in result.stdout.splitlines()]
    assert got == [expected for _, _, expected in cases], got

print("PASS: one FWHM input, automatic blank X limits, .set backward compatibility")
