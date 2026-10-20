# PlatformIO pre-build step: embeds ui/index.html into src/ui_html.h as a C string.
import base64
import hashlib
import os
import re

try:
    Import("env")  # noqa: F821  (provided by PlatformIO/SCons)
    root = env["PROJECT_DIR"]  # noqa: F821
except NameError:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

src = os.path.join(root, "ui", "index.html")
dst = os.path.join(root, "src", "ui_html.h")
html = "<!doctype html>\n<html lang=\"en\">\n" + open(src, encoding="utf-8").read()
html = html.replace('<meta name="claude:viewport-fit-sides" content="cover">\n', "")
out = "// Generated from ui/index.html by tools/embed_ui.py. Do not edit.\n#pragma once\n#include <pgmspace.h>\n"
out += 'static const char UI_HTML[] PROGMEM = R"UIHTML(' + html + ')UIHTML";\n'
# The page has one inline script; its hash goes in the Content-Security-Policy, so only that exact
# script can run on the settings page and an injected one never could.
scripts = re.findall(r"<script[^>]*>(.*?)</script>", html, re.S)
assert len(scripts) == 1, "the settings page must have exactly one <script> (CSP hash)"
digest = base64.b64encode(hashlib.sha256(scripts[0].encode("utf-8")).digest()).decode()
out += "#define UI_SCRIPT_CSP \"'sha256-" + digest + "'\"\n"
if not os.path.exists(dst) or open(dst, encoding="utf-8").read() != out:
    open(dst, "w", encoding="utf-8").write(out)
