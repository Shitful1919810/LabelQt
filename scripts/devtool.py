#!/usr/bin/env python3
"""Developer helper commands for LabelQt agents and maintainers."""

from __future__ import annotations

import argparse
import compileall
import json
import os
import platform
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


VALID_PARAMETER_TYPES = {
    "boolean",
    "bool",
    "choice",
    "directory",
    "file",
    "group",
    "multiline",
    "secret",
    "select",
    "text",
    "textarea",
}


@dataclass(frozen=True)
class ModuleTopic:
    name: str
    keywords: tuple[str, ...]
    files: tuple[str, ...]
    notes: tuple[str, ...]
    docs: tuple[str, ...] = ()


MODULE_TOPICS = (
    ModuleTopic(
        name="label-edit",
        keywords=("label", "edit", "undo", "clipboard", "paste", "marker"),
        files=(
            "src/services/LabelEditController.*",
            "src/core/UndoStack.*",
            "src/ui/LabelTableModel.*",
            "src/ui/LabelEditDelegates.*",
            "src/ui/CurrentPageLabelContext.*",
        ),
        notes=(
            "可逆 label 编辑必须通过 LabelEditController 和 UndoStack。",
            "当前页可见行映射与筛选应复用 CurrentPageLabelContext。",
        ),
    ),
    ModuleTopic(
        name="selection",
        keywords=("selection", "select", "multi", "shift", "ctrl", "visible"),
        files=(
            "src/ui/LabelSelectionController.*",
            "src/ui/CurrentPageLabelContext.*",
            "src/ui/ImageCanvas.*",
            "src/ui/MainWindow.*",
        ),
        notes=(
            "表格、画布和当前页选择同步应集中在 LabelSelectionController。",
            "不要让被 group 筛选隐藏的 label 留在 selection 中。",
        ),
    ),
    ModuleTopic(
        name="image-canvas",
        keywords=("canvas", "image", "zoom", "viewport", "hover", "selection mode"),
        files=(
            "src/ui/ImageCanvas.*",
            "src/ui/CanvasLabelItems.*",
            "src/ui/CanvasLabelTextEditor.*",
            "src/ui/ImagePageViewController.*",
            "src/services/ImagePageCache.*",
        ),
        notes=(
            "图片加载、缓存、相邻页预加载和视图恢复归 ImagePageViewController/ImagePageCache。",
            "ImageCanvas 负责交互意图和绘制，不直接修改 Project。",
        ),
    ),
    ModuleTopic(
        name="proofread-diff",
        keywords=("proofread", "review", "diff", "compare", "report"),
        files=(
            "src/services/ProjectComparisonService.*",
            "src/services/LabelSequenceDiffService.*",
            "src/services/TextDiffService.*",
            "src/services/TextDiffHtmlRenderer.*",
            "src/services/ProofreadReportService.*",
            "src/ui/ProofreadChangesDialog.*",
        ),
        notes=(
            "工程对比、页匹配和 label 对齐属于 services，UI 只展示结构化结果。",
            "HTML 报告导出属于 ProofreadReportService。",
        ),
        docs=("docs/proofreading-diff.md",),
    ),
    ModuleTopic(
        name="automation",
        keywords=("automation", "script", "python", "ocr", "translate", "keychain"),
        files=(
            "src/services/AutomationService.*",
            "src/services/AutomationManifestParser.*",
            "src/services/AutomationOperationApplier.*",
            "src/services/AutomationPythonResolver.*",
            "src/ui/AutomationController.*",
            "scripts/official/sdk/labelqt_automation.py",
        ),
        notes=(
            "自动化脚本通过外部 Python 进程运行，不嵌入解释器。",
            "脚本 secret 必须走 QtKeychain，不写入 JSON、日志或 preference。",
        ),
        docs=("docs/automation-scripting.md",),
    ),
    ModuleTopic(
        name="project-workflow",
        keywords=("project", "open", "save", "merge", "page order", "metadata"),
        files=(
            "src/services/ProjectController.*",
            "src/services/ProjectWorkflowController.*",
            "src/services/ProjectMergeService.*",
            "src/services/ProjectPageOrderService.*",
            "src/services/ProjectMetadataService.*",
        ),
        notes=(
            "打开、保存、新建、合并、页序调整等流程应留在 services/controller。",
            "LabelPlus comment 内部数据必须通过 ProjectMetadataService。",
        ),
    ),
    ModuleTopic(
        name="preferences-i18n",
        keywords=("preference", "setting", "i18n", "translation", "theme"),
        files=(
            "src/core/AppPreferences.*",
            "src/core/TranslationManager.*",
            "src/ui/PreferenceDialog.*",
            "translations/labelqt_*.ts",
        ),
        notes=(
            "新增用户可见文本必须使用 tr() 并同步四种 .ts。",
            "可配置行为应通过 AppPreferences 和 preference.json。",
        ),
    ),
    ModuleTopic(
        name="release",
        keywords=("release", "package", "zip", "msi", "deb", "rpm"),
        files=(
            "scripts/package_windows_release.py",
            "scripts/package_windows_msi.py",
            "scripts/release_packaging.py",
            "docs/release-checklist.md",
            "THIRD_PARTY_NOTICES.md",
        ),
        notes=(
            "发行包需要包含许可证和第三方声明。",
            "Windows 便携包与 Linux 安装包的脚本来源路径不同，修改前先查 release checklist。",
        ),
        docs=("docs/release-checklist.md",),
    ),
)


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def default_preset() -> str:
    system = platform.system()
    if system == "Linux":
        return "linux-debug"
    if system == "Darwin":
        return "macos-debug"
    if system == "Windows" or os.environ.get("MSYSTEM"):
        return "windows-vs-debug"
    raise SystemExit("Unable to infer a CMake preset for this platform.")


