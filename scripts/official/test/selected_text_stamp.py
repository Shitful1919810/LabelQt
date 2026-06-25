#!/usr/bin/env python3
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sdk"))
from labelqt_automation import AutomationContext


def main() -> None:
    parser = argparse.ArgumentParser(description="Append a text stamp to selected labels.")
    parser.add_argument("--input", required=True, help="Path to the LabelQt automation input JSON.")
    parser.add_argument("--output", required=True, help="Path to write the automation output JSON.")
    args = parser.parse_args()

    ctx = AutomationContext.from_file(args.input)
    script_parameters = ctx.parameters
    stamp = str(script_parameters.get("stamp", " [automation]"))

    operations = []
    for label in ctx.selected_labels:
        operations.append(label.set_text(label.text + stamp))

    if operations:
        result_text = f"Stamped {len(operations)} selected label(s)."
    else:
        result_text = "Select one or more labels before running this script."
    ctx.write_output(args.output, "Set Label Text Test", result_text, operations)


if __name__ == "__main__":
    main()
