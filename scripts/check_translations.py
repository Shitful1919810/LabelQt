#!/usr/bin/env python3
import html
import pathlib
import re
import sys
import xml.etree.ElementTree as ET


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[1]


def unescape_cpp_string(value: str) -> str:
    try:
        return bytes(value, "utf-8").decode("unicode_escape")
    except UnicodeDecodeError:
        return value


def collect_source_strings(src_dir: pathlib.Path) -> set[str]:
    tr_pattern = re.compile(r'\btr\s*\(\s*"((?:\\.|[^"\\])*)"')
    translate_pattern = re.compile(
        r'\b(?:QCoreApplication::)?translate\s*\(\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"'
    )

    sources: set[str] = set()
    for path in src_dir.rglob("*"):
        if path.suffix not in {".cpp", ".h", ".hpp"}:
            continue
        text = path.read_text(encoding="utf-8")
        for match in tr_pattern.finditer(text):
            sources.add(unescape_cpp_string(match.group(1)))
        for match in translate_pattern.finditer(text):
            sources.add(unescape_cpp_string(match.group(2)))
    return sources


def check_translation_file(
    ts_path: pathlib.Path, repo: pathlib.Path, sources: set[str]
) -> tuple[list[str], list[str]]:
    missing: list[str] = []
    unfinished: list[str] = []

    if not ts_path.exists():
        missing.append(f"{ts_path.relative_to(repo)} is missing")
        return missing, unfinished

    tree = ET.parse(ts_path)
    root = tree.getroot()
    ts_sources: set[str] = set()

    for message in root.findall(".//message"):
        source_node = message.find("source")
        translation_node = message.find("translation")
        if source_node is None:
            continue

        source_text = html.unescape(source_node.text or "")
        ts_sources.add(source_text)

        if translation_node is None or translation_node.get("type") == "unfinished":
            unfinished.append(
                f"{ts_path.relative_to(repo)}: unfinished translation for '{source_text}'"
            )

    for source in sorted(sources - ts_sources):
        missing.append(f"{ts_path.relative_to(repo)}: missing source '{source}'")

    return missing, unfinished


def main() -> int:
    repo = repo_root()
    sources = collect_source_strings(repo / "src")
    translation_files = sorted((repo / "translations").glob("labelqt_*.ts"))

    missing: list[str] = []
    unfinished: list[str] = []
    for ts_path in translation_files:
        file_missing, file_unfinished = check_translation_file(ts_path, repo, sources)
        missing.extend(file_missing)
        unfinished.extend(file_unfinished)

    if missing or unfinished:
        for issue in missing + unfinished:
            print(issue, file=sys.stderr)
        return 1

    print(f"Translation check passed ({len(sources)} tr() source strings).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
