#!/usr/bin/env python3
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sdk"))
from labelqt_automation import AutomationContext


def main() -> None:
    parser = argparse.ArgumentParser(description="Delete selected labels.")
    parser.add_argument("--input", required=True, help="Path to the LabelQt automation input JSON.")
    parser.add_argument("--output", required=True, help="Path to write the automation output JSON.")
    args = parser.parse_args()

    ctx = AutomationContext.from_file(args.input)

    operations = []
    for label in ctx.selected_labels:
        operations.append(label.delete())

    if operations:
        result_text = f"Deleted {len(operations)} selected label(s). Use Undo to restore them."
    else:
        result_text = "Select one or more labels before running this script."

    ctx.write_output(args.output, "Delete Label Test", result_text, operations)


if __name__ == "__main__":
    main()
