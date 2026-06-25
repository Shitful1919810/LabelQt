#!/usr/bin/env python3
import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sdk"))
from labelqt_automation import AutomationContext, Label, Page, load_script_config


DEFAULT_CONFIG = {
    "baseUrl": "https://api.deepseek.com",
    "model": "deepseek-v4-flash",
    "targetLanguage": "Simplified Chinese",
    "style": "Natural manga dialogue",
    "temperature": "0.4",
    "customPrompt": "",
    "showResult": "true",
}


def log(message: str) -> None:
    print(f"[ai_translate] {message}", flush=True)


def load_config() -> dict[str, str]:
    config = dict(DEFAULT_CONFIG)
    loaded = load_script_config(__file__)
    for key, value in loaded.items():
        if value is not None:
            config[key] = str(value)
    return config


def config_bool(config: dict[str, str], key: str, default: bool) -> bool:
    value = config.get(key)
    if value is None:
        return default
    return str(value).strip().lower() in {"1", "true", "yes", "on"}


def page_range_targets(ctx: AutomationContext, parameters: dict[str, Any]) -> list[tuple[Page, list[Label]]]:
    if not ctx.pages:
        raise RuntimeError("The project has no pages.")

    def parse_page_number(key: str, default: int) -> int:
        try:
            return int(str(parameters.get(key, default)).strip())
        except ValueError as error:
            raise RuntimeError(f"{key} must be a 1-based page number.") from error

    start_page = parse_page_number("startPage", 1)
    end_page = parse_page_number("endPage", start_page)
    return ctx.page_range_labels(start_page, end_page)


def prompt_for_labels(
    labels: list[Label],
    *,
    page_name: str,
    target_language: str,
    style: str,
    custom_prompt: str,
    include_analysis: bool,
) -> list[dict[str, str]]:
    items = [
        {
            "labelIndex": label.index,
            "visibleIndex": label.visible_index,
            "group": label.group,
            "text": label.text,
        }
        for label in labels
    ]
    analysis_rule = (
        "Also provide a short analysis field explaining tone, ambiguity, and wording choices."
        if include_analysis
        else "Set analysis to an empty string."
    )
    system_prompt = (
        "You are a professional Japanese manga localization translator working on a Chinese scanlation project. "
        "Return strict JSON only, without Markdown, comments, or extra prose."
    )
    rules = [
        "Translate Japanese manga dialogue into the target language for direct typesetting.",
        "Keep each translation close to the original line length whenever possible so it fits the existing speech bubble.",
        "Use common polished manga scanlation wording: natural, concise, readable, and emotionally accurate.",
        "Avoid trendy slang, internet memes, overly modern buzzwords, and unnecessarily embellished phrasing.",
        "Preserve character voice, relationships, honorific nuance, hesitation, yelling, and sentence fragments.",
        "Translate each item independently, but use all labels on the page as context.",
        "Do not invent missing context.",
        "Do not add commas, periods, or other sentence punctuation when the source line has none. Keep punctuation sparse and only preserve or add marks that are necessary for the original tone, such as question marks, exclamation marks, ellipses, or dashes.",
        "Prefer punctuation and line breaks that are easy to typeset; preserve intentional line breaks only when useful.",
        "If a source line is very short, keep the translation similarly short instead of explaining it.",
        analysis_rule,
        "Return JSON with shape: {\"items\":[{\"labelIndex\":number,\"translation\":string,\"analysis\":string}]}",
    ]
    if custom_prompt.strip():
        rules.append(f"User custom translation instructions: {custom_prompt.strip()}")

    user_prompt = {
        "task": "translate_labelqt_labels",
        "page": page_name,
        "targetLanguage": target_language,
        "style": style,
        "rules": rules,
        "labels": items,
    }
    return [
        {"role": "system", "content": system_prompt},
        {"role": "user", "content": json.dumps(user_prompt, ensure_ascii=False)},
    ]


