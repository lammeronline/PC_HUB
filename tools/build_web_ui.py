#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src" / "WebUI.html"
DST = ROOT / "src" / "WebUI.h"

html = SRC.read_text(encoding="utf-8")

DST.write_text(
    '#pragma once\n'
    '#include <Arduino.h>\n\n'
    '// Auto-generated from src/WebUI.html. Edit the HTML source, then run:\n'
    '//   python tools/build_web_ui.py\n'
    'static const char WEB_INDEX[] PROGMEM = R"rawliteral(\n'
    f'{html}\n'
    ')rawliteral";\n',
    encoding="utf-8",
    newline="\n",
)

print(f"{SRC.relative_to(ROOT)} -> {DST.relative_to(ROOT)} ({len(html)} bytes)")
