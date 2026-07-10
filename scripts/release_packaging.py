"""Shared helpers for LabelQt release packaging scripts."""

import shutil
from pathlib import Path


REQUIRED_NATIVE_LICENSE_PACKAGES = ("qtbase", "qtkeychain")


EXCLUDED_NAMES = {
    "LabelQtTests.exe",
    "Qt6Test.dll",
}

WINDOWS_EXECUTABLES = {
    "shared": "labelqt.exe",
    "static": "labelqt-static.exe",
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


def validate_deployed_release(source: Path, variant: str = "shared") -> None:
    if not source.is_dir():
        raise FileNotFoundError(f"Release directory does not exist: {source}")
    executable_name = WINDOWS_EXECUTABLES[variant]
    if not (source / executable_name).is_file():
        raise FileNotFoundError(f"{executable_name} was not found in release directory: {source}")


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


def remove_development_automation_scripts(destination: Path) -> None:
    test_scripts = destination / "scripts" / "official" / "test"
    if test_scripts.exists():
        shutil.rmtree(test_scripts)


def prepare_windows_release_tree(source: Path, destination: Path, variant: str,
                                 include_automation_scripts: bool = True) -> None:
    validate_deployed_release(source, variant)
    copy_release_tree(source, destination)

    selected_executable = WINDOWS_EXECUTABLES[variant]
    for executable_name in WINDOWS_EXECUTABLES.values():
        executable_path = destination / executable_name
        if executable_name != selected_executable and executable_path.exists():
            executable_path.unlink()

    if selected_executable != "labelqt.exe":
        (destination / selected_executable).replace(destination / "labelqt.exe")

    scripts_root = destination / "scripts"
    if include_automation_scripts:
        remove_development_automation_scripts(destination)
    elif scripts_root.exists():
        shutil.rmtree(scripts_root)


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


def package_has_license(source: Path, package_name: str, accepted_names: set[str]) -> bool:
    package_root = source / package_name
    return package_root.is_dir() and any(
        path.is_file() and path.name.lower() in accepted_names
        for path in package_root.rglob("*")
    )


def copy_native_dependency_licenses(source: Path, destination: Path) -> int:
    if not source.is_dir():
        raise FileNotFoundError(f"Native dependency license directory does not exist: {source}")

    accepted_names = {"copyright", "copying", "copying.txt", "license", "license.md", "license.txt"}
    missing_packages = [
        package_name
        for package_name in REQUIRED_NATIVE_LICENSE_PACKAGES
        if not package_has_license(source, package_name, accepted_names)
    ]
    if missing_packages:
        raise FileNotFoundError(
            "Required native dependency license directories are missing or contain no recognized license file: "
            + ", ".join(missing_packages)
        )

    copied = 0
    target_root = destination / "licenses" / "native"
    for license_path in sorted(path for path in source.rglob("*") if path.is_file()):
        if license_path.name.lower() not in accepted_names:
            continue
        target_path = target_root / license_path.relative_to(source)
        target_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(license_path, target_path)
        copied += 1

    if copied == 0:
        raise FileNotFoundError(f"No dependency license files were found under: {source}")
    return copied


def write_qt_source_offer(destination: Path, *, qt_version: str, qt_source_url: str) -> None:
    notice = f"""Qt corresponding source offer

This LabelQt package distributes Qt {qt_version} libraries.

The corresponding Qt source archive for this binary release is controlled by the LabelQt project and is available at:
{qt_source_url}

See THIRD_PARTY_NOTICES.md and licenses/native for the applicable license texts and component notices.
"""
    (destination / "QT_SOURCE_OFFER.txt").write_text(notice, encoding="utf-8")


def write_static_linking_notice(destination: Path, *, qt_version: str, qt_source_url: str,
                                relinking_instructions_url: str) -> None:
    notice = f"""LabelQt static linking notice

This package contains a statically linked Qt {qt_version} build and other native dependencies.

Corresponding Qt source controlled by the LabelQt project:
{qt_source_url}

LabelQt application source and relinking instructions:
{relinking_instructions_url}

The licenses for the exact dependency packages used to produce this binary are included under licenses/native.
LabelQt does not restrict reverse engineering performed for debugging modifications to LGPL-covered components.
"""
    (destination / "STATIC_LINKING_NOTICE.txt").write_text(notice, encoding="utf-8")


def copy_static_relinking_document(repository_root: Path, destination: Path) -> None:
    source = repository_root / "docs" / "windows-static-relinking.md"
    if not source.is_file():
        raise FileNotFoundError(f"Static relinking document is missing: {source}")
    shutil.copy2(source, destination / "RELINKING.md")
