import json
import pathlib
import sys
import tempfile
import unittest
from uuid import uuid4

sys.path.insert(0, str(pathlib.Path(__file__).parent / "official" / "sdk"))
from labelqt_task import Task


class TaskTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.output = pathlib.Path(self.directory.name) / "result.json"
        page_id, label_id, group_id = (str(uuid4()) for _ in range(3))
        self.payload = {
            "apiVersion": 2, "taskId": str(uuid4()), "sessionId": str(uuid4()),
            "projectId": str(uuid4()), "revision": "9007199254740993", "parameters": {},
            "selection": {}, "metadata": [], "groups": {group_id: "dialog"},
            "pages": [{"id": page_id}],
            "labels": [{"id": label_id, "pageId": page_id, "deleted": False, "text": "Original"}],
        }
        self.task = Task(self.payload, self.output)

    def test_native_ids_and_merged_fields(self):
        label = self.task.active_labels[0]
        self.task.update(label, text="New")
        self.task.update(label, position=(0.2, 0.3))
        self.task.finish(quiet=True)
        data = json.loads(self.output.read_text())
        self.assertNotIn("operations", data)
        self.assertEqual(data["taskId"], self.payload["taskId"])
        self.assertEqual(len(data["changes"]), 1)
        self.assertEqual(data["changes"][0]["labelId"], label.id)
        self.assertEqual(data["changes"][0]["position"], {"x": 0.2, "y": 0.3})
        self.assertEqual(label.text, "Original")
        self.assertEqual(self.task.revision, 9007199254740993)

    def test_invalid_fields_do_not_change_proposal(self):
        label = self.task.active_labels[0]
        for fields in ({"text": None}, {"position": (float("nan"), 0)}, {"position": (0, 2)},
                       {"group_id": ""}, {"deleted": 1}, {"typo": "oops"}, {}):
            with self.assertRaises(ValueError):
                self.task.update(label, **fields)
        self.task.finish()
        self.assertEqual(json.loads(self.output.read_text())["changes"], [])

    def test_one_shot_and_immutable_snapshot(self):
        with self.assertRaises(TypeError):
            self.task.parameters["test"] = "x"
        self.task.finish()
        with self.assertRaises(RuntimeError):
            self.task.finish()
        with self.assertRaises(RuntimeError):
            self.task.update(self.task.labels[0], text="x")

    def test_creation_has_no_fabricated_label_id(self):
        self.task.create(self.task.pages[0], text="", group_id=next(iter(self.task.groups)), position=(0, 1))
        self.task.finish()
        change = json.loads(self.output.read_text())["changes"][0]
        self.assertNotIn("labelId", change)
        self.assertEqual(change["text"], "")

    def test_legacy_version_rejected(self):
        self.payload["apiVersion"] = 1
        with self.assertRaises(ValueError):
            Task(self.payload, self.output)


if __name__ == "__main__":
    unittest.main()
