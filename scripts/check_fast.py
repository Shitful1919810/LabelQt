#!/usr/bin/env python3
import compileall
import pathlib
import subprocess
import sys


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[1]


def run(command: list[str], cwd: pathlib.Path) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def main() -> int:
    repo = repo_root()

    run([sys.executable, str(repo / "scripts" / "check_translations.py")], repo)

    if not compileall.compile_dir(repo / "scripts" / "official", quiet=1):
        return 1

    run(["git", "-C", str(repo), "diff", "HEAD", "--check"], repo)

    print("Fast checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
