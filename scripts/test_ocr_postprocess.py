#!/usr/bin/env python3
import importlib.util
import pathlib
import unittest


def load_ocr_module():
    script_path = (
        pathlib.Path(__file__).resolve().parents[1]
        / "scripts"
        / "official"
        / "ocr_preview"
        / "ocr_preview.py"
    )
    spec = importlib.util.spec_from_file_location("labelqt_ocr_preview", script_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load OCR script: {script_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ocr = load_ocr_module()


class OcrPostprocessTests(unittest.TestCase):
    def test_vertical_columns_follow_requested_reading_direction(self):
        regions = [
            {"text": "left", "box": [10, 10, 20, 80], "score": 1.0},
            {"text": "right", "box": [70, 10, 80, 80], "score": 1.0},
        ]

        right_to_left = ocr.sort_regions(regions, vertical=True, right_to_left=True)
        left_to_right = ocr.sort_regions(regions, vertical=True, right_to_left=False)

        self.assertEqual([item["text"] for item in right_to_left], ["right", "left"])
        self.assertEqual([item["text"] for item in left_to_right], ["left", "right"])

    def test_normalization_clamps_boxes_and_discards_low_confidence(self):
        regions = [
            {"text": "kept", "box": [-5, -4, 120, 130], "score": 0.9},
            {"text": "discarded", "box": [0, 0, 20, 20], "score": 0.1},
        ]

        normalized = ocr.normalize_regions(regions, 100, 100)

        self.assertEqual(len(normalized), 1)
        self.assertEqual(normalized[0]["box"], [0, 0, 100, 100])

    def test_postprocess_deduplicates_identical_regions(self):
        regions = [
            {"text": "same", "box": [10, 10, 40, 40], "score": 0.9},
            {"text": "same", "box": [10, 10, 40, 40], "score": 0.8},
        ]

        processed = ocr.post_process_regions(
            regions,
            image_width=100,
            image_height=100,
            already_merged=True,
            right_to_left=True,
        )

        self.assertEqual(len(processed), 1)


if __name__ == "__main__":
    unittest.main()
