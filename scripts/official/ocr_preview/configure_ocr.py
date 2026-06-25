#!/usr/bin/env python3
import argparse
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sdk"))
from labelqt_automation import AutomationContext, as_bool, load_script_config, save_script_config


def read_existing_config() -> dict[str, Any]:
    return load_script_config(__file__)


def main() -> None:
    parser = argparse.ArgumentParser(description="Configure LabelQt OCR automation scripts.")
    parser.add_argument("--input", required=True, help="Path to the LabelQt automation input JSON.")
    parser.add_argument("--output", required=True, help="Path to write the automation output JSON.")
    args = parser.parse_args()

    ctx = AutomationContext.from_file(args.input)
    parameters = ctx.parameters

    config = read_existing_config()
    config.update(
        {
            "engine": str(parameters.get("engine", config.get("engine", "paddle"))),
            "language": str(parameters.get("language", config.get("language", "japan"))),
            "device": str(parameters.get("device", config.get("device", "cpu"))),
            "mangaModelPath": str(parameters.get("mangaModelPath", config.get("mangaModelPath", ""))),
            "defaultGroup": str(parameters.get("defaultGroup", config.get("defaultGroup", "框内"))),
            "rightToLeft": as_bool(parameters.get("rightToLeft", config.get("rightToLeft", True))),
            "showResult": as_bool(parameters.get("showResult", config.get("showResult", True))),
        }
    )

    config_path = save_script_config(__file__, config)

    lines = [
        f"Saved OCR configuration to {config_path}",
        "",
        f"engine: {config['engine']}",
        f"language: {config['language']}",
        f"device: {config['device']}",
        f"mangaModelPath: {config['mangaModelPath'] or '(auto/local HuggingFace cache)'}",
        f"defaultGroup: {config['defaultGroup']}",
        f"rightToLeft: {config['rightToLeft']}",
        f"showResult: {config['showResult']}",
    ]
    ctx.write_output(args.output, "OCR Configuration", "\n".join(lines), summary="OCR configuration saved.")


if __name__ == "__main__":
    main()
