import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sdk"))
from labelqt_task import Task


def main() -> None:
    task = Task.from_cli()
    prefix = task.parameters["prefix"]
    if not isinstance(prefix, str):
        raise ValueError("Prefix must be text")
    for label in task.active_labels:
        task.update(label, text=prefix + label.text)
    task.finish(summary=f"Proposed {len(task.active_labels)} text changes", quiet=True)


if __name__ == "__main__":
    main()