def run(command: list[str], cwd: Path) -> None:
    print("+ " + " ".join(command))
    subprocess.run(command, cwd=cwd, check=True)


def check_command(args: argparse.Namespace) -> int:
    repo = repo_root()
    if args.mode == "i18n":
        run([sys.executable, str(repo / "scripts" / "check_translations.py")], repo)
        return 0
    if args.mode == "fast":
        run([sys.executable, str(repo / "scripts" / "check_fast.py")], repo)
        return 0

    preset = args.preset or os.environ.get("LABELQT_CHECK_PRESET") or default_preset()
    run([sys.executable, str(repo / "scripts" / "check_all.py"), preset], repo)
    return 0


def print_module_topic(topic: ModuleTopic) -> None:
    print(f"[{topic.name}]")
    print("Files:")
    for file_pattern in topic.files:
        print(f"  - {file_pattern}")
    if topic.docs:
        print("Docs:")
        for doc in topic.docs:
            print(f"  - {doc}")
    print("Notes:")
    for note in topic.notes:
        print(f"  - {note}")
    print()


def module_map_command(args: argparse.Namespace) -> int:
    query = " ".join(args.query).lower().strip()
    if not query:
        for topic in MODULE_TOPICS:
            print_module_topic(topic)
        return 0

    matches: list[ModuleTopic] = []
    for topic in MODULE_TOPICS:
        haystack = " ".join((topic.name, *topic.keywords, *topic.files, *topic.notes)).lower()
        if all(term in haystack for term in query.split()):
            matches.append(topic)

    if not matches:
        print(f"No module topic matched: {query}", file=sys.stderr)
        print("Run without a query to list all topics.", file=sys.stderr)
        return 1

    for topic in matches:
        print_module_topic(topic)
    return 0


def load_json(path: Path) -> tuple[dict[str, Any] | None, str | None]:
    try:
        with path.open("r", encoding="utf-8") as handle:
            value = json.load(handle)
    except OSError as error:
        return None, str(error)
    except json.JSONDecodeError as error:
        return None, f"{error.msg} at line {error.lineno}, column {error.colno}"

    if not isinstance(value, dict):
        return None, "top-level value must be an object"
    return value, None


def manifest_scripts(manifest: dict[str, Any]) -> list[dict[str, Any]]:
    scripts = manifest.get("scripts")
    if isinstance(scripts, list):
        return [script for script in scripts if isinstance(script, dict)]
    return [manifest]


