import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sdk"))
from labelqt_task import Task


def main() -> None:
    task = Task.from_cli()
    count = sum(len(label.text or "") for label in task.active_labels)
    task.finish(title="Text Count", text=f"{len(task.active_labels)} labels, {count} characters")


if __name__ == "__main__":
    main()
