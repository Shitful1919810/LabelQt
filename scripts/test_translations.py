#!/usr/bin/env python3
import pathlib
import tempfile
import unittest

from check_translations import collect_source_strings


class TranslationExtractionTests(unittest.TestCase):
    def test_adjacent_literals_preserve_context_and_unicode(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / "Panel.cpp").write_text(
                'tr("first "\n "second");\n'
                'Other::tr("unicode: " "中文");\n'
                'QCoreApplication::translate("Service", "line\\n" "two");\n',
                encoding="utf-8",
            )
            sources, contexts = collect_source_strings(path)
            self.assertEqual(sources, {"first second", "unicode: 中文", "line\ntwo"})
            self.assertEqual(contexts, {("Panel", "first second"), ("Other", "unicode: 中文"),
                                        ("Service", "line\ntwo")})


if __name__ == "__main__":
    unittest.main()