def validate_parameter(parameter: Any, manifest_path: Path, script_name: str, issues: list[str]) -> None:
    if not isinstance(parameter, dict):
        issues.append(f"{manifest_path}: {script_name}: parameter must be an object")
        return

    key = parameter.get("key")
    parameter_type = parameter.get("type")
    if not isinstance(key, str) or not key:
        issues.append(f"{manifest_path}: {script_name}: parameter is missing a non-empty key")
    if parameter_type not in VALID_PARAMETER_TYPES:
        issues.append(
            f"{manifest_path}: {script_name}: parameter {key!r} has invalid type {parameter_type!r}"
        )
    if parameter_type in {"choice", "select"}:
        options = parameter.get("options")
        if not isinstance(options, list) or not options:
            issues.append(f"{manifest_path}: {script_name}: choice parameter {key!r} needs options")
    if parameter_type == "secret":
        for field in ("service", "account", "environment"):
            if not isinstance(parameter.get(field), str) or not parameter.get(field):
                issues.append(
                    f"{manifest_path}: {script_name}: secret parameter {key!r} is missing {field}"
                )


def validate_secret(secret: Any, manifest_path: Path, script_name: str, issues: list[str]) -> None:
    if not isinstance(secret, dict):
        issues.append(f"{manifest_path}: {script_name}: secret must be an object")
        return
    for field in ("service", "account", "environment"):
        if not isinstance(secret.get(field), str) or not secret.get(field):
            issues.append(f"{manifest_path}: {script_name}: secret is missing {field}")


def validate_manifest(manifest_path: Path, repo: Path) -> list[str]:
    issues: list[str] = []
    manifest, error = load_json(manifest_path)
    if error:
        return [f"{manifest_path.relative_to(repo)}: {error}"]
    assert manifest is not None

    group_name = manifest.get("name")
    if not isinstance(group_name, str) or not group_name:
        issues.append(f"{manifest_path.relative_to(repo)}: missing non-empty name")
    if manifest.get("apiVersion") != 1:
        issues.append(f"{manifest_path.relative_to(repo)}: apiVersion should be 1")

    scripts = manifest_scripts(manifest)
    if not scripts:
        issues.append(f"{manifest_path.relative_to(repo)}: no scripts were declared")
        return issues

    seen_ids: set[str] = set()
    for script in scripts:
        script_name = str(script.get("name") or script.get("id") or "<unnamed>")
        script_id = script.get("id")
        entry = script.get("entry")
        if not isinstance(script_id, str) or not script_id:
            issues.append(f"{manifest_path.relative_to(repo)}: {script_name}: missing non-empty id")
        elif script_id in seen_ids:
            issues.append(f"{manifest_path.relative_to(repo)}: duplicate script id {script_id!r}")
        else:
            seen_ids.add(script_id)

        if not isinstance(script.get("name"), str) or not script.get("name"):
            issues.append(f"{manifest_path.relative_to(repo)}: {script_name}: missing non-empty name")

        if not isinstance(entry, str) or not entry:
            issues.append(f"{manifest_path.relative_to(repo)}: {script_name}: missing entry")
        else:
            entry_path = manifest_path.parent / entry
            if not entry_path.is_file():
                issues.append(
                    f"{manifest_path.relative_to(repo)}: {script_name}: entry does not exist: {entry}"
                )
            elif entry_path.suffix == ".py":
                if not compileall.compile_file(str(entry_path), quiet=1):
                    issues.append(
                        f"{manifest_path.relative_to(repo)}: {script_name}: Python entry failed to compile"
                    )

        for parameter in script.get("parameters", []):
            validate_parameter(parameter, manifest_path.relative_to(repo), script_name, issues)
        for secret in script.get("secrets", []):
            validate_secret(secret, manifest_path.relative_to(repo), script_name, issues)

    return issues


def automation_validate_command(args: argparse.Namespace) -> int:
    repo = repo_root()
    roots = args.roots or [repo / "scripts" / "official"]
    issues: list[str] = []
    manifest_count = 0

    for root in roots:
        root = root if root.is_absolute() else repo / root
        if not root.exists():
            issues.append(f"{root}: script root does not exist")
            continue
        for manifest_path in sorted(root.rglob("script.json")):
            manifest_count += 1
            issues.extend(validate_manifest(manifest_path, repo))

    if issues:
        for issue in issues:
            print(issue, file=sys.stderr)
        return 1

    print(f"Automation manifests passed ({manifest_count} script.json files).")
    return 0


