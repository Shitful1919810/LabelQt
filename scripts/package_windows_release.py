#!/usr/bin/env python3
"""Create a Windows release zip from a deployed LabelQt directory."""


import argparse
import tempfile
import zipfile
from pathlib import Path

from release_packaging import (
    copy_native_dependency_licenses,
    copy_release_notices,
    copy_static_relinking_document,
    prepare_windows_release_tree,
    write_qt_source_offer,
    write_static_linking_notice,
)


def create_zip(source: Path, zip_path: Path, archive_root: Path) -> None:
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(source.rglob("*")):
            relative_path = archive_root / path.relative_to(source)
            if path.is_dir():
                archive.write(path, relative_path.as_posix() + "/")
            else:
                archive.write(path, relative_path.as_posix())


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path, help="Deployed Windows Release directory.")
    parser.add_argument("--version", required=True, help="Release version, for example v0.1.0-alpha.2.")
    parser.add_argument("--variant", choices=("shared", "static"), default="shared",
                        help="Windows runtime variant. Defaults to shared.")
    parser.add_argument("--output-dir", default=Path("dist"), type=Path, help="Directory for the generated zip.")
    parser.add_argument("--repo-root", default=Path.cwd(), type=Path, help="Repository root containing release notices.")
    parser.add_argument("--dependency-license-dir", required=True, type=Path,
                        help="Directory containing native dependency license files, such as a vcpkg triplet share directory.")
    parser.add_argument("--qt-version", required=True, help="Exact Qt version distributed by this release.")
    parser.add_argument("--qt-source-url", required=True,
                        help="Project-controlled URL for the corresponding Qt source archive.")
    parser.add_argument("--relinking-instructions-url",
                        help="URL for LabelQt source and static relinking instructions.")
    parser.add_argument("--exclude-automation-scripts", action="store_true",
                        help="Exclude the complete scripts directory from this release.")
    return parser.parse_args()


def validate_static_args(args: argparse.Namespace) -> None:
    if args.variant != "static":
        return
    required = {
        "--relinking-instructions-url": args.relinking_instructions_url,
    }
    missing = [option for option, value in required.items() if not value]
    if missing:
        raise SystemExit("Static release packaging requires: " + ", ".join(missing))


def main() -> int:
    args = parse_args()
    source = args.source.resolve()
    repository_root = args.repo_root.resolve()
    output_dir = args.output_dir.resolve()
    validate_static_args(args)
    package_name = f"LabelQt-{args.version}-windows-x64-{args.variant}"
    zip_path = output_dir / f"{package_name}.zip"

    with tempfile.TemporaryDirectory(prefix="labelqt-windows-package-") as temporary_directory:
        package_root = Path(temporary_directory) / package_name
        prepare_windows_release_tree(
            source,
            package_root,
            args.variant,
            include_automation_scripts=not args.exclude_automation_scripts,
        )
        copy_release_notices(repository_root, package_root)
        copy_native_dependency_licenses(args.dependency_license_dir.resolve(), package_root)
        write_qt_source_offer(package_root, qt_version=args.qt_version, qt_source_url=args.qt_source_url)
        if args.variant == "static":
            write_static_linking_notice(
                package_root,
                qt_version=args.qt_version,
                qt_source_url=args.qt_source_url,
                relinking_instructions_url=args.relinking_instructions_url,
            )
            copy_static_relinking_document(repository_root, package_root)
        create_zip(package_root, zip_path, Path(package_name))

    print(zip_path)
    print(f"{zip_path.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
