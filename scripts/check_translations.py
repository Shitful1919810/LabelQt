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


def collect_source_strings(src_dir: pathlib.Path) -> tuple[set[str], set[tuple[str, str]]]:
    tr_pattern = re.compile(r'(?<!::)\btr\s*\(\s*"((?:\\.|[^"\\])*)"')
    qualified_tr_pattern = re.compile(r'\b([A-Za-z_][A-Za-z0-9_]*)::tr\s*\(\s*"((?:\\.|[^"\\])*)"')
    translate_pattern = re.compile(
        r'\b(?:QCoreApplication::)?translate\s*\(\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"'
    )

    sources: set[str] = set()
    context_sources: set[tuple[str, str]] = set()
    for path in src_dir.rglob("*"):
        if path.suffix not in {".cpp", ".h", ".hpp"}:
            continue
        text = path.read_text(encoding="utf-8")
        default_context = path.stem
        for match in qualified_tr_pattern.finditer(text):
            source = unescape_cpp_string(match.group(2))
            sources.add(source)
            context_sources.add((unescape_cpp_string(match.group(1)), source))
        for match in tr_pattern.finditer(text):
            source = unescape_cpp_string(match.group(1))
            sources.add(source)
            context_sources.add((default_context, source))
        for match in translate_pattern.finditer(text):
            context = unescape_cpp_string(match.group(1))
            source = unescape_cpp_string(match.group(2))
            sources.add(source)
            context_sources.add((context, source))
    return sources, context_sources


def check_translation_file(
    ts_path: pathlib.Path, repo: pathlib.Path, sources: set[str], context_sources: set[tuple[str, str]]
) -> tuple[list[str], list[str]]:
    missing: list[str] = []
    unfinished: list[str] = []

    if not ts_path.exists():
        missing.append(f"{ts_path.relative_to(repo)} is missing")
        return missing, unfinished

    tree = ET.parse(ts_path)
    root = tree.getroot()
    ts_sources: set[str] = set()
    ts_context_sources: set[tuple[str, str]] = set()

    for context_node in root.findall("context"):
        context_name = context_node.findtext("name") or ""
        for message in context_node.findall("message"):
            source_node = message.find("source")
            translation_node = message.find("translation")
            if source_node is None:
                continue

            source_text = html.unescape(source_node.text or "")
            ts_sources.add(source_text)
            ts_context_sources.add((context_name, source_text))

            if translation_node is None or translation_node.get("type") == "unfinished":
                unfinished.append(
                    f"{ts_path.relative_to(repo)}: unfinished translation for '{context_name}::{source_text}'"
                )

    for source in sorted(sources - ts_sources):
        missing.append(f"{ts_path.relative_to(repo)}: missing source '{source}'")
    for context_name, source in sorted(context_sources - ts_context_sources):
        missing.append(f"{ts_path.relative_to(repo)}: missing source '{source}' in context '{context_name}'")

    return missing, unfinished


def main() -> int:
    repo = repo_root()
    sources, context_sources = collect_source_strings(repo / "src")
    translation_files = sorted((repo / "translations").glob("labelqt_*.ts"))

    missing: list[str] = []
    unfinished: list[str] = []
    for ts_path in translation_files:
        file_missing, file_unfinished = check_translation_file(ts_path, repo, sources, context_sources)
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