def release_audit_command(args: argparse.Namespace) -> int:
    root = args.path.resolve()
    issues: list[str] = []
    if not root.exists():
        issues.append(f"Path does not exist: {root}")
    elif args.kind == "windows-dir":
        required = ("labelqt.exe", "LICENSE.txt", "THIRD_PARTY_NOTICES.md")
        for name in required:
            if not (root / name).exists():
                issues.append(f"Missing {name} in {root}")
        if not ((root / "scripts" / "official").is_dir()):
            issues.append("Missing scripts/official directory")
        if not ((root / "scripts" / "custom").is_dir()):
            issues.append("Missing scripts/custom directory")
        for forbidden in root.rglob("*"):
            if forbidden.name == "__pycache__" or forbidden.suffix.lower() in {".pdb", ".ilk", ".lib", ".exp"}:
                issues.append(f"Unexpected release artifact: {forbidden.relative_to(root)}")
    elif args.kind == "linux-install-root":
        if not (root / "usr" / "bin" / "labelqt").exists():
            issues.append("Missing usr/bin/labelqt")
        if not (root / "usr" / "share" / "labelqt" / "scripts" / "official").is_dir():
            issues.append("Missing usr/share/labelqt/scripts/official")
    else:
        raise SystemExit(f"Unknown release audit kind: {args.kind}")

    if issues:
        for issue in issues:
            print(issue, file=sys.stderr)
        return 1

    print(f"Release audit passed: {root}")
    return 0


def commit_message_command(args: argparse.Namespace) -> int:
    repo = repo_root()
    diff_args = ["git", "-C", str(repo), "diff", "--stat"]
    if args.staged:
        diff_args.insert(4, "--cached")
    stat = subprocess.run(diff_args, check=True, text=True, capture_output=True).stdout.strip()
    untracked = ""
    if not args.staged:
        untracked = subprocess.run(
            ["git", "-C", str(repo), "ls-files", "--others", "--exclude-standard"],
            check=True,
            text=True,
            capture_output=True,
        ).stdout.strip()
    title = args.title or "<type>(<scope>): <一句话概括本次改动>"

    print(title)
    print()
    print("背景:")
    print("- ")
    print()
    print("改动:")
    if stat or untracked:
        print("```text")
        if stat:
            print(stat)
        if untracked:
            print("Untracked files:")
            print(untracked)
        print("```")
    else:
        print("- ")
    print()
    print("验证:")
    print("- [ ] `python scripts/devtool.py check --mode fast`")
    print("- [ ] `python scripts/devtool.py check --mode all`")
    print("- [ ] 其他：")
    print()
    print("风险:")
    print("- ")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    check_parser = subparsers.add_parser("check", help="Run repository checks.")
    check_parser.add_argument("--mode", choices=("i18n", "fast", "all"), default="fast")
    check_parser.add_argument("--preset", help="CMake preset for --mode all.")
    check_parser.set_defaults(func=check_command)

    module_parser = subparsers.add_parser("module-map", help="Find likely files for a task area.")
    module_parser.add_argument("query", nargs="*", help="Topic keywords, for example proofread or automation.")
    module_parser.set_defaults(func=module_map_command)

    automation_parser = subparsers.add_parser(
        "automation-validate", help="Validate automation script manifests."
    )
    automation_parser.add_argument("roots", nargs="*", type=Path, help="Script roots to scan.")
    automation_parser.set_defaults(func=automation_validate_command)

    release_parser = subparsers.add_parser("release-audit", help="Validate a packaged release tree.")
    release_parser.add_argument("path", type=Path)
    release_parser.add_argument(
        "--kind", choices=("windows-dir", "linux-install-root"), default="windows-dir"
    )
    release_parser.set_defaults(func=release_audit_command)

    commit_parser = subparsers.add_parser("commit-message", help="Print a commit message skeleton.")
    commit_parser.add_argument("--staged", action="store_true", help="Use staged diff stats.")
    commit_parser.add_argument("--title", help="Commit title to place in the template.")
    commit_parser.set_defaults(func=commit_message_command)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