def prompt_for_page_range(
    target_pages: list[tuple[Page, list[Label]]],
    *,
    target_language: str,
    style: str,
    custom_prompt: str,
) -> list[dict[str, str]]:
    pages = []
    for page, labels in target_pages:
        pages.append(
            {
                "page": page.name,
                "pageNumber": page.number,
                "labels": [
                    {
                        "labelIndex": label.index,
                        "visibleIndex": label.visible_index,
                        "group": label.group,
                        "text": label.text,
                    }
                    for label in labels
                ],
            }
        )

    system_prompt = (
        "You are a professional Japanese manga localization translator working on a Chinese scanlation project. "
        "Return strict JSON only, without Markdown, comments, or extra prose."
    )
    rules = [
        "Translate all labels in the selected page range into the target language for direct typesetting.",
        "Use every page in this request as story context: keep names, pronouns, tone, terminology, jokes, and running references consistent across pages.",
        "Preserve page-local dialogue flow. Later pages may depend on earlier pages, but do not invent context that is not present.",
        "Keep each translation close to the original line length whenever possible so it fits the existing speech bubble.",
        "Use common polished manga scanlation wording: natural, concise, readable, and emotionally accurate.",
        "Avoid trendy slang, internet memes, overly modern buzzwords, and unnecessarily embellished phrasing.",
        "Preserve character voice, relationships, honorific nuance, hesitation, yelling, and sentence fragments.",
        "Do not add commas, periods, or other sentence punctuation when the source line has none. Keep punctuation sparse and only preserve or add marks that are necessary for the original tone, such as question marks, exclamation marks, ellipses, or dashes.",
        "Prefer punctuation and line breaks that are easy to typeset; preserve intentional line breaks only when useful.",
        "Return one output item per input label. Do not omit labels.",
        "Return JSON with shape: {\"items\":[{\"page\":string,\"labelIndex\":number,\"translation\":string,\"analysis\":string}]}",
        "Set analysis to an empty string.",
    ]
    if custom_prompt.strip():
        rules.append(f"User custom translation instructions: {custom_prompt.strip()}")

    user_prompt = {
        "task": "translate_labelqt_page_range",
        "targetLanguage": target_language,
        "style": style,
        "rules": rules,
        "pages": pages,
    }
    return [
        {"role": "system", "content": system_prompt},
        {"role": "user", "content": json.dumps(user_prompt, ensure_ascii=False)},
    ]


def parse_temperature(value: str) -> float:
    try:
        return max(0.0, min(2.0, float(value)))
    except ValueError:
        return 0.4


def call_deepseek(config: dict[str, str], messages: list[dict[str, str]]) -> dict[str, Any]:
    api_key = os.environ.get("DEEPSEEK_API_KEY", "").strip()
    if not api_key:
        raise RuntimeError("Missing DeepSeek API key. Run Configure AI Translation first.")

    base_url = config.get("baseUrl", DEFAULT_CONFIG["baseUrl"]).strip().rstrip("/")
    url = f"{base_url}/chat/completions"
    body = {
        "model": config.get("model", DEFAULT_CONFIG["model"]).strip() or DEFAULT_CONFIG["model"],
        "messages": messages,
        "temperature": parse_temperature(config.get("temperature", DEFAULT_CONFIG["temperature"])),
        "response_format": {"type": "json_object"},
    }
    data = json.dumps(body, ensure_ascii=False).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=data,
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
        method="POST",
    )
    log(f"Calling DeepSeek model={body['model']} url={url}")
    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            response_body = response.read().decode("utf-8")
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"DeepSeek API HTTP {error.code}: {detail}") from error
    except urllib.error.URLError as error:
        raise RuntimeError(f"DeepSeek API request failed: {error.reason}") from error

    result = json.loads(response_body)
    content = result.get("choices", [{}])[0].get("message", {}).get("content", "")
    if not content:
        raise RuntimeError("DeepSeek API returned an empty message.")
    parsed = parse_model_json(content)
    if not isinstance(parsed.get("items"), list):
        raise RuntimeError("DeepSeek API returned JSON, but it did not contain an items array.")
    return parsed


def parse_model_json(content: str) -> dict[str, Any]:
    try:
        return json.loads(content)
    except json.JSONDecodeError:
        match = re.search(r"\{.*\}", content, re.S)
        if match:
            return json.loads(match.group(0))
        raise


def result_items_by_label_index(result: dict[str, Any]) -> dict[int, dict[str, str]]:
    items: dict[int, dict[str, str]] = {}
    for item in result.get("items", []):
        try:
            label_index = int(item.get("labelIndex"))
        except (TypeError, ValueError):
            continue
        translation = str(item.get("translation", "")).strip()
        if not translation:
            continue
        items[label_index] = {
            "translation": translation,
            "analysis": str(item.get("analysis", "")).strip(),
        }
    return items


def result_items_by_page_and_label_index(result: dict[str, Any]) -> dict[tuple[str, int], dict[str, str]]:
    items: dict[tuple[str, int], dict[str, str]] = {}
    for item in result.get("items", []):
        try:
            label_index = int(item.get("labelIndex"))
        except (TypeError, ValueError):
            continue
        page_name = str(item.get("page", "")).strip()
        translation = str(item.get("translation", "")).strip()
        if not page_name or not translation:
            continue
        items[(page_name, label_index)] = {
            "translation": translation,
            "analysis": str(item.get("analysis", "")).strip(),
        }
    return items


