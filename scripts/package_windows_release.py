#!/usr/bin/env python3
"""Create a Windows release zip from a deployed LabelQt directory."""


import argparse
import tempfile
import zipfile
from pathlib import Path

from release_packaging import copy_release_notices, copy_release_tree, validate_deployed_release


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
    parser.add_argument("--output-dir", default=Path("dist"), type=Path, help="Directory for the generated zip.")
    parser.add_argument("--repo-root", default=Path.cwd(), type=Path, help="Repository root containing release notices.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source = args.source.resolve()
    repository_root = args.repo_root.resolve()
    output_dir = args.output_dir.resolve()
    package_name = f"LabelQt-{args.version}-windows-x64"
    zip_path = output_dir / f"{package_name}.zip"

    validate_deployed_release(source)

    with tempfile.TemporaryDirectory(prefix="labelqt-windows-package-") as temporary_directory:
        package_root = Path(temporary_directory) / package_name
        copy_release_tree(source, package_root)
        copy_release_notices(repository_root, package_root)
        create_zip(package_root, zip_path, Path(package_name))

    print(zip_path)
    print(f"{zip_path.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
