"""Shared helpers for LabelQt release packaging scripts."""

import shutil
from pathlib import Path


EXCLUDED_NAMES = {
    "LabelQtTests.exe",
    "Qt6Test.dll",
}

EXCLUDED_SUFFIXES = {
    ".pdb",
    ".ilk",
    ".exp",
    ".lib",
    ".obj",
    ".tlog",
    ".lastbuildstate",
    ".recipe",
    ".log",
}

EXCLUDED_DIRECTORIES = {
    ".qt",
    "__pycache__",
    "CMakeFiles",
    "Testing",
}


def should_exclude(path: Path) -> bool:
    return path.name in EXCLUDED_NAMES or path.name in EXCLUDED_DIRECTORIES or path.suffix.lower() in EXCLUDED_SUFFIXES


def ignored_names(directory: str, names: list[str]) -> set[str]:
    ignored: set[str] = set()
    for name in names:
        if should_exclude(Path(directory) / name):
            ignored.add(name)
    return ignored


def validate_deployed_release(source: Path) -> None:
    if not source.is_dir():
        raise FileNotFoundError(f"Release directory does not exist: {source}")
    if not (source / "labelqt.exe").is_file():
        raise FileNotFoundError(f"labelqt.exe was not found in release directory: {source}")


def copy_release_tree(source: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    for item in source.iterdir():
        if should_exclude(item):
            continue

        target = destination / item.name
        if item.is_dir():
            shutil.copytree(item, target, ignore=ignored_names)
        elif item.is_file():
            shutil.copy2(item, target)


def copy_release_notices(repository_root: Path, destination: Path) -> None:
    for file_name in ("LICENSE.txt", "THIRD_PARTY_NOTICES.md"):
        source = repository_root / file_name
        if not source.is_file():
            raise FileNotFoundError(f"Required release notice file is missing: {source}")
        shutil.copy2(source, destination / file_name)

    license_files = {
        "BreezeStyleSheets-LICENSE.md": repository_root / "resources/themes/breeze/LICENSE.md",
        "MaterialUi-LICENSE.txt": repository_root / "resources/themes/breeze/MaterialUi.LICENSE",
        "diff-match-patch-LICENSE.txt": repository_root / "third_party/diff-match-patch/LICENSE",
    }
    licenses_dir = destination / "licenses"
    licenses_dir.mkdir(exist_ok=True)
    for target_name, source in license_files.items():
        if not source.is_file():
            raise FileNotFoundError(f"Required third-party license file is missing: {source}")
        shutil.copy2(source, licenses_dir / target_name)
