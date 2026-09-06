"""LabelQt v2 task SDK. Snapshots are values; mutations are proposed, never live writes."""

from __future__ import annotations

import argparse
import json
import math
import os
import tempfile
from dataclasses import dataclass
from pathlib import Path
from types import MappingProxyType
from typing import Any, Mapping
from uuid import UUID


def _id(value: Any) -> str:
    if not isinstance(value, str) or str(UUID(value)) != value or UUID(value).int == 0:
        raise ValueError("Expected a non-null canonical UUID")
    return value


def _position(value: Any) -> tuple[float, float]:
    if not isinstance(value, (tuple, list)) or len(value) != 2:
        raise ValueError("Position must contain x and y")
    if any(type(v) not in (int, float) or not math.isfinite(v) or not 0 <= v <= 1 for v in value):
        raise ValueError("Coordinates must be finite and normalized to [0, 1]")
    return (float(value[0]), float(value[1]))


def _freeze(value: Any) -> Any:
    if isinstance(value, dict):
        return MappingProxyType({key: _freeze(item) for key, item in value.items()})
    if isinstance(value, list):
        return tuple(_freeze(item) for item in value)
    return value


@dataclass(frozen=True)
class LabelSnapshot:
    id: str
    page_id: str
    deleted: bool
    text: str | None
    group_id: str | None
    position: tuple[float, float] | None


@dataclass(frozen=True)
class PageSnapshot:
    id: str
    image_name: str | None
    image_path: str | None


class Task:
    """One captured scope, one atomic result. Missing fields were not requested."""

    def __init__(self, payload: dict[str, Any], output: str | Path) -> None:
        if type(payload.get("apiVersion")) is not int or payload["apiVersion"] != 2:
            raise ValueError("Only LabelQt automation API v2 is supported")
        self.task_id = _id(payload["taskId"])
        self.session_id = _id(payload["sessionId"])
        self.project_id = _id(payload["projectId"])
        self.revision = int(payload["revision"])
        self.parameters: Mapping[str, Any] = _freeze(payload["parameters"])
        self.selection: Mapping[str, Any] = _freeze(payload["selection"])
        self.groups: Mapping[str, str] = _freeze(payload["groups"])
        self.metadata = tuple(payload["metadata"])
        self.pages = tuple(
            PageSnapshot(_id(page["id"]), page.get("imageSource", {}).get("name"),
                         page.get("imageSource", {}).get("path"))
            for page in payload["pages"]
        )
        self.labels = tuple(
            LabelSnapshot(_id(label["id"]), _id(label["pageId"]), label["deleted"],
                          label.get("text"), label.get("groupId"),
                          _position([label["position"]["x"], label["position"]["y"]])
                          if "position" in label else None)
            for label in payload["labels"]
        )
        self._labels = {label.id: label for label in self.labels}
        self._updates: dict[str, dict[str, Any]] = {}
        self._creates: list[dict[str, Any]] = []
        self._output = Path(output)
        self._finished = False

    @classmethod
    def from_cli(cls) -> Task:
        parser = argparse.ArgumentParser()
        parser.add_argument("--input", type=Path, required=True)
        parser.add_argument("--output", type=Path, required=True)
        args = parser.parse_args()
        with args.input.open(encoding="utf-8") as stream:
            return cls(json.load(stream), args.output)

    @property
    def active_labels(self) -> tuple[LabelSnapshot, ...]:
        return tuple(label for label in self.labels if not label.deleted)

    def _check_open(self) -> None:
        if self._finished:
            raise RuntimeError("This task result has already been written")

    @staticmethod
    def _fields(fields: dict[str, Any]) -> dict[str, Any]:
        if not fields or fields.keys() - {"text", "group_id", "position", "deleted"}:
            raise ValueError("Expected text, group_id, position or deleted")
        result: dict[str, Any] = {}
        for key, value in fields.items():
            if key == "text":
                if not isinstance(value, str):
                    raise ValueError("Text must be a string")
                result["text"] = value
            elif key == "group_id":
                result["groupId"] = _id(value)
            elif key == "position":
                x, y = _position(value)
                result["position"] = {"x": x, "y": y}
            else:
                if type(value) is not bool:
                    raise ValueError("Deleted must be a boolean")
                result["deleted"] = value
        return result

    def update(self, label: LabelSnapshot, **fields: Any) -> None:
        self._check_open()
        if self._labels.get(label.id) != label or label.deleted:
            raise ValueError("Label is not an active member of this snapshot")
        values = self._fields(fields)
        change = self._updates.setdefault(label.id, {"pageId": label.page_id, "labelId": label.id})
        change.update(values)

    def create(self, page: PageSnapshot, *, text: str, group_id: str,
               position: tuple[float, float]) -> None:
        self._check_open()
        if page not in self.pages:
            raise ValueError("Page is not in this snapshot")
        self._creates.append({"pageId": page.id, **self._fields(
            {"text": text, "group_id": group_id, "position": position})})

    def finish(self, *, summary: str = "", title: str = "", text: str = "", quiet: bool = False) -> None:
        """Write a proposal atomically. Only the host can report whether it was committed."""
        self._check_open()
        if not all(isinstance(value, str) for value in (summary, title, text)) or type(quiet) is not bool:
            raise ValueError("Invalid presentation fields")
        changes = [*self._updates.values(), *self._creates]
        if len(changes) > 10000 or sum(len(c.get("text", "").encode("utf-16-le")) // 2 for c in changes) > 4 * 1024 * 1024:
            raise ValueError("Task result exceeds host limits")
        encoded = json.dumps({
            "apiVersion": 2, "taskId": self.task_id, "sessionId": self.session_id,
            "changes": changes, "summary": summary, "title": title, "text": text, "quiet": quiet,
        }, ensure_ascii=False, allow_nan=False).encode("utf-8")
        if len(encoded) > 16 * 1024 * 1024:
            raise ValueError("Task output exceeds 16 MiB")
        fd, temporary = tempfile.mkstemp(prefix=".labelqt-result-", dir=self._output.parent)
        try:
            with os.fdopen(fd, "wb") as stream:
                stream.write(encoded)
            os.replace(temporary, self._output)
        finally:
            Path(temporary).unlink(missing_ok=True)
        self._finished = True
