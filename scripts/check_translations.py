#!/usr/bin/env python3
import html
import ast
import pathlib
import re
import sys
import xml.etree.ElementTree as ET


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[1]


def unescape_cpp_string(value: str) -> str:
    try:
        return ast.literal_eval('"' + value + '"')
    except (SyntaxError, ValueError):
        return value


def joined_cpp_strings(expression: str) -> str:
    return "".join(unescape_cpp_string(value) for value in re.findall(r'"((?:\\.|[^"\\])*)"', expression))


STRING_LITERAL = r""""(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'"""
COMMENTS_OR_STRINGS = re.compile(r'//[^\n]*|/\*.*?\*/|' + STRING_LITERAL, re.DOTALL)


def without_comments(text: str, *, strings: bool = False) -> str:
    return COMMENTS_OR_STRINGS.sub(
        lambda match: " " * len(match.group())
        if strings or match.group().startswith(("//", "/*")) else match.group(),
        text,
    )


def cpp_default_context(path: pathlib.Path) -> str:
    # Q_OBJECT uses the qualified class name; Q_DECLARE_TR_FUNCTIONS uses its explicit context.
    declarations = re.compile(
        r'\bnamespace\s+(?P<namespace>[\w:]+)\s*\{'
        r'|\b(?:class|struct)\s+(?P<class>\w+)\b[^;{}]*\{'
        r'|\bQ_DECLARE_TR_FUNCTIONS\s*\(\s*(?P<context>[\w:]+)\s*\)'
        r'|\bQ_OBJECT\b|[{}]'
    )
    candidates = [path] if path.suffix in {".h", ".hpp"} else [path.with_suffix(".h"), path.with_suffix(".hpp")]
    for header in candidates:
        if not header.exists():
            continue
        scopes: list[str] = []
        for match in declarations.finditer(without_comments(header.read_text(encoding="utf-8"), strings=True)):
            if match.group("namespace") or match.group("class"):
                scopes.append(match.group("namespace") or match.group("class"))
            elif match.group() == "{":
                scopes.append("")
            elif match.group() == "}":
                if scopes:
                    scopes.pop()
            elif scopes and scopes[-1] == path.stem:
                return match.group("context") or "::".join(scope for scope in scopes if scope)
    return path.stem


def collect_qml_strings(text: str, default_context: str) -> set[tuple[str, str]]:
    text = without_comments(text)
    # Match the whole literal expression, never a literal prefix of a dynamic argument.
    expression = r'((?:' + STRING_LITERAL + r')(?:\s*\+\s*(?:' + STRING_LITERAL + r'))*)'
    tr_pattern = re.compile(r'qsTr\s*\(\s*' + expression + r'\s*(?=[,)])')
    translate_pattern = re.compile(
        r'qsTranslate\s*\(\s*' + expression + r'\s*,\s*' + expression + r'\s*(?=[,)])'
    )
    tokens = re.compile(STRING_LITERAL + r'|(?<![\w$.])\b(?:qsTr|qsTranslate)\s*\(')

    def literal_value(value: str) -> str:
        return "".join(ast.literal_eval(match.group()) for match in re.finditer(STRING_LITERAL, value))

    result: set[tuple[str, str]] = set()
    for token in tokens.finditer(text):
        if match := tr_pattern.match(text, token.start()):
            result.add((default_context, literal_value(match.group(1))))
        elif match := translate_pattern.match(text, token.start()):
            result.add((literal_value(match.group(1)), literal_value(match.group(2))))
    return result


def collect_source_strings(src_dir: pathlib.Path) -> tuple[set[str], set[tuple[str, str]]]:
    string_expression = r'((?:"(?:\\.|[^"\\])*"\s*)+)'
    tr_pattern = re.compile(r'(?<!::)\btr\s*\(\s*' + string_expression)
    qualified_tr_pattern = re.compile(r'\b([A-Za-z_][A-Za-z0-9_]*)::tr\s*\(\s*' + string_expression)
    translate_pattern = re.compile(
        r'\b(?:QCoreApplication::)?translate\s*\(\s*"((?:\\.|[^"\\])*)"\s*,\s*' + string_expression
    )

    sources: set[str] = set()
    context_sources: set[tuple[str, str]] = set()
    for path in src_dir.rglob("*"):
        if path.suffix == ".qml":
            qml_sources = collect_qml_strings(path.read_text(encoding="utf-8"), path.stem)
            context_sources.update(qml_sources)
            sources.update(source for _, source in qml_sources)
            continue
        if path.suffix not in {".cpp", ".h", ".hpp"}:
            continue
        text = path.read_text(encoding="utf-8")
        default_context = cpp_default_context(path)
        for match in qualified_tr_pattern.finditer(text):
            source = joined_cpp_strings(match.group(2))
            sources.add(source)
            context_sources.add((unescape_cpp_string(match.group(1)), source))
        for match in tr_pattern.finditer(text):
            source = joined_cpp_strings(match.group(1))
            sources.add(source)
            context_sources.add((default_context, source))
        for match in translate_pattern.finditer(text):
            context = unescape_cpp_string(match.group(1))
            source = joined_cpp_strings(match.group(2))
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

    print(f"Translation check passed ({len(sources)} C++/QML source strings, {len(context_sources)} context/source pairs).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
