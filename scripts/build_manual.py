#!/usr/bin/env python3
"""Build the LabelQt user manual with Typst."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def referenced_screenshots(typst: str, source: Path) -> list[str]:
    result = subprocess.run(
        [typst, "query", str(source), "<manual-screenshot>", "--field", "value"],
        check=True, stdout=subprocess.PIPE, text=True,
    )
    files = json.loads(result.stdout)
    if not isinstance(files, list) or any(
        not isinstance(file, str) or not file or Path(file).name != file for file in files
    ):
        raise ValueError("Screenshot metadata must contain asset file names.")
    return list(dict.fromkeys(files))


def partition_screenshots(files: list[str], assets: Path) -> tuple[list[str], list[str]]:
    available, missing = [], []
    for file in files:
        (available if (assets / file).is_file() else missing).append(file)
    return available, missing


def main() -> int:
    parser = argparse.ArgumentParser(description="Build the LabelQt Typst user manual.")
    parser.add_argument(
        "--output",
        type=Path,
        default=repo_root() / "dist" / "LabelQt-user-manual-zh_CN.pdf",
        help="Output PDF path. Defaults to dist/LabelQt-user-manual-zh_CN.pdf.",
    )
    parser.add_argument(
        "--typst",
        default="typst",
        help="Typst executable or command name.",
    )
    parser.add_argument(
        "--with-screenshots",
        action="store_true",
        help="Embed available screenshots from manual/assets; keep placeholders for missing files.",
    )
    parser.add_argument(
        "--require-screenshots", action="store_true",
        help="Embed screenshots and fail before building if any referenced image is missing.",
    )
    parser.add_argument(
        "--list-missing-screenshots", action="store_true",
        help="List missing screenshot file names without building a PDF.",
    )
    args = parser.parse_args()

    typst = shutil.which(args.typst)
    if typst is None:
        print(
            "Typst was not found. Install Typst 0.14 or newer, or pass --typst /path/to/typst.",
            file=sys.stderr,
        )
        return 1

    root = repo_root()
    source = root / "manual" / "main.typ"
    available = []
    if args.with_screenshots or args.require_screenshots or args.list_missing_screenshots:
        available, missing = partition_screenshots(referenced_screenshots(typst, source), root / "manual" / "assets")
        print(f"Screenshots: {len(available)} available, {len(missing)} missing.")
        for file in missing:
            print(f"  manual/assets/{file}")
        if args.require_screenshots and missing:
            print("Required screenshots are missing; the output PDF has not been changed.", file=sys.stderr)
            return 1
        if args.list_missing_screenshots:
            return 0
    output = args.output
    if not output.is_absolute():
        output = root / output
    output.parent.mkdir(parents=True, exist_ok=True)

    command = [typst, "compile"]
    if args.with_screenshots or args.require_screenshots:
        command.extend(["--input", "screenshots=true"])
        command.extend(["--input", "screenshot-files=" + json.dumps(available)])
    command.extend([str(source), str(output)])
    print("+ " + " ".join(command))
    subprocess.run(command, cwd=root, check=True)
    print(f"Built {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
