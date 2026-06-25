#!/usr/bin/env python3
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sdk"))
from labelqt_automation import AutomationContext, as_bool, save_script_config


def main() -> None:
    parser = argparse.ArgumentParser(description="Configure LabelQt AI translation scripts.")
    parser.add_argument("--input", required=True, help="Path to the LabelQt automation input JSON.")
    parser.add_argument("--output", required=True, help="Path to write the automation output JSON.")
    args = parser.parse_args()

    ctx = AutomationContext.from_file(args.input)
    parameters = ctx.parameters
    config = {
        "baseUrl": str(parameters.get("baseUrl") or "https://api.deepseek.com").strip().rstrip("/")
        or "https://api.deepseek.com",
        "model": str(parameters.get("model") or "deepseek-v4-flash").strip() or "deepseek-v4-flash",
        "targetLanguage": str(parameters.get("targetLanguage") or "Simplified Chinese").strip()
        or "Simplified Chinese",
        "style": str(parameters.get("style") or "Natural manga dialogue").strip() or "Natural manga dialogue",
        "temperature": str(parameters.get("temperature") or "0.4").strip() or "0.4",
        "customPrompt": str(parameters.get("customPrompt") or "").strip(),
        "showResult": as_bool(parameters.get("showResult", True)),
    }

    config_path = save_script_config(__file__, config)

    ctx.write_output(
        args.output,
        "AI Translation Configuration",
        "\n".join(
            [
                f"Saved configuration to {config_path}",
                "API key: saved in the system keychain by LabelQt",
                f"Base URL: {config['baseUrl']}",
                f"Model: {config['model']}",
                f"Target language: {config['targetLanguage']}",
                f"Style: {config['style']}",
                "Custom prompt: configured" if config["customPrompt"] else "Custom prompt: not configured",
                f"showResult: {config['showResult']}",
            ]
        ),
        operations=[],
        summary="AI translation configuration saved.",
    )


if __name__ == "__main__":
    main()
