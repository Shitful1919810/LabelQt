#!/usr/bin/env python3
"""Create a Windows MSI installer from a deployed LabelQt directory.

This script is intended to run on Windows with WiX Toolset v4+ available as
the `wix` command.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import tempfile
import uuid
import xml.etree.ElementTree as ET
from pathlib import Path

from release_packaging import copy_release_notices, copy_release_tree, validate_deployed_release


WIX_NAMESPACE = "http://wixtoolset.org/schemas/v4/wxs"
WIX_UI_NAMESPACE = "http://wixtoolset.org/schemas/v4/wxs/ui"
UPGRADE_CODE = uuid.UUID("7f3c9c85-82be-5ff8-b590-df1919cb7b63")


def wix_tag(name: str) -> str:
    return f"{{{WIX_NAMESPACE}}}{name}"


def wix_ui_tag(name: str) -> str:
    return f"{{{WIX_UI_NAMESPACE}}}{name}"


def sanitize_identifier(value: str) -> str:
    sanitized = re.sub(r"[^A-Za-z0-9_]", "_", value)
    if not sanitized or sanitized[0].isdigit():
        sanitized = f"_{sanitized}"
    return sanitized


def stable_guid(relative_path: Path) -> str:
    return f"{{{uuid.uuid5(UPGRADE_CODE, relative_path.as_posix())}}}"


def msi_version(version: str) -> str:
    numeric_part = version.strip().removeprefix("v").split("-", 1)[0]
    parts = numeric_part.split(".")
    if not 1 <= len(parts) <= 3:
        raise ValueError(f"MSI version must contain one to three numeric fields: {version}")
    if not all(part.isdigit() for part in parts):
        raise ValueError(f"MSI version fields must be numeric: {version}")
    return ".".join(parts)


def rtf_escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace("{", "\\{").replace("}", "\\}").replace("\n", "\\par\n")


def write_license_rtf(repository_root: Path, destination: Path) -> None:
    license_text = (repository_root / "LICENSE.txt").read_text(encoding="utf-8")
    destination.write_text(r"{\rtf1\ansi\deff0" + "\n" + rtf_escape(license_text) + "\n}", encoding="utf-8")


class WixDocumentBuilder:
    def __init__(self, package_root: Path, product_name: str, manufacturer: str, version: str,
                 license_rtf_path: Path) -> None:
        ET.register_namespace("", WIX_NAMESPACE)
        ET.register_namespace("ui", WIX_UI_NAMESPACE)
        self.package_root = package_root
        self.product_name = product_name
        self.manufacturer = manufacturer
        self.version = version
        self.license_rtf_path = license_rtf_path
        self.component_refs: list[str] = []

    def build(self) -> ET.ElementTree:
        root = ET.Element(wix_tag("Wix"))
        package = ET.SubElement(
            root,
            wix_tag("Package"),
            {
                "Name": self.product_name,
                "Manufacturer": self.manufacturer,
                "Version": self.version,
                "UpgradeCode": stable_guid(Path("ProductUpgradeCode")),
                "Scope": "perMachine",
            },
        )
        ET.SubElement(
            package,
            wix_tag("MajorUpgrade"),
            {"DowngradeErrorMessage": "A newer version of LabelQt is already installed."},
        )
        ET.SubElement(package, wix_tag("MediaTemplate"), {"EmbedCab": "yes"})
        ET.SubElement(package, wix_tag("WixVariable"), {"Id": "WixUILicenseRtf", "Value": str(self.license_rtf_path)})
        ET.SubElement(package, wix_ui_tag("WixUI"), {"Id": "WixUI_InstallDir", "InstallDirectory": "INSTALLFOLDER"})

        program_files = ET.SubElement(package, wix_tag("StandardDirectory"), {"Id": "ProgramFiles64Folder"})
        install_folder = ET.SubElement(program_files, wix_tag("Directory"), {"Id": "INSTALLFOLDER", "Name": "LabelQt"})
        self.add_directory_contents(install_folder, self.package_root, Path())
        self.add_start_menu_shortcut(package)
        self.add_feature(package)
        return ET.ElementTree(root)

    def add_directory_contents(self, directory_element: ET.Element, source_directory: Path, relative_directory: Path) -> None:
        for path in sorted(source_directory.iterdir(), key=lambda item: (item.is_file(), item.name.lower())):
            relative_path = relative_directory / path.name
            if path.is_dir():
                directory_id = sanitize_identifier(f"dir_{relative_path.as_posix()}")
                child_directory = ET.SubElement(directory_element, wix_tag("Directory"), {"Id": directory_id, "Name": path.name})
                self.add_directory_contents(child_directory, path, relative_path)
            elif path.is_file():
                self.add_file_component(directory_element, path, relative_path)

    def add_file_component(self, directory_element: ET.Element, source_file: Path, relative_path: Path) -> None:
        component_id = sanitize_identifier(f"cmp_{relative_path.as_posix()}")
        file_id = sanitize_identifier(f"fil_{relative_path.as_posix()}")
        component = ET.SubElement(
            directory_element,
            wix_tag("Component"),
            {"Id": component_id, "Guid": stable_guid(relative_path)},
        )
        ET.SubElement(component, wix_tag("File"), {"Id": file_id, "Source": str(source_file), "KeyPath": "yes"})
        self.component_refs.append(component_id)

    def add_start_menu_shortcut(self, package: ET.Element) -> None:
        program_menu = ET.SubElement(package, wix_tag("StandardDirectory"), {"Id": "ProgramMenuFolder"})
        shortcut_directory = ET.SubElement(program_menu, wix_tag("Directory"), {"Id": "ApplicationProgramsFolder", "Name": "LabelQt"})
        component = ET.SubElement(
            shortcut_directory,
            wix_tag("Component"),
            {"Id": "StartMenuShortcut", "Guid": stable_guid(Path("StartMenuShortcut"))},
        )
        ET.SubElement(
            component,
            wix_tag("Shortcut"),
            {
                "Id": "ApplicationStartMenuShortcut",
                "Name": "LabelQt",
                "Target": "[INSTALLFOLDER]labelqt.exe",
                "WorkingDirectory": "INSTALLFOLDER",
            },
        )
        ET.SubElement(component, wix_tag("RemoveFolder"), {"Id": "ApplicationProgramsFolder", "On": "uninstall"})
        ET.SubElement(
            component,
            wix_tag("RegistryValue"),
            {
                "Root": "HKCU",
                "Key": "Software\\LabelQt\\LabelQt",
                "Name": "installed",
                "Type": "integer",
                "Value": "1",
                "KeyPath": "yes",
            },
        )

    def add_feature(self, package: ET.Element) -> None:
        feature = ET.SubElement(package, wix_tag("Feature"), {"Id": "MainFeature", "Title": "LabelQt", "Level": "1"})
        for component_id in self.component_refs:
            ET.SubElement(feature, wix_tag("ComponentRef"), {"Id": component_id})
        ET.SubElement(feature, wix_tag("ComponentRef"), {"Id": "StartMenuShortcut"})


def write_wxs(package_root: Path, wxs_path: Path, product_name: str, manufacturer: str, version: str,
              license_rtf_path: Path) -> None:
    builder = WixDocumentBuilder(package_root, product_name, manufacturer, version, license_rtf_path)
    tree = builder.build()
    ET.indent(tree, space="  ")
    tree.write(wxs_path, encoding="utf-8", xml_declaration=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path, help="Deployed Windows Release directory.")
    parser.add_argument("--version", required=True, help="Release version, for example v0.1.0-alpha.2.")
    parser.add_argument("--output-dir", default=Path("dist"), type=Path, help="Directory for the generated MSI.")
    parser.add_argument("--repo-root", default=Path.cwd(), type=Path, help="Repository root containing release notices.")
    parser.add_argument("--wix", default="wix", help="WiX command path. Defaults to wix from PATH.")
    parser.add_argument("--wix-ui-extension", default="WixToolset.UI.wixext",
                        help="WiX UI extension passed to wix build. Defaults to WixToolset.UI.wixext.")
    parser.add_argument("--manufacturer", default="LabelQt contributors", help="MSI manufacturer.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if os.name != "nt":
        raise SystemExit("MSI packaging is intended to run on Windows with WiX Toolset installed.")

    source = args.source.resolve()
    repository_root = args.repo_root.resolve()
    output_dir = args.output_dir.resolve()
    version = msi_version(args.version)
    package_name = f"LabelQt-{args.version}-windows-x64"
    msi_path = output_dir / f"{package_name}.msi"

    validate_deployed_release(source)
    output_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="labelqt-msi-package-") as temporary_directory:
        temporary_root = Path(temporary_directory)
        package_root = temporary_root / "LabelQt"
        wxs_path = temporary_root / "LabelQt.wxs"
        license_rtf_path = temporary_root / "LICENSE.rtf"
        copy_release_tree(source, package_root)
        copy_release_notices(repository_root, package_root)
        write_license_rtf(repository_root, license_rtf_path)
        write_wxs(package_root, wxs_path, "LabelQt", args.manufacturer, version, license_rtf_path)
        subprocess.run(
            [
                args.wix,
                "build",
                "-arch",
                "x64",
                "-ext",
                args.wix_ui_extension,
                "-out",
                str(msi_path),
                str(wxs_path),
            ],
            check=True,
        )

    print(msi_path)
    print(f"{msi_path.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
