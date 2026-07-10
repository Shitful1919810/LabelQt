#!/usr/bin/env python3
"""Focused tests for release tree filtering and static compliance files."""

import tempfile
import unittest
from pathlib import Path

from release_packaging import (
    copy_native_dependency_licenses,
    prepare_windows_release_tree,
    write_qt_source_offer,
    write_static_linking_notice,
)
from package_windows_msi import msi_version


class ReleasePackagingTests(unittest.TestCase):
    def create_source_tree(self, root: Path) -> Path:
        source = root / "source"
        (source / "scripts" / "official" / "test").mkdir(parents=True)
        (source / "scripts" / "official" / "ocr_preview").mkdir(parents=True)
        (source / "labelqt.exe").write_bytes(b"shared")
        (source / "labelqt-static.exe").write_bytes(b"static")
        (source / "DiffMatchPatch.lib").write_bytes(b"development artifact")
        (source / "scripts" / "official" / "test" / "script.json").write_text("{}", encoding="utf-8")
        (source / "scripts" / "official" / "ocr_preview" / "script.json").write_text("{}", encoding="utf-8")
        return source

    def test_shared_tree_excludes_static_and_development_files(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            destination = root / "destination"
            prepare_windows_release_tree(self.create_source_tree(root), destination, "shared")

            self.assertEqual((destination / "labelqt.exe").read_bytes(), b"shared")
            self.assertFalse((destination / "labelqt-static.exe").exists())
            self.assertFalse((destination / "DiffMatchPatch.lib").exists())
            self.assertFalse((destination / "scripts" / "official" / "test").exists())
            self.assertTrue((destination / "scripts" / "official" / "ocr_preview" / "script.json").is_file())

    def test_static_tree_uses_stable_executable_name(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            destination = root / "destination"
            prepare_windows_release_tree(self.create_source_tree(root), destination, "static")

            self.assertEqual((destination / "labelqt.exe").read_bytes(), b"static")
            self.assertFalse((destination / "labelqt-static.exe").exists())

    def test_dependency_licenses_and_static_notice_are_written(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            license_root = root / "share"
            destination = root / "destination"
            (license_root / "qtbase").mkdir(parents=True)
            (license_root / "qtkeychain").mkdir(parents=True)
            (license_root / "qtbase" / "copyright").write_text("Qt license", encoding="utf-8")
            (license_root / "qtkeychain" / "copyright").write_text("QtKeychain license", encoding="utf-8")
            (license_root / "ignored.txt").write_text("not a license", encoding="utf-8")

            self.assertEqual(copy_native_dependency_licenses(license_root, destination), 2)
            self.assertTrue((destination / "licenses" / "native" / "qtbase" / "copyright").is_file())
            self.assertTrue((destination / "licenses" / "native" / "qtkeychain" / "copyright").is_file())
            self.assertFalse((destination / "licenses" / "native" / "ignored.txt").exists())

            write_qt_source_offer(
                destination,
                qt_version="6.8.3",
                qt_source_url="https://example.invalid/qt-source.tar.xz",
            )
            source_offer = (destination / "QT_SOURCE_OFFER.txt").read_text(encoding="utf-8")
            self.assertIn("Qt 6.8.3", source_offer)

            write_static_linking_notice(
                destination,
                qt_version="6.8.3",
                qt_source_url="https://example.invalid/qt-source.tar.xz",
                relinking_instructions_url="https://example.invalid/relinking",
            )
            notice = (destination / "STATIC_LINKING_NOTICE.txt").read_text(encoding="utf-8")
            self.assertIn("Qt 6.8.3", notice)
            self.assertIn("qt-source.tar.xz", notice)

    def test_dependency_license_copy_rejects_missing_required_package(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            license_root = root / "share"
            destination = root / "destination"
            (license_root / "qtbase").mkdir(parents=True)
            (license_root / "qtbase" / "copyright").write_text("Qt license", encoding="utf-8")

            with self.assertRaisesRegex(FileNotFoundError, "qtkeychain"):
                copy_native_dependency_licenses(license_root, destination)

    def test_msi_versions_preserve_prerelease_order(self) -> None:
        versions = [
            msi_version("v0.1.0-alpha.2"),
            msi_version("v0.1.0-beta.1"),
            msi_version("v0.1.0-rc.1"),
            msi_version("v0.1.0"),
            msi_version("v0.1.1-alpha.1"),
        ]
        numeric_versions = [tuple(int(part) for part in version.split(".")) for version in versions]
        self.assertEqual(numeric_versions, sorted(numeric_versions))
        self.assertEqual(len(numeric_versions), len(set(numeric_versions)))


if __name__ == "__main__":
    unittest.main()
