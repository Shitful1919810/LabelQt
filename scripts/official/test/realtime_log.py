#!/usr/bin/env python3
import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sdk"))
from labelqt_automation import AutomationContext


def main() -> None:
    parser = argparse.ArgumentParser(description="Emit stdout and stderr lines for automation log testing.")
    parser.add_argument("--input", required=True, help="Path to the LabelQt automation input JSON.")
    parser.add_argument("--output", required=True, help="Path to write the automation output JSON.")
    args = parser.parse_args()
    ctx = AutomationContext.from_file(args.input)

    for index in range(1, 6):
        print(f"stdout progress {index}/5", flush=True)
        print(f"stderr progress {index}/5", file=sys.stderr, flush=True)
        time.sleep(0.5)

    ctx.write_output(args.output, "Realtime Log Test", "Realtime log test finished.")


if __name__ == "__main__":
    main()
