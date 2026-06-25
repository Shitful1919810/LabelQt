#!/usr/bin/env python3
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sdk"))
from labelqt_automation import AutomationContext


def main() -> None:
    parser = argparse.ArgumentParser(description="Swap all labels between two LabelQt groups.")
    parser.add_argument("--input", required=True, help="Path to the LabelQt automation input JSON.")
    parser.add_argument("--output", required=True, help="Path to write the automation output JSON.")
    args = parser.parse_args()

    ctx = AutomationContext.from_file(args.input)
    script_parameters = ctx.parameters
    group_a = script_parameters.get("groupA", "")
    group_b = script_parameters.get("groupB", "")
    operations = []

    if group_a and group_b and group_a != group_b:
        for page in ctx.pages:
            for label in page.labels:
                if label.group == group_a:
                    operations.append(label.set_group(group_b))
                elif label.group == group_b:
                    operations.append(label.set_group(group_a))

    count = len(operations)
    if not group_a or not group_b:
        result_text = "Please select two groups before running this script."
    elif group_a == group_b:
        result_text = "The two selected groups are the same, so no labels were changed."
    else:
        result_text = f"Swapped {count} labels between {group_a} and {group_b}."

    ctx.write_output(args.output, "Swap Label Groups", result_text, operations)


if __name__ == "__main__":
    main()
