Import("env")
from pathlib import Path

root = Path(env["PROJECT_DIR"])
src  = root / "src" / "WebUI.html"
dst  = root / "src" / "WebUI.h"

if not src.exists():
    print(f"[WebUI] WARNING: {src} not found, skipping generation")
else:
    html = src.read_text(encoding="utf-8")
    dst.write_text(
        '#pragma once\n'
        '#include <Arduino.h>\n\n'
        '// Auto-generated from src/WebUI.html — do not edit directly.\n'
        'static const char WEB_INDEX[] PROGMEM = R"rawliteral(\n'
        f'{html}\n'
        ')rawliteral";\n',
        encoding="utf-8",
        newline="\n",
    )
    print(f"[WebUI] WebUI.html -> WebUI.h ({len(html):,} bytes)")
