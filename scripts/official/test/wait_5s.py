#!/usr/bin/env python3
import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sdk"))
from labelqt_automation import AutomationContext


def main() -> None:
    parser = argparse.ArgumentParser(description="Wait for five seconds to test cancellation and disabled UI state.")
    parser.add_argument("--input", required=True, help="Path to the LabelQt automation input JSON.")
    parser.add_argument("--output", required=True, help="Path to write the automation output JSON.")
    args = parser.parse_args()
    ctx = AutomationContext.from_file(args.input)

    time.sleep(5)
    ctx.write_output(args.output, "Wait 5s", "Waited 5 seconds.", quiet=True)


if __name__ == "__main__":
    main()
