#!/usr/bin/env python3
"""Build the LabelQt user manual with Typst."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


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
        help="Embed screenshots from manual/assets instead of rendering placeholders.",
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
    output = args.output
    if not output.is_absolute():
        output = root / output
    output.parent.mkdir(parents=True, exist_ok=True)

    command = [typst, "compile"]
    if args.with_screenshots:
        command.extend(["--input", "screenshots=true"])
    command.extend([str(source), str(output)])
    print("+ " + " ".join(command))
    subprocess.run(command, cwd=root, check=True)
    print(f"Built {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
