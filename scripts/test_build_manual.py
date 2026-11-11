#!/usr/bin/env python3
"""Screenshot discovery and partial/strict manual build contracts."""

import contextlib
import io
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import build_manual


class ManualBuildTests(unittest.TestCase):
    def test_query_uses_structured_typst_metadata_and_deduplicates(self):
        result = subprocess.CompletedProcess([], 0, '["a.png", "b.png", "a.png"]')
        with patch.object(build_manual.subprocess, "run", return_value=result):
            self.assertEqual(build_manual.referenced_screenshots("typst", Path("main.typ")), ["a.png", "b.png"])

    def test_invalid_asset_metadata_is_rejected(self):
        for data in ('["../secret.png"]', '[null]', '{}'):
            with self.subTest(data=data), patch.object(
                build_manual.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, data)
            ):
                with self.assertRaises(ValueError):
                    build_manual.referenced_screenshots("typst", Path("main.typ"))

    def test_availability_is_read_from_disk_each_time(self):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory)
            files = ["a.png", "b.png"]
            self.assertEqual(build_manual.partition_screenshots(files, assets), ([], files))
            (assets / "a.png").write_bytes(b"test")
            (assets / "b.png").mkdir()
            self.assertEqual(build_manual.partition_screenshots(files, assets), (["a.png"], ["b.png"]))

    def test_partial_strict_and_listing_modes(self):
        for mode in ("--with-screenshots", "--require-screenshots", "--list-missing-screenshots"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                assets = root / "manual" / "assets"
                assets.mkdir(parents=True)
                (assets / "a.png").write_bytes(b"test")
                output = root / "existing.pdf"
                output.write_bytes(b"previous PDF")
                with patch.object(build_manual, "repo_root", return_value=root), \
                     patch.object(build_manual.shutil, "which", return_value="typst"), \
                     patch.object(build_manual, "referenced_screenshots", return_value=["a.png", "b.png"]), \
                     patch.object(build_manual.subprocess, "run") as compile_call, \
                     patch("sys.argv", ["build_manual.py", mode, "--output", str(output)]), \
                     contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                    status = build_manual.main()
                self.assertEqual(output.read_bytes(), b"previous PDF")
                self.assertEqual(status, 1 if mode == "--require-screenshots" else 0)
                if mode == "--with-screenshots":
                    command = compile_call.call_args.args[0]
                    self.assertIn('screenshot-files=["a.png"]', command)
                    self.assertIn("screenshots=true", command)
                else:
                    compile_call.assert_not_called()


if __name__ == "__main__":
    unittest.main()
