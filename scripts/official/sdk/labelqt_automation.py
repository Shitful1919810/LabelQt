"""Small object-oriented SDK for LabelQt automation scripts.

The SDK is intentionally dependency-free. It wraps the host application's JSON
contract with a compact API so official scripts can demonstrate a clean style
without hiding the data model from script authors.
"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path
from typing import Any


JsonObject = dict[str, Any]


def user_config_root(app_name: str = "LabelQt") -> Path:
    """Return a per-user config directory suitable for automation script data."""
    if sys.platform == "win32":
        base = Path(os.environ.get("APPDATA") or Path.home() / "AppData" / "Roaming")
        return base / app_name
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Application Support" / app_name
    return Path(os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config") / app_name


def script_config_path(script_file: str | Path, file_name: str = "config.json") -> Path:
    script_dir = Path(script_file).resolve().parent
    script_scope = script_dir.parent.name or "scripts"
    return user_config_root() / "automation" / script_scope / script_dir.name / file_name


def legacy_script_config_path(script_file: str | Path, file_name: str = "config.json") -> Path:
    return Path(script_file).resolve().with_name(file_name)


def load_script_config(script_file: str | Path, file_name: str = "config.json") -> JsonObject:
    for path in (script_config_path(script_file, file_name), legacy_script_config_path(script_file, file_name)):
        if not path.exists():
            continue
        try:
            with open(path, "r", encoding="utf-8") as config_file:
                config = json.load(config_file)
            return config if isinstance(config, dict) else {}
        except Exception:
            return {}
    return {}


def save_script_config(script_file: str | Path, config: JsonObject, file_name: str = "config.json") -> Path:
    path = script_config_path(script_file, file_name)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as config_file:
        json.dump(config, config_file, ensure_ascii=False, indent=2)
        config_file.write("\n")
    return path


class OperationBuilder:
    """Build LabelQt operations with stable field names."""

    @staticmethod
    def set_label_text(page: str, label_index: int, text: str) -> JsonObject:
        return {"type": "setLabelText", "page": page, "labelIndex": label_index, "text": text}

    @staticmethod
    def set_label_group(page: str, label_index: int, group: str) -> JsonObject:
        return {"type": "setLabelGroup", "page": page, "labelIndex": label_index, "group": group}

    @staticmethod
    def set_label_position(page: str, label_index: int, x: float, y: float) -> JsonObject:
        return {"type": "setLabelPosition", "page": page, "labelIndex": label_index, "x": x, "y": y}

    @staticmethod
    def delete_label(page: str, label_index: int) -> JsonObject:
        return {"type": "deleteLabel", "page": page, "labelIndex": label_index}

    @staticmethod
    def add_label(page: str, group: str, text: str, x: float, y: float) -> JsonObject:
        return {"type": "addLabel", "page": page, "group": group, "text": text, "x": x, "y": y}


class Label:
    """A non-owning wrapper around one LabelQt label JSON object."""

    def __init__(self, data: JsonObject, page_name: str = "") -> None:
        self.data = data
        self.page_name = page_name

    @property
    def index(self) -> int:
        return int_value(self.data.get("labelIndex"), -1)

    @property
    def visible_index(self) -> int:
        fallback = self.index + 1 if self.index >= 0 else 0
        return int_value(self.data.get("visibleIndex"), fallback)

    @property
    def group(self) -> str:
        return str(self.data.get("group", ""))

    @property
    def text(self) -> str:
        return str(self.data.get("text", ""))

    @property
    def x(self) -> float:
        return as_float(self.data.get("x"), 0.0)

    @property
    def y(self) -> float:
        return as_float(self.data.get("y"), 0.0)

    def has_text(self) -> bool:
        return bool(self.text.strip())

    def set_text(self, text: str) -> JsonObject:
        return OperationBuilder.set_label_text(self.page_name, self.index, text)

    def set_group(self, group: str) -> JsonObject:
        return OperationBuilder.set_label_group(self.page_name, self.index, group)

    def set_position(self, x: float, y: float) -> JsonObject:
        return OperationBuilder.set_label_position(self.page_name, self.index, x, y)

    def delete(self) -> JsonObject:
        return OperationBuilder.delete_label(self.page_name, self.index)


class Page:
    """A non-owning wrapper around one LabelQt page JSON object."""

    def __init__(self, data: JsonObject) -> None:
        self.data = data

    @property
    def index(self) -> int:
        return int_value(self.data.get("index"), -1)

    @property
    def number(self) -> int:
        return self.index + 1 if self.index >= 0 else 0

    @property
    def name(self) -> str:
        return str(self.data.get("name", "")).strip()

    @property
    def image_path(self) -> str:
        return str(self.data.get("imagePath", "")).strip()

    @property
    def labels(self) -> list[Label]:
        value = self.data.get("labels", [])
        if not isinstance(value, list):
            return []
        return [Label(label, self.name) for label in value if isinstance(label, dict)]

    def labels_with_text(self) -> list[Label]:
        return [label for label in self.labels if label.has_text()]

    def add_label(self, group: str, text: str, x: float, y: float) -> JsonObject:
        return OperationBuilder.add_label(self.name, group, text, x, y)


class AutomationContext:
    """Read LabelQt input JSON and write standard output JSON."""

    def __init__(self, payload: JsonObject) -> None:
        self.payload = payload
        self.ops = OperationBuilder()

    @classmethod
    def from_file(cls, path: str | Path) -> "AutomationContext":
        with open(path, "r", encoding="utf-8") as input_file:
            payload = json.load(input_file)
        return cls(payload if isinstance(payload, dict) else {})

    @property
    def parameters(self) -> JsonObject:
        value = self.payload.get("parameters", {})
        return value if isinstance(value, dict) else {}

    @property
    def project(self) -> JsonObject:
        value = self.payload.get("project", {})
        return value if isinstance(value, dict) else {}

    @property
    def pages(self) -> list[Page]:
        value = self.project.get("pages", [])
        if not isinstance(value, list):
            return []
        return [Page(page) for page in value if isinstance(page, dict)]

    @property
    def groups(self) -> list[str]:
        value = self.project.get("groups", [])
        return [str(group) for group in value] if isinstance(value, list) else []

    @property
    def current_page_info(self) -> JsonObject:
        context = self.payload.get("context", {})
        value = context.get("currentPage", {}) if isinstance(context, dict) else {}
        return value if isinstance(value, dict) else {}

    @property
    def current_page_name(self) -> str:
        page_name = str(self.current_page_info.get("name", "")).strip()
        if page_name:
            return page_name
        return str(self.project.get("currentPage", "")).strip()

    @property
    def current_page(self) -> Page | None:
        page_index = self.current_page_info.get("index", -1)
        if isinstance(page_index, int) and 0 <= page_index < len(self.pages):
            return self.pages[page_index]
        return self.page_by_name(self.current_page_name)

    @property
    def selected_labels(self) -> list[Label]:
        context = self.payload.get("context", {})
        labels = context.get("selectedLabels", []) if isinstance(context, dict) else []
        if not isinstance(labels, list):
            return []
        return [Label(label, self.current_page_name) for label in labels if isinstance(label, dict)]

    @property
    def selection(self) -> JsonObject:
        value = self.payload.get("selection", {})
        return value if isinstance(value, dict) else {}

    @property
    def has_selection(self) -> bool:
        return as_bool(self.selection.get("hasSelection"), False)

    def page_by_name(self, page_name: str) -> Page | None:
        return next((page for page in self.pages if page.name == page_name), None)

    def current_page_labels(self) -> list[Label]:
        page = self.current_page
        return page.labels if page is not None else []

    def selected_or_current_page_labels(self) -> tuple[Page | None, list[Label]]:
        selected = [label for label in self.selected_labels if label.has_text()]
        if selected:
            return self.current_page, selected
        page = self.current_page or (self.pages[0] if self.pages else None)
        return page, (page.labels_with_text() if page is not None else [])

    def page_range_labels(self, start_page: int, end_page: int) -> list[tuple[Page, list[Label]]]:
        targets: list[tuple[Page, list[Label]]] = []
        for page in self.page_range_pages(start_page, end_page):
            labels = page.labels_with_text()
            if labels:
                targets.append((page, labels))
        return targets

    def page_range_pages(self, start_page: int, end_page: int) -> list[Page]:
        if not self.pages:
            return []
        start_index, end_index = normalized_page_range(len(self.pages), start_page, end_page)
        return self.pages[start_index : end_index + 1]

    def write_output(
        self,
        path: str | Path,
        title: str,
        text: str,
        operations: list[JsonObject] | None = None,
        *,
        summary: str | None = None,
        quiet: bool = False,
    ) -> None:
        payload: JsonObject = {
            "apiVersion": 1,
            "summary": summary or (text.splitlines()[0] if text else title),
            "result": {"type": "message", "title": title, "text": text},
        }
        if operations is not None:
            payload["operations"] = operations
        if quiet:
            payload["quiet"] = True
        with open(path, "w", encoding="utf-8") as output_file:
            json.dump(payload, output_file, ensure_ascii=False, indent=2)


def normalized_page_range(page_count: int, start_page: int, end_page: int) -> tuple[int, int]:
    if page_count <= 0:
        return 0, -1
    if start_page > end_page:
        start_page, end_page = end_page, start_page
    start_index = max(0, min(page_count - 1, start_page - 1))
    end_index = max(0, min(page_count - 1, end_page - 1))
    return start_index, end_index


def int_value(value: Any, fallback: int) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return fallback


def as_float(value: Any, fallback: float) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return fallback


def as_bool(value: Any, fallback: bool = False) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        return value.strip().lower() in {"1", "true", "yes", "on"}
    if value is None:
        return fallback
    return bool(value)