def preview_text(labels: list[Label], translations: dict[int, dict[str, str]], include_analysis: bool) -> str:
    lines: list[str] = []
    for label in labels:
        translated = translations.get(label.index, {}).get("translation", "")
        analysis = translations.get(label.index, {}).get("analysis", "")
        lines.append(f"#{label.visible_index}")
        lines.append(f"Original: {label.text}")
        lines.append(f"Translation: {translated or '[missing]'}")
        if include_analysis and analysis:
            lines.append(f"Analysis: {analysis}")
        lines.append("")
    return "\n".join(lines).strip()


def main() -> None:
    parser = argparse.ArgumentParser(description="Translate LabelQt labels with DeepSeek.")
    parser.add_argument("--input", required=True, help="Path to the LabelQt automation input JSON.")
    parser.add_argument("--output", required=True, help="Path to write the automation output JSON.")
    args = parser.parse_args()

    ctx = AutomationContext.from_file(args.input)

    action = os.environ.get("LABELQT_TRANSLATION_ACTION", "preview").lower()
    parameters = ctx.parameters
    include_analysis = parameters.get("outputMode") == "translationWithAnalysis"
    config = load_config()
    try:
        if action == "apply-page-range":
            target_pages = page_range_targets(ctx, parameters)
        else:
            page, labels = ctx.selected_or_current_page_labels()
            target_pages = [(page, labels)] if page is not None and labels else []
    except Exception as error:
        ctx.write_output(args.output, "AI Translation Failed", f"AI translation failed:\n{error}")
        return

    if not target_pages:
        ctx.write_output(args.output, "AI Translation", "No labels to translate.")
        return

    all_operations: list[dict[str, Any]] = []
    preview_sections: list[str] = []
    translated_label_count = 0

    if action == "apply-page-range":
        log(f"Translating {sum(len(labels) for _, labels in target_pages)} label(s) across {len(target_pages)} page(s)")
        messages = prompt_for_page_range(
            target_pages,
            target_language=config.get("targetLanguage", DEFAULT_CONFIG["targetLanguage"]),
            style=config.get("style", DEFAULT_CONFIG["style"]),
            custom_prompt=config.get("customPrompt", DEFAULT_CONFIG["customPrompt"]),
        )
        try:
            model_result = call_deepseek(config, messages)
            translations = result_items_by_page_and_label_index(model_result)
        except Exception as error:
            ctx.write_output(args.output, "AI Translation Failed", f"AI page range translation failed:\n{error}")
            return

        for page, labels in target_pages:
            for label in labels:
                translation = translations.get((page.name, label.index), {}).get("translation", "")
                if not translation or label.index < 0 or translation == label.text:
                    continue
                all_operations.append(label.set_text(translation))
        translated_label_count = len(all_operations)
        ctx.write_output(
            args.output,
            "Apply Page Range Translation",
            f"Prepared {translated_label_count} label translation operation(s) across {len(target_pages)} page(s).",
            all_operations,
            quiet=not config_bool(config, "showResult", True),
        )
        return

    for page, labels in target_pages:
        page_name = page.name
        log(f"Translating {len(labels)} label(s) on page {page_name or '[unknown]'} action={action}")
        messages = prompt_for_labels(
            labels,
            page_name=page_name,
            target_language=config.get("targetLanguage", DEFAULT_CONFIG["targetLanguage"]),
            style=config.get("style", DEFAULT_CONFIG["style"]),
            custom_prompt=config.get("customPrompt", DEFAULT_CONFIG["customPrompt"]),
            include_analysis=include_analysis,
        )

        try:
            model_result = call_deepseek(config, messages)
            translations = result_items_by_label_index(model_result)
        except Exception as error:
            ctx.write_output(args.output, "AI Translation Failed", f"AI translation failed on {page_name}:\n{error}")
            return

        if action == "apply":
            page_operations = []
            for label in labels:
                translation = translations.get(label.index, {}).get("translation", "")
                if not translation or label.index < 0 or translation == label.text:
                    continue
                page_operations.append(label.set_text(translation))
            all_operations.extend(page_operations)
            translated_label_count += len(page_operations)
            continue

        preview_sections.append(preview_text(labels, translations, include_analysis))

    if action == "apply":
        page_count = len(target_pages)
        ctx.write_output(
            args.output,
            "Apply Translation",
            f"Prepared {translated_label_count} label translation operation(s) across {page_count} page(s).",
            all_operations,
            quiet=not config_bool(config, "showResult", True),
        )
        return

    text = "\n\n".join(section for section in preview_sections if section)
    ctx.write_output(args.output, "Translation Preview", text or "No translation was returned.")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:  # Keep automation output readable even for unexpected script errors.
        print(f"[ai_translate] Fatal error: {exc}", file=sys.stderr, flush=True)
        raise
