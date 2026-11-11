#!/usr/bin/env python3
import pathlib
import tempfile
import unittest

from check_translations import collect_qml_strings, collect_source_strings, cpp_default_context


class TranslationExtractionTests(unittest.TestCase):
    def collect_files(self, files):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            for name, text in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding="utf-8")
            return collect_source_strings(root)

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

    def test_qml_literals_quotes_escapes_unicode_and_concatenation(self):
        text = r'''Item {
            property string a: qsTr("Save")
            property string b: qsTr('It\'s ready')
            property string c: qsTr("Say \"hello\"")
            property string d: qsTr("first\n" + 'second')
            property string e: qsTr("中文")
            property string f: qsTr("\u4e2d\u6587")
            property string g: qsTr("Save")
        }'''
        self.assertEqual(collect_qml_strings(text, "Panel"), {
            ("Panel", "Save"), ("Panel", "It's ready"), ("Panel", 'Say "hello"'),
            ("Panel", "first\nsecond"), ("Panel", "中文"),
        })

    def test_qml_comments_and_call_looking_strings_are_ignored(self):
        text = r'''Item {
            // qsTr("Hidden line")
            /* qsTranslate("Hidden", "Hidden block") */
            property string a: "qsTr('Not a call')"
            property string b: 'qsTranslate("Hidden", "Not a call")'
            property string c: qsTr /* comment */ ("Visible // literal /* literal */")
            property string d: qsTr("One" /* joined */ + " two")
            property string e: qsTranslate(/* context */ "Explicit", /* source */ "Visible")
        }'''
        self.assertEqual(collect_qml_strings(text, "Panel"), {
            ("Panel", "Visible // literal /* literal */"), ("Panel", "One two"),
            ("Explicit", "Visible"),
        })

    def test_qml_explicit_contexts_disambiguation_plural_and_arg(self):
        text = r'''Item {
            property string a: qsTranslate("Explicit", 'Label %1').arg(2)
            property string b: qsTranslate('Other', "Open", "verb")
            property string c: qsTranslate("Count", "%n labels", "labels", count)
            property string d: qsTr("%n pages", "pages", count)
            property string e: qsTranslate("Shared" + "Context", "First " + "second")
        }'''
        self.assertEqual(collect_qml_strings(text, "Panel"), {
            ("Explicit", "Label %1"), ("Other", "Open"), ("Count", "%n labels"),
            ("Panel", "%n pages"), ("SharedContext", "First second"),
        })

    def test_qml_dynamic_arguments_do_not_collect_literal_prefixes(self):
        calls = [
            'qsTr(value)',
            'qsTr("Prefix " + value)',
            'qsTr("Prefix " + value, "comment")',
            'qsTr("Prefix " + "literal " + value)',
            'qsTr(condition ? "First" : "Second")',
            'qsTranslate(context, "Text")',
            'qsTranslate("Context" + suffix, "Text")',
            'qsTranslate("Context", value)',
            'qsTranslate("Context", "Prefix " + value)',
        ]
        for call in calls:
            with self.subTest(call=call):
                self.assertEqual(collect_qml_strings(call, "Panel"), set())

    def test_qml_similarly_named_or_member_functions_are_not_qt_calls(self):
        for call in ['myqsTr("Text")', 'qsTrExtra("Text")', 'object.qsTr("Text")',
                     'object.qsTranslate("Context", "Text")']:
            with self.subTest(call=call):
                self.assertEqual(collect_qml_strings(call, "Panel"), set())

    def test_qml_file_stems_are_distinct_case_sensitive_contexts(self):
        sources, contexts = self.collect_files({
            "mobile/Main.qml": 'Item { property string text: qsTr("Save") }',
            "mobile/ProjectCanvas.qml": 'Item { property string text: qsTr("Save") }',
            "main.cpp": 'tr("Save");',
            "ignored.txt": 'qsTr("Not a source file")',
        })
        self.assertEqual(sources, {"Save"})
        self.assertEqual(contexts, {("Main", "Save"), ("ProjectCanvas", "Save"), ("main", "Save")})

    def test_qobject_context_includes_compound_namespace(self):
        sources, contexts = self.collect_files({
            "MobileDocument.h": '''namespace labelqt::mobile {
                class MobileDocument final : public QObject { Q_OBJECT };
            }''',
            "MobileDocument.cpp": 'tr("Keep draft");',
        })
        self.assertEqual(sources, {"Keep draft"})
        self.assertEqual(contexts, {("labelqt::mobile::MobileDocument", "Keep draft")})

    def test_qobject_context_includes_nested_namespace_from_hpp(self):
        sources, contexts = self.collect_files({
            "AndroidFileBridge.hpp": '''namespace labelqt { namespace platform {
                class AndroidFileBridge : public QObject {
                    Q_OBJECT
                    QString error() { return tr("Unavailable"); }
                };
            } }''',
            "AndroidFileBridge.cpp": 'tr("Permission lost");',
        })
        self.assertEqual(sources, {"Unavailable", "Permission lost"})
        self.assertEqual(contexts, {
            ("labelqt::platform::AndroidFileBridge", "Unavailable"),
            ("labelqt::platform::AndroidFileBridge", "Permission lost"),
        })

    def test_explicit_tr_macro_overrides_namespace_and_class_name(self):
        for context in ("StableTransfer", "shared::Transfer"):
            with self.subTest(context=context):
                sources, contexts = self.collect_files({
                    "Transfer.h": f'''namespace labelqt::services {{
                        class Transfer final {{ Q_DECLARE_TR_FUNCTIONS({context}) }};
                    }}''',
                    "Transfer.cpp": 'tr("Invalid ZIP");',
                })
                self.assertEqual(sources, {"Invalid ZIP"})
                self.assertEqual(contexts, {(context, "Invalid ZIP")})

    def test_context_detection_ignores_unrelated_classes_comments_and_braces(self):
        sources, contexts = self.collect_files({
            "Panel.h": r'''namespace unrelated { class Forward; }
                class Other : public QObject { Q_OBJECT };
                // namespace fake { class Panel { Q_OBJECT };
                /* namespace wrong { class Panel { Q_DECLARE_TR_FUNCTIONS(Wrong) }; } */
                namespace app {
                    struct Helpers { void work() { if (true) {} } };
                    const char* text = "} namespace fake { class Panel { Q_OBJECT";
                    class Panel : public QObject { Q_OBJECT };
                }''',
            "Panel.cpp": 'tr("Visible");',
        })
        self.assertEqual(sources, {"Visible"})
        self.assertEqual(contexts, {("app::Panel", "Visible")})

    def test_global_qobject_and_missing_header_keep_filename_context(self):
        sources, contexts = self.collect_files({
            "Panel.h": 'class Panel : public QObject { Q_OBJECT };',
            "Panel.cpp": 'tr("Global");',
            "Standalone.cpp": 'tr("Fallback");',
        })
        self.assertEqual(sources, {"Global", "Fallback"})
        self.assertEqual(contexts, {("Panel", "Global"), ("Standalone", "Fallback")})

    def test_current_mobile_contexts_match_moc_runtime_names(self):
        src = pathlib.Path(__file__).resolve().parents[1] / "src"
        expected = {
            "mobile/MobileDocument.cpp": "labelqt::mobile::MobileDocument",
            "mobile/MobileWorkspaceController.cpp": "labelqt::mobile::MobileWorkspaceController",
            "platform/android/AndroidFileBridge.cpp": "labelqt::platform::AndroidFileBridge",
            "mobile/MobileReviewController.cpp": "MobileReviewController",
            "services/transfer/ProjectTransferService.cpp": "ProjectTransferService",
        }
        for name, context in expected.items():
            with self.subTest(path=name):
                self.assertEqual(cpp_default_context(src / name), context)


if __name__ == "__main__":
    unittest.main()
