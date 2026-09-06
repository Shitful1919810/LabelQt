import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sdk"))
from labelqt_task import Task


def main() -> None:
    task = Task.from_cli()
    if not task.groups:
        raise ValueError("Create a group before running this example")
    task.create(task.pages[0], text="Example", group_id=next(iter(task.groups)), position=(0.5, 0.5))
    task.finish(summary="Proposed one new label", quiet=True)


if __name__ == "__main__":
    main()
