#!/usr/bin/env python3
import os
import pathlib
import platform
import subprocess
import sys


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[1]


def default_preset() -> str:
    system = platform.system()
    if system == "Linux":
        return "linux-debug"
    if system == "Darwin":
        return "macos-debug"
    if system == "Windows" or os.environ.get("MSYSTEM"):
        return "windows-debug"

    raise SystemExit(
        "Unable to infer a CMake preset for this platform. Pass one explicitly."
    )


def run(command: list[str], cwd: pathlib.Path) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def cmake_env() -> dict[str, str]:
    env = os.environ.copy()
    env["CCACHE_DISABLE"] = "1"
    return env


def run_with_env(command: list[str], cwd: pathlib.Path) -> None:
    subprocess.run(command, cwd=cwd, check=True, env=cmake_env())


def main() -> int:
    repo = repo_root()
    preset = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("LABELQT_CHECK_PRESET")
    if not preset:
        preset = default_preset()

    run_with_env(["cmake", "--preset", preset], repo)
    run_with_env(["cmake", "--build", "--preset", preset], repo)
    run_with_env(["ctest", "--preset", preset], repo)
    run([sys.executable, str(repo / "scripts" / "check_fast.py")], repo)

    print("All checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
