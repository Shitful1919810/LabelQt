#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

python3 - "$repo_root" <<'PY'
import html
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

repo = pathlib.Path(sys.argv[1])
src_dir = repo / "src"
translation_files = sorted((repo / "translations").glob("labelqt_*.ts"))

tr_pattern = re.compile(r'\btr\s*\(\s*"((?:\\.|[^"\\])*)"')
translate_pattern = re.compile(r'\b(?:QCoreApplication::)?translate\s*\(\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"')

def unescape_cpp_string(value: str) -> str:
    try:
        return bytes(value, "utf-8").decode("unicode_escape")
    except UnicodeDecodeError:
        return value

sources = set()
for path in src_dir.rglob("*"):
    if path.suffix not in {".cpp", ".h", ".hpp"}:
        continue
    text = path.read_text(encoding="utf-8")
    for match in tr_pattern.finditer(text):
        sources.add(unescape_cpp_string(match.group(1)))
    for match in translate_pattern.finditer(text):
        sources.add(unescape_cpp_string(match.group(2)))

missing = []
unfinished = []

for ts_path in translation_files:
    if not ts_path.exists():
        missing.append(f"{ts_path.relative_to(repo)} is missing")
        continue

    tree = ET.parse(ts_path)
    root = tree.getroot()
    ts_sources = set()
    for message in root.findall(".//message"):
        source_node = message.find("source")
        translation_node = message.find("translation")
        if source_node is None:
            continue
        source_text = html.unescape(source_node.text or "")
        ts_sources.add(source_text)
        if translation_node is None or translation_node.get("type") == "unfinished":
            unfinished.append(f"{ts_path.relative_to(repo)}: unfinished translation for '{source_text}'")

    for source in sorted(sources - ts_sources):
        missing.append(f"{ts_path.relative_to(repo)}: missing source '{source}'")

if missing or unfinished:
    for issue in missing + unfinished:
        print(issue, file=sys.stderr)
    sys.exit(1)

print(f"Translation check passed ({len(sources)} tr() source strings).")
PY
