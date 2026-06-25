#!/usr/bin/env python3
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sdk"))
from labelqt_automation import AutomationContext, as_float


def clamp_unit(value: float) -> float:
    return max(0.0, min(1.0, value))


def main() -> None:
    parser = argparse.ArgumentParser(description="Nudge selected label marker positions.")
    parser.add_argument("--input", required=True, help="Path to the LabelQt automation input JSON.")
    parser.add_argument("--output", required=True, help="Path to write the automation output JSON.")
    args = parser.parse_args()

    ctx = AutomationContext.from_file(args.input)
    script_parameters = ctx.parameters
    dx = as_float(script_parameters.get("dx"), 0.02)
    dy = as_float(script_parameters.get("dy"), 0.02)

    operations = []
    for label in ctx.selected_labels:
        operations.append(label.set_position(clamp_unit(label.x + dx), clamp_unit(label.y + dy)))

    if operations:
        result_text = f"Nudged {len(operations)} selected label marker(s) by dx={dx}, dy={dy}."
    else:
        result_text = "Select one or more labels before running this script."
    ctx.write_output(args.output, "Set Label Position Test", result_text, operations)


if __name__ == "__main__":
    main()
