#!/usr/bin/env python3
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sdk"))
from labelqt_automation import AutomationContext


def main() -> None:
    parser = argparse.ArgumentParser(description="Count characters in all LabelQt labels.")
    parser.add_argument("--input", required=True, help="Path to the LabelQt automation input JSON.")
    parser.add_argument("--output", required=True, help="Path to write the automation output JSON.")
    args = parser.parse_args()

    ctx = AutomationContext.from_file(args.input)
    project_pages = ctx.pages
    label_count = 0
    character_count = 0
    page_count = len(project_pages)
    group_counts = {}

    for page in project_pages:
        for label in page.labels:
            group = label.group
            text = label.text
            label_count += 1
            character_count += len(text)
            group_count = group_counts.setdefault(group, {"labels": 0, "characters": 0})
            group_count["labels"] += 1
            group_count["characters"] += len(text)

    summary = f"Counted {character_count} characters in {label_count} labels across {page_count} pages."
    group_lines = []
    for group in sorted(group_counts):
        display_group = group if group else "(empty group)"
        counts = group_counts[group]
        group_lines.append(
            f"{display_group}: {counts['characters']} characters in {counts['labels']} labels"
        )
    result_text = summary
    if group_lines:
        result_text = summary + "\n\nBy group:\n" + "\n".join(group_lines)

    ctx.write_output(args.output, "Label Word Count", result_text)


if __name__ == "__main__":
    main()
